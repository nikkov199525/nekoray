#include "db/ProxyEntity.hpp"
#include "fmt/includes.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QUrl>

#define WriteTempFile(fn, data)                                   \
    QDir dir;                                                     \
    if (!dir.exists("temp")) dir.mkdir("temp");                   \
    QFile f(QStringLiteral("temp/") + fn);                               \
    bool ok = f.open(QIODevice::WriteOnly | QIODevice::Truncate); \
    if (ok) {                                                     \
        f.write(data);                                            \
    } else {                                                      \
        result.error = f.errorString();                           \
    }                                                             \
    f.close();                                                    \
    auto TempFile = QFileInfo(f).absoluteFilePath();

namespace NekoGui_fmt {
    namespace {
        QString resolveProgramWithFallback(const QString &coreName) {
            auto configured = NekoGui::dataStore->extraCore->Get(coreName).trimmed();
            if (!configured.isEmpty()) return configured;
#ifdef Q_OS_WIN
            auto appCore = NekoGui::FindCoreAsset(coreName + ".exe");
#else
            auto appCore = NekoGui::FindCoreAsset(coreName);
#endif
            if (!appCore.isEmpty()) return appCore;
            return coreName;
        }

        void setOutboundServerAndPort(QJsonObject &outbound, const QString &address, int port) {
            auto settings = outbound["settings"].toObject();
            if (settings.isEmpty()) return;

            auto vnext = settings["vnext"].toArray();
            if (!vnext.isEmpty()) {
                auto first = vnext.at(0).toObject();
                first["address"] = address;
                first["port"] = port;
                vnext[0] = first;
                settings["vnext"] = vnext;
                outbound["settings"] = settings;
                return;
            }

            auto servers = settings["servers"].toArray();
            if (!servers.isEmpty()) {
                auto first = servers.at(0).toObject();
                first["address"] = address;
                first["port"] = port;
                servers[0] = first;
                settings["servers"] = servers;
                outbound["settings"] = settings;
            }
        }

        ExternalBuildResult buildXrayExternal(const CoreObjOutboundBuildResult &coreR, int mapping_port, int socks_port) {
            ExternalBuildResult result{resolveProgramWithFallback("xray")};

            if (!coreR.error.isEmpty()) {
                result.error = coreR.error;
                return result;
            }
            if (coreR.outbound.isEmpty()) {
                result.error = "unsupported outbound";
                return result;
            }

            auto findXrayAsset = [&](const QString &assetName) {
                auto asset = NekoGui::FindCoreAsset(assetName);
                if (!asset.isEmpty()) return asset;
                auto programInfo = QFileInfo(result.program);
                if (programInfo.exists() && programInfo.isFile()) {
                    auto sidecar = QFileInfo(programInfo.absolutePath() + "/" + assetName);
                    if (sidecar.exists()) return sidecar.absoluteFilePath();
                }
                return QString{};
            };
            auto geoip = findXrayAsset("geoip.dat");
            auto geosite = findXrayAsset("geosite.dat");
            if (geoip.isEmpty()) result.error = "geoip.dat not found";
            if (geosite.isEmpty()) result.error = "geosite.dat not found";
            if (!result.error.isEmpty()) return result;
            auto geoipDir = QFileInfo(geoip).absolutePath();
            auto geositeDir = QFileInfo(geosite).absolutePath();
            if (geoipDir.isEmpty() || geositeDir.isEmpty() || geoipDir != geositeDir) {
                result.error = "geoip.dat/geosite.dat must be in the same directory";
                return result;
            }
            result.env = QProcessEnvironment::systemEnvironment().toStringList();
            result.env += "XRAY_LOCATION_ASSET=" + geoipDir;

            auto outbound = coreR.outbound;
            if (mapping_port > 0) {
                setOutboundServerAndPort(outbound, "127.0.0.1", mapping_port);
            }
            outbound["tag"] = "proxy";

            QJsonObject inbound{
                {"tag", "socks-in"},
                {"protocol", "socks"},
                {"listen", "127.0.0.1"},
                {"port", socks_port},
                {"settings", QJsonObject{{"udp", true}}},
            };

            QJsonObject config{
                {"log", QJsonObject{{"loglevel", NekoGui::dataStore->log_level}}},
                {"inbounds", QJsonArray{inbound}},
                {"outbounds", QJsonArray{outbound}},
                {"routing", QJsonObject{
                                {"domainStrategy", "AsIs"},
                                {"rules", QJsonArray{
                                              QJsonObject{
                                                  {"type", "field"},
                                                  {"domain", QJsonArray{"geosite:cn"}},
                                                  {"outboundTag", "proxy"},
                                              },
                                              QJsonObject{
                                                  {"type", "field"},
                                                  {"ip", QJsonArray{"geoip:cn"}},
                                                  {"outboundTag", "proxy"},
                                              },
                                          }},
                            }},
            };

            result.config_export = QJsonObject2QString(config, false);
            WriteTempFile("xray_" + GetRandomString(10) + ".json", result.config_export.toUtf8());
            result.arguments = QStringList{"run", "-c", TempFile};
            return result;
        }
    } // namespace

    // -1: Cannot use this config
    // 0: Internal
    // 1: Mapping External
    // 2: Direct External

    int NaiveBean::NeedExternal(bool isFirstProfile) {
        if (isFirstProfile) {
            if (NekoGui::dataStore->spmode_vpn) {
                return 1;
            }
            return 2;
        }
        return 1;
    }

    int QUICBean::NeedExternal(bool isFirstProfile) {
        auto extCore = [=] {
            if (isFirstProfile) {
                if (NekoGui::dataStore->spmode_vpn && hopPort.trimmed().isEmpty()) {
                    return 1;
                }
                return 2;
            } else {
                if (!hopPort.trimmed().isEmpty()) {
                    return -1;
                }
            }
            return 1;
        };

        if (!forceExternal) {
            // sing-box support
            return 0;
        } else {
            // external core support
            return extCore();
        }
    }

    int CustomBean::NeedExternal(bool isFirstProfile) {
        if (core == "internal" || core == "internal-full") return 0;
        return 1;
    }

    int VMessBean::NeedExternal(bool isFirstProfile) {
        Q_UNUSED(isFirstProfile)
        if (stream->NeedXrayCore()) return 1;
        return 0;
    }

    int TrojanVLESSBean::NeedExternal(bool isFirstProfile) {
        Q_UNUSED(isFirstProfile)
        if (stream->NeedXrayCore()) return 1;
        return 0;
    }

    ExternalBuildResult NaiveBean::BuildExternal(int mapping_port, int socks_port, int external_stat) {
        ExternalBuildResult result{NekoGui::dataStore->extraCore->Get("naive")};

        auto is_direct = external_stat == 2;
        auto domain_address = sni.isEmpty() ? serverAddress : sni;
        auto connect_address = is_direct ? serverAddress : "127.0.0.1";
        auto connect_port = is_direct ? serverPort : mapping_port;
        domain_address = WrapIPV6Host(domain_address);
        connect_address = WrapIPV6Host(connect_address);

        auto proxy_url = QUrl();
        proxy_url.setScheme(protocol);
        proxy_url.setUserName(username);
        proxy_url.setPassword(password);
        proxy_url.setPort(connect_port);
        proxy_url.setHost(domain_address);

        if (!disable_log) result.arguments += "--log";
        result.arguments += "--listen=socks://127.0.0.1:" + Int2String(socks_port);
        result.arguments += "--proxy=" + proxy_url.toString(QUrl::FullyEncoded);
        if (domain_address != connect_address)
            result.arguments += "--host-resolver-rules=MAP " + domain_address + " " + connect_address;
        if (insecure_concurrency > 0) result.arguments += "--insecure-concurrency=" + Int2String(insecure_concurrency);
        if (!extra_headers.trimmed().isEmpty()) result.arguments += "--extra-headers=" + extra_headers;
        if (!certificate.trimmed().isEmpty()) {
            WriteTempFile("naive_" + GetRandomString(10) + ".crt", certificate.toUtf8());
            result.env += "SSL_CERT_FILE=" + TempFile;
        }

        auto config_export = QStringList{result.program};
        config_export += result.arguments;
        result.config_export = QStringList2Command(config_export);

        return result;
    }

    ExternalBuildResult QUICBean::BuildExternal(int mapping_port, int socks_port, int external_stat) {
        if (proxy_type == proxy_TUIC) {
            ExternalBuildResult result{NekoGui::dataStore->extraCore->Get("tuic")};

            QJsonObject relay;

            relay["uuid"] = uuid;
            relay["password"] = password;
            relay["udp_relay_mode"] = udpRelayMode;
            relay["congestion_control"] = congestionControl;
            relay["zero_rtt_handshake"] = zeroRttHandshake;
            relay["disable_sni"] = disableSni;
            if (!heartbeat.trimmed().isEmpty()) relay["heartbeat"] = heartbeat;
            if (!alpn.trimmed().isEmpty()) relay["alpn"] = QList2QJsonArray(alpn.split(","));

            if (!caText.trimmed().isEmpty()) {
                WriteTempFile("tuic_" + GetRandomString(10) + ".crt", caText.toUtf8());
                QJsonArray certificate;
                certificate.append(TempFile);
                relay["certificates"] = certificate;
            }

            // The most confused part of TUIC......
            if (serverAddress == sni) {
                relay["server"] = serverAddress + ":" + Int2String(serverPort);
            } else {
                relay["server"] = sni + ":" + Int2String(serverPort);
                relay["ip"] = serverAddress;
            }

            QJsonObject local{
                {"server", "127.0.0.1:" + Int2String(socks_port)},
            };

            QJsonObject config{
                {"relay", relay},
                {"local", local},
            };

            //

            result.config_export = QJsonObject2QString(config, false);
            WriteTempFile("tuic_" + GetRandomString(10) + ".json", result.config_export.toUtf8());
            result.arguments = QStringList{"-c", TempFile};

            return result;
        } else if (proxy_type == proxy_Hysteria2) {
            ExternalBuildResult result{NekoGui::dataStore->extraCore->Get("hysteria2")};

            QJsonObject config;

            auto server = serverAddress;
            if (!hopPort.trimmed().isEmpty()) {
                server = WrapIPV6Host(server) + ":" + hopPort;
            } else {
                server = WrapIPV6Host(server) + ":" + Int2String(serverPort);
            }

            QJsonObject transport;
            transport["type"] = "udp";
            transport["udp"] = QJsonObject{
                {"hopInterval", QString::number(hopInterval) + "s"},
            };
            config["transport"] = transport;

            config["server"] = server;
            config["socks5"] = QJsonObject{
                {"listen", "127.0.0.1:" + Int2String(socks_port)},
                {"disableUDP", false},
            };
            config["auth"] = password;

            QJsonObject bandwidth;
            if (uploadMbps > 0) bandwidth["up"] = Int2String(uploadMbps) + " mbps";
            if (downloadMbps > 0) bandwidth["down"] = Int2String(downloadMbps) + " mbps";
            config["bandwidth"] = bandwidth;

            QJsonObject quic;
            if (streamReceiveWindow > 0) quic["initStreamReceiveWindow"] = streamReceiveWindow;
            if (connectionReceiveWindow > 0) quic["initConnReceiveWindow"] = connectionReceiveWindow;
            if (disableMtuDiscovery) quic["disablePathMTUDiscovery"] = true;
            config["quic"] = quic;

            config["fastOpen"] = true;
            config["lazy"] = true;

            if (!obfsPassword.isEmpty()) {
                QJsonObject obfs;
                obfs["type"] = "salamander";
                obfs["salamander"] = QJsonObject{
                    {"password", obfsPassword},
                };

                config["obfs"] = obfs;
            }

            QJsonObject tls;
            auto sniGen = sni;
            if (sni.isEmpty() && !IsIpAddress(serverAddress)) sniGen = serverAddress;
            tls["sni"] = sniGen;
            if (allowInsecure) tls["insecure"] = true;
            if (!caText.trimmed().isEmpty()) {
                WriteTempFile("hysteria2_" + GetRandomString(10) + ".crt", caText.toUtf8());
                QJsonArray certificate;
                certificate.append(TempFile);
                tls["certificates"] = certificate;
            }
            config["tls"] = tls;

            result.config_export = QJsonObject2QString(config, false);
            WriteTempFile("hysteria2_" + GetRandomString(10) + ".json", result.config_export.toUtf8());
            result.arguments = QStringList{"-c", TempFile};

            return result;
        }
        ExternalBuildResult e;
        e.error = "unknown type";
        return e;
    }

    ExternalBuildResult CustomBean::BuildExternal(int mapping_port, int socks_port, int external_stat) {
        ExternalBuildResult result{NekoGui::dataStore->extraCore->Get(core)};

        result.arguments = command; // TODO split?

        for (int i = 0; i < result.arguments.length(); i++) {
            auto arg = result.arguments[i];
            arg = arg.replace("%mapping_port%", Int2String(mapping_port));
            arg = arg.replace("%socks_port%", Int2String(socks_port));
            arg = arg.replace("%server_addr%", serverAddress);
            arg = arg.replace("%server_port%", Int2String(serverPort));
            result.arguments[i] = arg;
        }

        if (!config_simple.trimmed().isEmpty()) {
            auto config = config_simple;
            config = config.replace("%mapping_port%", Int2String(mapping_port));
            config = config.replace("%socks_port%", Int2String(socks_port));
            config = config.replace("%server_addr%", serverAddress);
            config = config.replace("%server_port%", Int2String(serverPort));

            // suffix
            QString suffix;
            if (!config_suffix.isEmpty()) {
                suffix = "." + config_suffix;
            } else if (!QString2QJsonObject(config).isEmpty()) {
                // trojan-go: unsupported config format: xxx.tmp. use .yaml or .json instead.
                suffix = ".json";
            }

            // write config
            WriteTempFile("custom_" + GetRandomString(10) + suffix, config.toUtf8());
            for (int i = 0; i < result.arguments.count(); i++) {
                result.arguments[i] = result.arguments[i].replace("%config%", TempFile);
            }

            result.config_export = config;
        }

        return result;
    }

    ExternalBuildResult VMessBean::BuildExternal(int mapping_port, int socks_port, int external_stat) {
        Q_UNUSED(external_stat)
        return buildXrayExternal(BuildCoreObjV2Ray(), mapping_port, socks_port);
    }

    ExternalBuildResult TrojanVLESSBean::BuildExternal(int mapping_port, int socks_port, int external_stat) {
        Q_UNUSED(external_stat)
        return buildXrayExternal(BuildCoreObjV2Ray(), mapping_port, socks_port);
    }

} // namespace NekoGui_fmt
