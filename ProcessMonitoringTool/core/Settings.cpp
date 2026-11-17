#include "Settings.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include "AutoStart.h"

Settings& Settings::inst() { static Settings s; return s; }

Settings::Settings() : QObject(nullptr) {
	QString dir = QCoreApplication::applicationDirPath();
	QString path = QDir(dir).filePath("sysmonitor.ini");
	m_qs = new QSettings(path, QSettings::IniFormat, this);
}

void Settings::load() { /* QSettings 已在 ctor 中打开 */ }
void Settings::save() { m_qs->sync(); }

QString Settings::language() const { return m_qs->value("lang", "zh_CN").toString(); }
int     Settings::sampleIntervalMs() const { return qMax(1000, m_qs->value("intervalMs", 5000).toInt()); }
int     Settings::topN() const { return qBound(5, m_qs->value("topN", 10).toInt(), 20); }
int     Settings::retentionHours() const { return qMax(1, m_qs->value("retentionH", 48).toInt()); }
int     Settings::gpuDivisor() const { return qBound(1, m_qs->value("gpuDiv", 2).toInt(), 20); }
bool    Settings::networkEnabled() const { return m_qs->value("netEn", false).toBool(); }
QStringList Settings::filterList() const { return m_qs->value("filter").toStringList(); }
double  Settings::opacity() const { return qBound(0.2, m_qs->value("opacity", 0.80).toDouble(), 1.0); }
double  Settings::scale() const { return qBound(0.5, m_qs->value("scale", 1.00).toDouble(), 2.0); }
QPoint  Settings::windowPos() const { return m_qs->value("pos", QPoint(-1, -1)).toPoint(); }
QSize   Settings::windowSize() const { return m_qs->value("size", QSize(300, 260)).toSize(); }
int     Settings::activeMetric() const { return qBound(0, m_qs->value("metric", 0).toInt(), 4); }
bool    Settings::pinned() const { return m_qs->value("pinned", false).toBool(); }
bool    Settings::autoStart() const { return m_qs->value("autostart", false).toBool(); }

void Settings::setLanguage(const QString& v) { m_qs->setValue("lang", v); emit changed(); }
void Settings::setSampleIntervalMs(int v) { m_qs->setValue("intervalMs", qMax(1000, v)); emit changed(); }
void Settings::setTopN(int v) { m_qs->setValue("topN", qBound(5, v, 20)); emit changed(); }
void Settings::setRetentionHours(int v) { m_qs->setValue("retentionH", qMax(1, v)); emit changed(); }
void Settings::setGpuDivisor(int v) { m_qs->setValue("gpuDiv", qBound(1, v, 20)); emit changed(); }
void Settings::setNetworkEnabled(bool v) { m_qs->setValue("netEn", v); emit changed(); }
void Settings::setFilterList(const QStringList& v) { m_qs->setValue("filter", v); emit changed(); }
void Settings::setOpacity(double v) { m_qs->setValue("opacity", qBound(0.2, v, 1.0)); emit changed(); }
void Settings::setScale(double v) { m_qs->setValue("scale", qBound(0.5, v, 2.0)); emit changed(); }
void Settings::setWindowPos(const QPoint& p) { m_qs->setValue("pos", p); }
void Settings::setWindowSize(const QSize& s) { m_qs->setValue("size", s); }
void Settings::setActiveMetric(int v) { m_qs->setValue("metric", qBound(0, v, 4)); emit changed(); }
void Settings::setPinned(bool v) { m_qs->setValue("pinned", v); emit changed(); }

void Settings::setAutoStart(bool v) {
	m_qs->setValue("autostart", v);
	if (v) {
		QString err;
		if (!AutoStart::enable(&err))
			AutoStart::log(QStringLiteral("ERROR 开启开机自启失败: ") + err);
	}
	else {
		AutoStart::disable();
	}
	emit changed();
}

// 默认保护名单：仅在用户从未修改过时生效
static QStringList defaultProtectedList() {
	return {
		QStringLiteral("System"),
		QStringLiteral("Registry"),
		QStringLiteral("Memory Compression"),
		QStringLiteral("csrss.exe"),
		QStringLiteral("wininit.exe"),
		QStringLiteral("winlogon.exe"),
		QStringLiteral("services.exe"),
		QStringLiteral("lsass.exe"),
		QStringLiteral("smss.exe"),
		QStringLiteral("svchost.exe"),
		QStringLiteral("dwm.exe"),
		QStringLiteral("fontdrvhost.exe"),
	};
}

QStringList Settings::protectedList() const {
	// 用户没动过 → 用默认；动过（哪怕清空）→ 用用户的值
	if (!m_qs->contains("protected"))
		return defaultProtectedList();
	return m_qs->value("protected").toStringList();
}

void Settings::setProtectedList(const QStringList& v) {
	m_qs->setValue("protected", v);
	emit changed();
}