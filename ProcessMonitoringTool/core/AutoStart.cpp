#include "AutoStart.h"
#include "Settings.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QStringConverter>
#include <QStringList>
#include <QTextStream>

#include <windows.h>
#include <string>

const wchar_t* AutoStart::kTaskName = L"SysMonitor-Autostart";
const wchar_t* AutoStart::kRunValue = L"SysMonitor";
static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

namespace {

    QString exePath() {
        return QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
    }

    QString innerCommand() {
        const QString p = exePath();
        if (p.contains(QLatin1Char(' ')) || p.contains(QLatin1Char('\t')))
            return QLatin1Char('"') + p + QLatin1String("\" --autostart");
        return p + QLatin1String(" --autostart");
    }

    QString taskName() { return QString::fromWCharArray(AutoStart::kTaskName); }

    // schtasks 的输出在中文系统上是 GBK，错误信息用 fromLocal8Bit 解
    bool runSchtasks(const QStringList& args, QByteArray* raw, QString* err) {
        QProcess p;
        p.setProgram(QStringLiteral("schtasks.exe"));
        p.setArguments(args);
        p.start();
        if (!p.waitForStarted(3000) || !p.waitForFinished(10000)) {
            p.kill();
            p.waitForFinished(1000);
            if (err) *err = QStringLiteral("schtasks 无法执行或超时");
            return false;
        }
        const QByteArray out = p.readAllStandardOutput();
        if (raw) *raw = out;
        if (err) {
            *err = QString::fromLocal8Bit(p.readAllStandardError()).trimmed();
            if (err->isEmpty() && p.exitCode() != 0)
                *err = QStringLiteral("schtasks 退出码 %1").arg(p.exitCode());
        }
        return p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0;
    }

    bool taskExists() {
        return runSchtasks({ QStringLiteral("/Query"), QStringLiteral("/TN"), taskName() },
            nullptr, nullptr);
    }

    // 任务计划导出的 XML 是 UTF-16，直接按宽字节序列比对，避免踩编码问题
    bool taskPointsHere() {
        QByteArray raw;
        if (!runSchtasks({ QStringLiteral("/Query"), QStringLiteral("/TN"), taskName(),
                          QStringLiteral("/XML") }, &raw, nullptr))
            return false;

        const QString p = exePath();
        QByteArray u16;
        u16.reserve(p.size() * 2);
        for (const QChar c : p) {
            u16.append(static_cast<char>(c.unicode() & 0xFF));
            u16.append(static_cast<char>(c.unicode() >> 8));
        }
        if (raw.contains(u16)) return true;
        return QString::fromLocal8Bit(raw).contains(p, Qt::CaseInsensitive);
    }

    bool runValueExists() {
        HKEY k = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) != ERROR_SUCCESS)
            return false;
        const LONG r = RegQueryValueExW(k, AutoStart::kRunValue, nullptr, nullptr, nullptr, nullptr);
        RegCloseKey(k);
        return r == ERROR_SUCCESS;
    }

    QString runValueText() {
        QString v;
        HKEY k = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
            wchar_t buf[2048] = {};
            DWORD cb = sizeof(buf) - sizeof(wchar_t);
            DWORD type = 0;
            if (RegQueryValueExW(k, AutoStart::kRunValue, nullptr, &type,
                reinterpret_cast<LPBYTE>(buf), &cb) == ERROR_SUCCESS
                && type == REG_SZ) {
                v = QString::fromWCharArray(buf);
            }
            RegCloseKey(k);
        }
        return v;
    }

    bool writeRunValue() {
        HKEY k = nullptr;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE,
            nullptr, &k, nullptr) != ERROR_SUCCESS)
            return false;
        const std::wstring w = innerCommand().toStdWString();   // 带引号 + --autostart
        const LONG r = RegSetValueExW(k, AutoStart::kRunValue, 0, REG_SZ,
            reinterpret_cast<const BYTE*>(w.c_str()),
            static_cast<DWORD>((w.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(k);
        return r == ERROR_SUCCESS;
    }

    void deleteRunValue() {
        HKEY k = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
            RegDeleteValueW(k, AutoStart::kRunValue);
            RegCloseKey(k);
        }
    }

}  // namespace

QString AutoStart::logPath() {
    static QString cached;
    if (!cached.isEmpty()) return cached;

    const QString p =
        QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("startup.log"));
    QFile f(p);
    if (f.open(QIODevice::WriteOnly | QIODevice::Append)) {
        f.close();
        cached = p;
        return cached;
    }
    // 装到 Program Files 之类没写权限的目录时，退到 LocalAppData
    const QString alt = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(alt);
    cached = QDir(alt).filePath(QStringLiteral("startup.log"));
    return cached;
}

void AutoStart::log(const QString& msg) {
    QFile f(logPath());
    if (f.size() > 512 * 1024) f.remove();      // 简单滚动，别让它无限长大
    if (!f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        return;
    QTextStream ts(&f);
    ts.setEncoding(QStringConverter::Utf8);
    ts << QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"))
        << QStringLiteral("  [")
        << QString::number(static_cast<qlonglong>(QCoreApplication::applicationPid()))
        << QStringLiteral("] ") << msg << QLatin1Char('\n');
    f.close();
}

bool AutoStart::isEnabled() {
    return taskExists() || runValueExists();
}

bool AutoStart::enable(QString* err) {
    QString taskErr;
    const bool taskOk =
        runSchtasks({ QStringLiteral("/Create"), QStringLiteral("/F"),
                     QStringLiteral("/TN"), taskName(),
                     QStringLiteral("/TR"), innerCommand(),
                     QStringLiteral("/SC"), QStringLiteral("ONLOGON"),
                     QStringLiteral("/RL"), QStringLiteral("HIGHEST") },
            nullptr, &taskErr)
        && taskExists();

    bool runOk = true;
    if (taskOk) {
        // 任务建好了，就把老版本留下的 Run 项清掉，免得登录时拉起两次
        if (runValueExists()) deleteRunValue();
    }
    else {
        // 兜底：写 Run 项（带引号）。它不一定能绕开提权限制，但至少路径是对的。
        runOk = writeRunValue();
    }

    QString detail;
    if (!taskOk) detail += QStringLiteral("计划任务失败: ") + taskErr;
    if (!runOk)
        detail += (detail.isEmpty() ? QString() : QStringLiteral(" / "))
        + QStringLiteral("Run 项写入失败");
    if (err) *err = detail;

    log(QStringLiteral("enable: 计划任务=%1 Run项=%2 %3")
        .arg(taskOk ? QStringLiteral("OK") : QStringLiteral("FAIL"),
            runOk ? QStringLiteral("OK") : QStringLiteral("FAIL"),
            detail));
    return taskOk || runOk;
}

void AutoStart::disable() {
    QString e;
    runSchtasks({ QStringLiteral("/Delete"), QStringLiteral("/F"),
                 QStringLiteral("/TN"), taskName() }, nullptr, &e);
    deleteRunValue();
    log(QStringLiteral("disable: 已删除计划任务和 Run 项 %1").arg(e));
}

bool AutoStart::sync() {
    if (Settings::inst().autoStart()) {
        if (taskPointsHere()) return true;      // 已经指向当前 exe，不动它
        if (!taskExists() && runValueText().contains(exePath(), Qt::CaseInsensitive))
            return true;
        return enable();                        // 搬家 / 换构建目录 → 重建
    }
    disable();
    return true;
}

QString AutoStart::describe() {
    return QStringLiteral("exe=%1 | 计划任务=%2 | Run项=%3")
        .arg(exePath(),
            taskExists() ? QStringLiteral("有") : QStringLiteral("无"),
            runValueExists() ? runValueText() : QStringLiteral("无"));
}