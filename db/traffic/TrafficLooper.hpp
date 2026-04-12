#pragma once

#include <QString>
#include <QList>
#include <QMutex>
#include <QHash>

#include "TrafficData.hpp"

namespace NekoGui_traffic {
    class TrafficLooper {
    public:
        bool loop_enabled = false;
        bool looping = false;
        QMutex loop_mutex;

        QList<std::shared_ptr<TrafficData>> items;
        TrafficData *proxy = nullptr;

        void UpdateAll();

        void Loop();

        void SetNoGrpcStatsFile(const QString &path);

    private:
        TrafficData *bypass = new TrafficData("bypass");
#ifdef NKR_NO_GRPC
        QString no_grpc_stats_file;
        QHash<QString, QPair<long long, long long>> no_grpc_last_totals;
#endif

        [[nodiscard]] static TrafficData *update_stats(TrafficData *item);

        [[nodiscard]] static QJsonArray get_connection_list();
    };

    extern TrafficLooper *trafficLooper;
} // namespace NekoGui_traffic
