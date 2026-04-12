package main

import (
	"context"
	stdlog "log"

	box "github.com/sagernet/sing-box"
	"github.com/sagernet/sing-box/experimental/deprecated"
	"github.com/sagernet/sing-box/include"
	sblog "github.com/sagernet/sing-box/log"
	"github.com/sagernet/sing-box/option"
	"github.com/sagernet/sing/common/json"
	"github.com/sagernet/sing/service"
)

func createAndStartBox(configContent []byte, disableColor bool) (*box.Box, context.CancelFunc, error) {
	stdlog.Println("createAndStartBox: parse config")
	baseCtx := context.Background()
	baseCtx = include.Context(service.ContextWith(baseCtx, deprecated.NewStderrManager(sblog.StdLogger())))

	options, err := json.UnmarshalExtendedContext[option.Options](baseCtx, configContent)
	if err != nil {
		stdlog.Println("createAndStartBox: parse config failed:", err)
		return nil, nil, err
	}
	stdlog.Println("createAndStartBox: parse config done")
	if disableColor {
		if options.Log == nil {
			options.Log = &option.LogOptions{}
		}
		options.Log.DisableColor = true
	}

	stdlog.Println("createAndStartBox: box.New")
	ctx, cancel := context.WithCancel(baseCtx)
	instance, err := box.New(box.Options{
		Context: ctx,
		Options: options,
	})
	if err != nil {
		stdlog.Println("createAndStartBox: box.New failed:", err)
		cancel()
		return nil, nil, err
	}
	stdlog.Println("createAndStartBox: box.New done")
	stdlog.Println("createAndStartBox: instance.Start")
	if err = instance.Start(); err != nil {
		stdlog.Println("createAndStartBox: instance.Start failed:", err)
		cancel()
		_ = instance.Close()
		return nil, nil, err
	}
	stdlog.Println("createAndStartBox: instance.Start done")
	return instance, cancel, nil
}
