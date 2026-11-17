#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QDir>
#include <QMessageBox>
#include <QTimer>
#include <QElapsedTimer>
#include <QSystemTrayIcon>
#include <memory>

#include "core/Tr.h"
#include "core/Settings.h"
#include "core/Sampler.h"
#include "core/HistoryStore.h"
#include "core/AutoStart.h"
#include "ui/MainWindow.h"
#include "ui/TrayController.h"

#include <windows.h>

// 登录自启常常赶在 explorer 前面，这时候建托盘图标会失败，而且失败后不会自动重试，
// 结果就是"进程起来了但看不见"，被当成"打不开"。
static bool shellReady() {
	if (!QSystemTrayIcon::isSystemTrayAvailable()) return false;
	return FindWindowW(L"Shell_TrayWnd", nullptr) != nullptr;
}

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	app.setApplicationName("SysMonitor");
	app.setOrganizationName("SysMonitor");
	app.setQuitOnLastWindowClosed(false);
	app.setWindowIcon(QIcon(":/ico/ui/ico/headImage.png"));

	qRegisterMetaType<RankEntry>("RankEntry");
	qRegisterMetaType<QVector<RankEntry>>("QVector<RankEntry>");

	const bool fromBoot = app.arguments().contains("--autostart");

	// ---- 单实例 ----
	QLocalSocket sock;
	sock.connectToServer("SysMonitor-Instance");
	if (sock.waitForConnected(500)) {
		sock.write("SHOW");
		sock.waitForBytesWritten(500);
		return 0;
	}
	QLocalServer::removeServer("SysMonitor-Instance");
	QLocalServer server;
	if (!server.listen("SysMonitor-Instance")) {
		// 可能是另一个实例刚好抢先，再确认一次
		QLocalSocket s2;
		s2.connectToServer("SysMonitor-Instance");
		if (s2.waitForConnected(500)) {
			s2.write("SHOW");
			s2.waitForBytesWritten(500);
			return 0;
		}
		AutoStart::log(QStringLiteral("WARN 单实例监听失败: ") + server.errorString());
	}

	// ---- 设置 ----
	Settings::inst().load();
	Tr::setLang(Settings::inst().language());

	AutoStart::log(QStringLiteral("启动 fromBoot=%1 exe=%2 cwd=%3")
		.arg(fromBoot ? QStringLiteral("yes") : QStringLiteral("no"),
			QDir::toNativeSeparators(QCoreApplication::applicationFilePath()),
			QDir::toNativeSeparators(QDir::currentPath())));

	// 自启项自愈：搬家、从 Debug 换到 Release 之后，把登记项重新指回当前 exe
	if (Settings::inst().autoStart() || AutoStart::isEnabled()) {
		if (!AutoStart::sync())
			AutoStart::log(QStringLiteral("WARN 自启项同步失败: ") + AutoStart::describe());
	}

	// ---- 历史库 ----
	const bool dbOk = HistoryStore::inst().open();
	if (!dbOk) AutoStart::log(QStringLiteral("ERROR history.db 打开失败"));

	// ---- 采样线程 ----
	Sampler sampler;
	QObject::connect(&sampler, &Sampler::ranksReady,
		&HistoryStore::inst(), &HistoryStore::enqueue);

	// ---- 主窗口 & 托盘 ----
	MainWindow win;
	TrayController tray(&win, &sampler);
	QObject::connect(&sampler, &Sampler::ranksReady,
		&win, &MainWindow::onRanksReady);

	auto showWindow = [&win] {
		win.show();
		win.raise();
		win.activateWindow();
		};

	QObject::connect(&server, &QLocalServer::newConnection, [&] {
		auto* c = server.nextPendingConnection();
		if (c) {
			c->waitForReadyRead(100);
			showWindow();
			c->deleteLater();
		}
		});

	// 开机时没人看得见模态框：数据库失败写日志，非开机状态再弹框
	if (!dbOk) {
		AutoStart::log(QStringLiteral("ERROR history.db 打开失败"));
		if (!fromBoot)
			QMessageBox::warning(nullptr, "SysMonitor", "无法打开 history.db");
	}

	auto startUp = [&] {
		sampler.start();
		showWindow();
		};

	if (fromBoot) {
		// 等资源管理器就绪再显示；最多 30 秒，超时也照常启动
		auto clock = std::make_shared<QElapsedTimer>();
		clock->start();
		auto* t = new QTimer(&app);
		t->setInterval(500);
		QObject::connect(t, &QTimer::timeout, &app, [&, t, clock] {
			if (!shellReady() && clock->elapsed() < 30000) return;
			t->stop();
			AutoStart::log(QStringLiteral("shell %1，等待 %2 ms 后显示界面")
				.arg(shellReady() ? QStringLiteral("就绪") : QStringLiteral("超时"))
				.arg(clock->elapsed()));
			startUp();
			});
		t->start();
	}
	else {
		startUp();
	}

	const int rc = app.exec();
	sampler.stop();
	HistoryStore::inst().close();
	return rc;
}