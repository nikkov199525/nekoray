package main

import (
	"context"
	"net"
	"net/http"
	"time"

	"github.com/matsuridayo/libneko/neko_common"
	"github.com/matsuridayo/libneko/neko_log"
	box "github.com/sagernet/sing-box"
	"github.com/sagernet/sing-box/common/dialer"
	M "github.com/sagernet/sing/common/metadata"
)

var instance *box.Box
var instance_cancel context.CancelFunc

func dialBoxContext(ctx context.Context, instance *box.Box, network, addr string) (net.Conn, error) {
	defaultOutbound := instance.Outbound().Default()
	return dialer.NewDetour(instance.Outbound(), defaultOutbound.Tag(), true).DialContext(ctx, network, M.ParseSocksaddr(addr))
}

func createProxyHTTPClient(instance *box.Box) *http.Client {
	transport := &http.Transport{
		TLSHandshakeTimeout:   3 * time.Second,
		ResponseHeaderTimeout: 3 * time.Second,
	}
	if instance != nil {
		transport.DialContext = func(ctx context.Context, network, addr string) (net.Conn, error) {
			return dialBoxContext(ctx, instance, network, addr)
		}
	}
	return &http.Client{Transport: transport}
}

func setupCore() {
	neko_log.SetupLog(50*1024, "./neko.log")
	neko_common.GetCurrentInstance = func() interface{} {
		return instance
	}
	neko_common.DialContext = func(ctx context.Context, specifiedInstance interface{}, network, addr string) (net.Conn, error) {
		if i, ok := specifiedInstance.(*box.Box); ok {
			return dialBoxContext(ctx, i, network, addr)
		}
		if instance != nil {
			return dialBoxContext(ctx, instance, network, addr)
		}
		return neko_common.DialContextSystem(ctx, network, addr)
	}
	neko_common.DialUDP = func(ctx context.Context, specifiedInstance interface{}) (net.PacketConn, error) {
		_ = specifiedInstance
		return neko_common.DialUDPSystem(ctx)
	}
	neko_common.CreateProxyHttpClient = func(specifiedInstance interface{}) *http.Client {
		if i, ok := specifiedInstance.(*box.Box); ok {
			return createProxyHTTPClient(i)
		}
		return createProxyHTTPClient(instance)
	}
}
