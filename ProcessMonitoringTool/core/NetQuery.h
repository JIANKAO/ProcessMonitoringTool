#pragma once
#include <QHash>
#include <QtGlobal>

namespace NetQuery {
    // A 方案（ETW）预留接口。默认关闭；返回 false 表示当前不可用。
    bool start();
    void stop();
    bool query(QHash<quint32, quint64>& outBps);
}