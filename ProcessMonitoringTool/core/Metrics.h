#pragma once
#include <QString>
#include <QVector>
#include <QMetaType>

enum class MetricType : int {
    CPU = 0, Memory, Disk, Network, GPU, Count
};

inline const char* metricKey(MetricType m) {
    switch (m) {
    case MetricType::CPU:     return "metric.cpu";
    case MetricType::Memory:  return "metric.mem";
    case MetricType::Disk:    return "metric.disk";
    case MetricType::Network: return "metric.net";
    case MetricType::GPU:     return "metric.gpu";
    default: return "";
    }
}

struct RawProc {
    quint32 pid = 0;
    quint64 createTime = 0;
    quint64 cpuTime = 0;
    quint64 workingSet = 0;
    quint64 ioBytes = 0;
    QString name;
};

struct ProcessInfo {
    quint32 pid = 0;
    quint64 createTime = 0;
    QString name;
    double  cpu = 0.0;
    quint64 workingSet = 0;
    quint64 diskBps = 0;
    quint64 netBps = 0;
    double  gpu = 0.0;
};

struct RankEntry {
    quint32 pid = 0;
    QString key;
    QString path;
    QString name;
    double  value = 0.0;
    qint64  firstSeenMs = 0;
    qint64  durationMs = 0;
    bool    stale = false;       // 进程已终止
    qint64  staleSinceMs = 0;    // 变为 stale 的时刻
    int     procCount = 1;       // 聚合了几个进程
};