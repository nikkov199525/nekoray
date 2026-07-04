package main

import (
	"context"
	"errors"

	"grpc_server"
	"grpc_server/gen"

	"github.com/matsuridayo/libneko/neko_common"
	"github.com/matsuridayo/libneko/speedtest"
	box "github.com/sagernet/sing-box"
	"github.com/sagernet/sing-box/experimental/v2rayapi"
	"github.com/sagernet/sing-box/option"

	"log"
)

type server struct {
	grpc_server.BaseServer
}

var statsServer *v2rayapi.StatsService

func (s *server) Start(ctx context.Context, in *gen.LoadConfigReq) (out *gen.ErrorResp, _ error) {
	var err error

	defer func() {
		out = &gen.ErrorResp{}
		if err != nil {
			log.Println("grpc Start failed:", err)
			out.Error = err.Error()
			instance = nil
		} else {
			log.Println("grpc Start success")
		}
	}()

	if neko_common.Debug {
		log.Println("Start:", in.CoreConfig)
	}

	if instance != nil {
		err = errors.New("instance already started")
		return
	}

	log.Println("grpc Start requested")
	instance, instance_cancel, err = createAndStartBox([]byte(in.CoreConfig), true)
	if err == nil && instance != nil && len(in.StatsOutbounds) > 0 {
		statsServer = v2rayapi.NewStatsService(option.V2RayStatsServiceOptions{
			Enabled:   true,
			Outbounds: in.StatsOutbounds,
		})
		instance.Router().AppendTracker(statsServer)
	}
	return
}

func (s *server) Stop(ctx context.Context, in *gen.EmptyReq) (out *gen.ErrorResp, _ error) {
	var err error

	defer func() {
		out = &gen.ErrorResp{}
		if err != nil {
			log.Println("grpc Stop failed:", err)
			out.Error = err.Error()
		} else {
			log.Println("grpc Stop success")
		}
	}()

	if instance == nil {
		return
	}

	log.Println("grpc Stop requested")
	instance_cancel()
	_ = instance.Close()
	instance = nil
	statsServer = nil
	return
}

func (s *server) Test(ctx context.Context, in *gen.TestReq) (out *gen.TestResp, _ error) {
	var err error
	out = &gen.TestResp{Ms: 0}

	defer func() {
		if err != nil {
			out.Error = err.Error()
		}
	}()

	if in.Mode == gen.TestMode_UrlTest {
		var i *box.Box
		var cancel context.CancelFunc
		if in.Config != nil {
			i, cancel, err = createAndStartBox([]byte(in.Config.CoreConfig), true)
			if i != nil {
				defer i.Close()
				defer cancel()
			}
			if err != nil {
				return
			}
		} else {
			i = instance
			if i == nil {
				return
			}
		}
		out.Ms, err = speedtest.UrlTest(createProxyHTTPClient(i), in.Url, in.Timeout, speedtest.UrlTestStandard_RTT)
	} else if in.Mode == gen.TestMode_TcpPing {
		out.Ms, err = speedtest.TcpPing(in.Address, in.Timeout)
	} else if in.Mode == gen.TestMode_FullTest {
		i, cancel, e := createAndStartBox([]byte(in.Config.CoreConfig), true)
		err = e
		if i != nil {
			defer i.Close()
			defer cancel()
		}
		if err != nil {
			return
		}
		return grpc_server.DoFullTest(ctx, in, i)
	}

	return
}

func (s *server) QueryStats(ctx context.Context, in *gen.QueryStatsReq) (out *gen.QueryStatsResp, _ error) {
	out = &gen.QueryStatsResp{}
	if statsServer != nil {
		out.Traffic = queryStats(statsServer, "outbound>>>"+in.Tag+">>>traffic>>>"+in.Direct)
	}
	return
}

func (s *server) ListConnections(ctx context.Context, in *gen.EmptyReq) (*gen.ListConnectionsResp, error) {
	out := &gen.ListConnectionsResp{
		// TODO upstream api
	}
	return out, nil
}
