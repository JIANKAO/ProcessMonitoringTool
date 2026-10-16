#include "Tr.h"
#include <QHash>

static QString g_lang = "zh_CN";

QString Tr::lang() { return g_lang; }
void Tr::setLang(const QString& c) { g_lang = c; }

static const QHash<QString, QPair<QString, QString>>& table() {
    static const QHash<QString, QPair<QString, QString>> t = {
        {"app.title",       {"系统监视器",           "System Monitor"}},
        {"metric.cpu",      {"CPU",                  "CPU"}},
        {"metric.mem",      {"内存",                 "Memory"}},
        {"metric.disk",     {"磁盘",                 "Disk"}},
        {"metric.net",      {"网络",                 "Network"}},
        {"metric.gpu",      {"GPU",                  "GPU"}},

        {"menu.tray",       {"托盘",                 "Tray"}},
        {"menu.pin",        {"钉在桌面(穿透)",       "Pin (click-through)"}},
        {"menu.unpin",      {"解锁(取消穿透)",       "Unpin"}},
        {"menu.scale",      {"缩放窗口...",          "Scale window..."}},
        {"menu.opacity",    {"透明度...",            "Opacity..."}},
        {"menu.settings",   {"设置...",              "Settings..."}},
        {"menu.stats",      {"历史统计...",          "History stats..."}},
        {"menu.hide",       {"隐藏到托盘",           "Hide to tray"}},
        {"menu.quit",       {"退出",                 "Quit"}},

        {"set.title",       {"设置",                 "Settings"}},
        {"set.language",    {"语言",                 "Language"}},
        {"set.interval",    {"采样间隔(秒)",         "Sample interval (s)"}},
        {"set.topn",        {"榜单条数",             "Top N"}},
        {"set.retention",   {"保留时长(小时)",       "Retention (hours)"}},
        {"set.gpuMul",      {"GPU 采样倍数",         "GPU sample divisor"}},
        {"set.enableNet",   {"启用网络统计(ETW)",    "Enable network (ETW)"}},
        {"set.filter",      {"过滤进程(每行一个关键字)", "Filter (one per line)"}},
        {"set.opacity",     {"透明度(%)",            "Opacity (%)"}},
        {"set.scale",       {"缩放(%)",              "Scale (%)"}},
        {"set.autoStart",   {"开机自启",             "Auto-start"}},
        {"set.ok",          {"确定",                 "OK"}},
        {"set.cancel",      {"取消",                 "Cancel"}},

        {"stats.title",     {"历史统计",             "History stats"}},
        {"stats.from",      {"起始",                 "From"}},
        {"stats.to",        {"结束",                 "To"}},
        {"stats.load",      {"加载",                 "Load"}},
        {"stats.quick1h",   {"最近1h",               "Last 1h"}},
        {"stats.quick6h",   {"最近6h",               "Last 6h"}},
        {"stats.quick24h",  {"最近24h",              "Last 24h"}},
        {"stats.quickAll",  {"全部",                 "All"}},
        {"stats.loading",   {"加载中 %1%",           "Loading %1%"}},
        {"stats.empty",     {"无数据",               "No data"}},
    };
    return t;
}

QString Tr::s(const char* key) {
    auto it = table().constFind(QString::fromUtf8(key));
    if (it == table().constEnd()) return QString::fromUtf8(key);
    return g_lang == "en_US" ? it->second : it->first;
}