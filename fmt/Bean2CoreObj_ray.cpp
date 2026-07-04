#include "db/ProxyEntity.hpp"
#include "fmt/includes.h"

#define MAKE_SETTINGS_STREAM_SETTINGS                         \
    outbound["settings"] = settings;                          \
    auto streamSettings = stream->BuildStreamSettingsV2Ray(); \
    outbound["streamSettings"] = streamSettings;

namespace NekoGui_fmt {
    QJsonObject V2rayStreamSettings::BuildStreamSettingsV2Ray() {
        auto normalizedNetwork = network.trimmed().isEmpty() ? "tcp" : network.trimmed();
        QJsonObject streamSettings{{"network", normalizedNetwork}};

        if (normalizedNetwork == "ws") {
            QJsonObject ws;
            if (!host.isEmpty()) ws["headers"] = QJsonObject{{"Host", host}};
            if (!path.isEmpty()) ws["path"] = path;
            if (ws_early_data_length > 0) {
                ws["maxEarlyData"] = ws_early_data_length;
                ws["earlyDataHeaderName"] = ws_early_data_name.isEmpty() ? "Sec-WebSocket-Protocol" : ws_early_data_name;
            }
            streamSettings["wsSettings"] = ws;
        } else if (normalizedNetwork == "http") {
            QJsonObject http;
            if (!path.isEmpty()) http["path"] = path;
            if (!host.isEmpty()) http["host"] = QList2QJsonArray(host.split(","));
            streamSettings["httpSettings"] = http;
        } else if (normalizedNetwork == "grpc") {
            QJsonObject grpc;
            if (!path.isEmpty()) grpc["serviceName"] = path;
            streamSettings["grpcSettings"] = grpc;
        } else if (normalizedNetwork == "httpupgrade") {
            QJsonObject hup;
            if (!path.isEmpty()) hup["path"] = path;
            if (!host.isEmpty()) hup["host"] = host;
            streamSettings["httpupgradeSettings"] = hup;
        } else if (normalizedNetwork == "xhttp") {
            streamSettings["network"] = "xhttp";
            QJsonObject xhttp;
            if (!path.isEmpty()) xhttp["path"] = path;
            if (!host.isEmpty()) xhttp["host"] = host;
            if (!xhttp_mode.isEmpty()) xhttp["mode"] = xhttp_mode;
            streamSettings["xhttpSettings"] = xhttp;
        } else if (normalizedNetwork == "splithttp") {
            streamSettings["network"] = "splithttp";
            QJsonObject splitHttp;
            if (!path.isEmpty()) splitHttp["path"] = path;
            if (!host.isEmpty()) splitHttp["host"] = host;
            if (!xhttp_mode.isEmpty()) splitHttp["mode"] = xhttp_mode;
            streamSettings["splithttpSettings"] = splitHttp;
        } else if (normalizedNetwork == "quic") {
            QJsonObject quic;
            if (!header_type.isEmpty()) quic["header"] = QJsonObject{{"type", header_type}};
            if (!path.isEmpty()) quic["key"] = path;
            if (!host.isEmpty()) quic["security"] = host;
            streamSettings["quicSettings"] = quic;
        } else if (normalizedNetwork == "tcp" && !header_type.isEmpty()) {
            QJsonObject header{{"type", header_type}};
            if (header_type == "http") {
                header["request"] = QJsonObject{
                    {"path", QList2QJsonArray(path.split(","))},
                    {"headers", QJsonObject{{"Host", QList2QJsonArray(host.split(","))}}},
                };
            }
            streamSettings["tcpSettings"] = QJsonObject{{"header", header}};
        }

        if (security == "tls") {
            auto fp = utlsFingerprint.isEmpty() ? NekoGui::dataStore->utlsFingerprint : utlsFingerprint;
            QJsonObject tls;
            if (!fp.trimmed().isEmpty()) tls["fingerprint"] = fp;
            if (!sni.trimmed().isEmpty()) tls["serverName"] = sni;
            if (reality_pbk.trimmed().isEmpty()) {
                // Xray 26.3.27 rejects allowInsecure after 2026-06-01.
                // Keep certificate bypass as a sing-box-only option; emitting the
                // removed Xray field makes the whole generated config invalid.
                if (!alpn.trimmed().isEmpty()) tls["alpn"] = QList2QJsonArray(alpn.split(","));
                if (!certificate.trimmed().isEmpty()) {
                    tls["disableSystemRoot"] = true;
                    tls["certificates"] = QJsonArray{
                        QJsonObject{
                            {"usage", "verify"},
                            {"certificate", QList2QJsonArray(SplitLines(certificate.trimmed()))},
                        },
                    };
                }
                streamSettings["tlsSettings"] = tls;
                streamSettings["security"] = "tls";
            } else {
                tls["publicKey"] = reality_pbk;
                tls["shortId"] = reality_sid;
                tls["spiderX"] = reality_spx;
                streamSettings["realitySettings"] = tls;
                streamSettings["security"] = "reality";
            }
        }

        return streamSettings;
    }

    CoreObjOutboundBuildResult SocksHttpBean::BuildCoreObjV2Ray() {
        CoreObjOutboundBuildResult result;

        QJsonObject outbound;
        outbound["protocol"] = socks_http_type == type_HTTP ? "http" : "socks";

        QJsonObject settings;
        QJsonArray servers;
        QJsonObject server;

        server["address"] = serverAddress;
        server["port"] = serverPort;
        if (socks_http_type == type_Socks4) {
            server["version"] = "4";
        }

        if (!username.isEmpty() && !password.isEmpty()) {
            server["users"] = QJsonArray{
                QJsonObject{
                    {"user", username},
                    {"pass", password},
                },
            };
        }

        servers.push_back(server);
        settings["servers"] = servers;

        MAKE_SETTINGS_STREAM_SETTINGS

        result.outbound = outbound;
        return result;
    }

    CoreObjOutboundBuildResult ShadowSocksBean::BuildCoreObjV2Ray() {
        CoreObjOutboundBuildResult result;

        QJsonObject outbound{{"protocol", "shadowsocks"}};

        QJsonObject settings;
        QJsonArray servers;
        QJsonObject server;

        server["address"] = serverAddress;
        server["port"] = serverPort;
        server["method"] = method;
        server["password"] = password;
        server["uot"] = uot;

        servers.push_back(server);
        settings["servers"] = servers;

        if (!plugin.trimmed().isEmpty()) {
            settings["plugin"] = SubStrBefore(plugin, ";");
            settings["pluginOpts"] = SubStrAfter(plugin, ";");
        }

        MAKE_SETTINGS_STREAM_SETTINGS

        result.outbound = outbound;
        return result;
    }

    CoreObjOutboundBuildResult VMessBean::BuildCoreObjV2Ray() {
        CoreObjOutboundBuildResult result;
        QJsonObject outbound{{"protocol", "vmess"}};

        QJsonObject settings{
            {"vnext", QJsonArray{
                          QJsonObject{
                              {"address", serverAddress},
                              {"port", serverPort},
                              {"users", QJsonArray{
                                            QJsonObject{
                                                {"id", uuid.trimmed()},
                                                {"alterId", aid},
                                                {"security", security},
                                            },
                                        }},
                          },
                      }},
        };

        MAKE_SETTINGS_STREAM_SETTINGS

        result.outbound = outbound;
        return result;
    }

    CoreObjOutboundBuildResult TrojanVLESSBean::BuildCoreObjV2Ray() {
        CoreObjOutboundBuildResult result;
        QJsonObject outbound{
            {"protocol", proxy_type == proxy_VLESS ? "vless" : "trojan"},
        };

        QJsonObject settings;
        if (proxy_type == proxy_VLESS) {
            auto adjustedFlow = flow;
            if (adjustedFlow.right(7) == "-udp443") {
                adjustedFlow.chop(7);
            } else if (adjustedFlow == "none") {
                adjustedFlow = "";
            }
            settings = QJsonObject{
                {"vnext", QJsonArray{
                              QJsonObject{
                                  {"address", serverAddress},
                                  {"port", serverPort},
                                  {"users", QJsonArray{
                                                QJsonObject{
                                                    {"id", password.trimmed()},
                                                    {"encryption", "none"},
                                                    {"flow", adjustedFlow},
                                                },
                                            }},
                              },
                          }},
            };
        } else {
            settings = QJsonObject{
                {"servers", QJsonArray{
                                QJsonObject{
                                    {"address", serverAddress},
                                    {"port", serverPort},
                                    {"password", password},
                                },
                            }},
            };
        }

        MAKE_SETTINGS_STREAM_SETTINGS

        result.outbound = outbound;
        return result;
    }

    CoreObjOutboundBuildResult CustomBean::BuildCoreObjV2Ray() {
        CoreObjOutboundBuildResult result;

        if (core == "internal") {
            result.outbound = QString2QJsonObject(config_simple);
        }

        return result;
    }
} // namespace NekoGui_fmt
