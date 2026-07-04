package main

import (
	"context"
	"encoding/json"
	"errors"
	"log"
	"os"
	"os/signal"
	"path/filepath"
	"strings"
	"syscall"
	"time"

	"github.com/sagernet/sing-box/experimental/v2rayapi"
	"github.com/sagernet/sing-box/option"
)

type runtimeOutboundStats struct {
	Uplink       int64 `json:"uplink"`
	Downlink     int64 `json:"downlink"`
	UplinkRate   int64 `json:"uplink_rate"`
	DownlinkRate int64 `json:"downlink_rate"`
}

func parseStatsOutbounds(value string) []string {
	if value == "" {
		return nil
	}
	out := make([]string, 0)
	seen := make(map[string]bool)
	for _, raw := range strings.Split(value, ",") {
		tag := strings.TrimSpace(raw)
		if tag == "" || seen[tag] {
			continue
		}
		seen[tag] = true
		out = append(out, tag)
	}
	return out
}

func writeStatsSnapshot(statsFile string, stats map[string]*runtimeOutboundStats) error {
	dir := filepath.Dir(statsFile)
	if dir != "" && dir != "." {
		if err := os.MkdirAll(dir, 0o755); err != nil {
			return err
		}
	}

	payload := make(map[string]runtimeOutboundStats, len(stats))
	for tag, item := range stats {
		if item == nil {
			continue
		}
		payload[tag] = *item
	}
	content, err := json.Marshal(payload)
	if err != nil {
		return err
	}

	tmpFile := statsFile + ".tmp"
	if err := os.WriteFile(tmpFile, content, 0o644); err != nil {
		return err
	}
	_ = os.Remove(statsFile)
	return os.Rename(tmpFile, statsFile)
}

func queryStats(statsServer *v2rayapi.StatsService, name string) int64 {
	response, err := statsServer.GetStats(context.Background(), &v2rayapi.GetStatsRequest{
		Name:   name,
		Reset_: true,
	})
	if err != nil || response.Stat == nil {
		return 0
	}
	return response.Stat.Value
}

func startStatsWriter(statsFile string, statsServer *v2rayapi.StatsService, outbounds []string) context.CancelFunc {
	ctx, cancel := context.WithCancel(context.Background())

	go func() {
		stats := make(map[string]*runtimeOutboundStats, len(outbounds))
		for _, tag := range outbounds {
			stats[tag] = &runtimeOutboundStats{}
		}

		if err := writeStatsSnapshot(statsFile, stats); err != nil {
			log.Println("stats writer init failed:", err)
		}

		ticker := time.NewTicker(time.Second)
		defer ticker.Stop()
		lastTick := time.Now()

		for {
			select {
			case <-ctx.Done():
				if err := writeStatsSnapshot(statsFile, stats); err != nil {
					log.Println("stats writer final flush failed:", err)
				}
				return
			case now := <-ticker.C:
				interval := now.Sub(lastTick).Milliseconds()
				if interval <= 0 {
					interval = 1000
				}
				lastTick = now

				for _, tag := range outbounds {
					item := stats[tag]
					if item == nil {
						continue
					}
					uplink := queryStats(statsServer, "outbound>>>"+tag+">>>traffic>>>uplink")
					downlink := queryStats(statsServer, "outbound>>>"+tag+">>>traffic>>>downlink")
					item.Uplink += uplink
					item.Downlink += downlink
					item.UplinkRate = uplink * 1000 / interval
					item.DownlinkRate = downlink * 1000 / interval
				}

				if err := writeStatsSnapshot(statsFile, stats); err != nil {
					log.Println("stats writer flush failed:", err)
				}
			}
		}
	}()

	return cancel
}

func runSingBoxCommand(args []string) error {
	disableColor := false
	var configPath string
	var statsFile string
	var statsOutbounds []string
	runMode := false

	for i := 0; i < len(args); i++ {
		arg := args[i]
		switch arg {
		case "run":
			runMode = true
		case "--disable-color":
			disableColor = true
		case "-c", "--config":
			if i+1 >= len(args) {
				return errors.New("missing value for -c/--config")
			}
			i++
			configPath = args[i]
		case "--stats-file":
			if i+1 >= len(args) {
				return errors.New("missing value for --stats-file")
			}
			i++
			statsFile = args[i]
		case "--stats-outbounds":
			if i+1 >= len(args) {
				return errors.New("missing value for --stats-outbounds")
			}
			i++
			statsOutbounds = parseStatsOutbounds(args[i])
		}
	}

	if !runMode {
		return errors.New("unsupported command (expected: run)")
	}
	if configPath == "" {
		configPath = "config.json"
	}

	configContent, err := os.ReadFile(configPath)
	if err != nil {
		return err
	}

	ins, cancel, err := createAndStartBox(configContent, disableColor)
	if err != nil {
		return err
	}
	defer ins.Close()
	defer cancel()

	if statsFile != "" {
		if len(statsOutbounds) == 0 {
			statsOutbounds = []string{"proxy", "bypass"}
		}
		statsServer := v2rayapi.NewStatsService(option.V2RayStatsServiceOptions{
			Enabled:   true,
			Outbounds: statsOutbounds,
		})
		ins.Router().AppendTracker(statsServer)
		stopStatsWriter := startStatsWriter(statsFile, statsServer, statsOutbounds)
		defer stopStatsWriter()
	}

	osSignals := make(chan os.Signal, 1)
	signal.Notify(osSignals, os.Interrupt, syscall.SIGTERM, syscall.SIGHUP)
	defer signal.Stop(osSignals)

	<-osSignals
	return nil
}
