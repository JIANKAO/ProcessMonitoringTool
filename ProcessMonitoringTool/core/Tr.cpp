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
		{"set.language",    {"语言(restart)",		 "Language(重启)"}},
		{"set.interval",    {"采样间隔(秒)",         "Sample interval (s)"}},
		{"set.topn",        {"榜单条数",             "Top N"}},
		{"set.retention",   {"保留时长(小时)",       "Retention (hours)"}},
		{"set.gpuMul",      {"GPU 采样倍数",         "GPU sample divisor"}},
		{"set.enableNet",   {"启用网络统计(ETW)",    "Enable network (ETW)"}},
		{"set.filter",      {"过滤进程\n(每行一个关键字)", "Filter\n(one per line)"}},
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

		{"menu.restart",      {"重启进程",              "Restart process"}},
		{"menu.locate",       {"打开文件所在位置",      "Open file location"}},
		{"menu.copyPath",     {"复制路径",              "Copy path"}},
		{"menu.target",       {"进程：%1",              "Process: %1"}},
		{"restart.confirm",   {"确定要重启「%1」吗？\n\n未保存的数据可能丢失。", "Restart \"%1\"?\n\nUnsaved data may be lost."}},
		{"restart.fail",      {"重启失败",              "Restart failed"}},
		{"restart.noPath",    {"无法获取该进程的可执行文件路径。","Cannot get executable path."}},
		{"restart.protected", {"「%1」是系统关键进程，不允许重启。","\"%1\" is a protected system process."}},
		{"restart.self",      {"不能重启本程序自身。",  "Cannot restart self."}},
		{"restart.notFound",  {"未找到「%1」的任何运行实例。","No running instance of \"%1\" found."}},
		{"restart.cantKill",  {"无法终止「%1」的进程实例（可能受系统保护）。","Failed to terminate \"%1\" (may be protected)."}},
		{"restart.spawnFail", {"启动「%1」失败。\n系统错误码：%2","Failed to start \"%1\".\nError code: %2"}},
		{"restart.alreadyClosed", {"「%1」已关闭，重启失败。","\"%1\" has already exited, restart failed."}},
		{"set.protected",    {"重启保护名单\n(每行一个进程名)\n(不区分大小写)", "Restart-protected processes\n(one per line)\n(case-insensitive)"}},
		{"set.protectedTip", {"留空表示不保护任何进程。系统关键进程即使在此名单之外，也可能因系统自身保护而无法终止。", "Empty = no protection. Critical system processes may still fail to terminate due to OS-level protection."}},
	};
	return t;
}

QString Tr::s(const char* key) {
	auto it = table().constFind(QString::fromUtf8(key));
	if (it == table().constEnd()) return QString::fromUtf8(key);
	return g_lang == "en_US" ? it->second : it->first;
}