#pragma once
#include "Metrics.h"
#include <QVector>

namespace ProcessQuery {
    bool enumAll(QVector<RawProc>& out);
    QString resolvePath(quint32 pid, quint64 createTime);
    void clearPathCache();
}