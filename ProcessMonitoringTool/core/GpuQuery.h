#pragma once
#include <QHash>
#include <QtGlobal>

namespace GpuQuery {
    bool query(QHash<quint32, double>& out); // pid -> 0..100
}