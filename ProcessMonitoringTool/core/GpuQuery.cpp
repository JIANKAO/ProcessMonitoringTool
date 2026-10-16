#include "GpuQuery.h"
#include <windows.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <QRegularExpression>

namespace {
    PDH_HQUERY  g_q = nullptr;
    PDH_HCOUNTER g_c = nullptr;
    bool g_init = false, g_failed = false;

    bool initOnce() {
        if (g_init) return true;
        if (g_failed) return false;
        if (PdhOpenQueryW(nullptr, 0, &g_q) != ERROR_SUCCESS) { g_failed = true; return false; }
        if (PdhAddEnglishCounterW(g_q, L"\\GPU Engine(*)\\Utilization Percentage", 0, &g_c) != ERROR_SUCCESS) {
            PdhCloseQuery(g_q); g_q = nullptr; g_failed = true; return false;
        }
        g_init = true;
        return true;
    }
}

bool GpuQuery::query(QHash<quint32, double>& out) {
    out.clear();
    if (!initOnce()) return false;
    if (PdhCollectQueryData(g_q) != ERROR_SUCCESS) return false;

    DWORD bufSize = 0, cnt = 0;
    PDH_STATUS st = PdhGetFormattedCounterArrayW(g_c, PDH_FMT_DOUBLE, &bufSize, &cnt, nullptr);
    if (st != PDH_MORE_DATA || cnt == 0) return false;

    QByteArray buf((int)bufSize, 0);
    auto* items = (PDH_FMT_COUNTERVALUE_ITEM_W*)buf.data();
    if (PdhGetFormattedCounterArrayW(g_c, PDH_FMT_DOUBLE, &bufSize, &cnt, items) != ERROR_SUCCESS)
        return false;

    static QRegularExpression pidRe(QStringLiteral("^pid_(\\d+)_"));

    QHash<quint32, QHash<QString, double>> perPid;
    for (DWORD i = 0; i < cnt; ++i) {
        QString name = QString::fromWCharArray(items[i].szName);
        auto m = pidRe.match(name);
        if (!m.hasMatch()) continue;
        quint32 pid = m.captured(1).toUInt();

        int ei = name.indexOf(QStringLiteral("engtype_"));
        QString eng = ei >= 0 ? name.mid(ei + 8) : QStringLiteral("_");

        double v = items[i].FmtValue.doubleValue;
        if (v < 0) v = 0; if (v > 100) v = 100;

        auto& engMap = perPid[pid];
        auto it = engMap.find(eng);
        if (it == engMap.end() || it.value() < v) engMap[eng] = v;
    }

    for (auto it = perPid.constBegin(); it != perPid.constEnd(); ++it) {
        double sum = 0;
        for (double v : it.value()) sum += v;
        if (sum > 100.0) sum = 100.0;
        out[it.key()] = sum;
    }
    return true;
}