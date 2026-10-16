#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QDir>
#include <QMessageBox>

#include "core/Tr.h"
#include "core/Settings.h"
#include "core/Sampler.h"
#include "core/HistoryStore.h"
#include "ui/MainWindow.h"
#include "ui/TrayController.h"

int main(int argc, char** argv) {
	QApplication app(argc, argv);
	app.setApplicationName("SysMonitor");
	app.setOrganizationName("SysMonitor");
	app.setQuitOnLastWindowClosed(false);

	qRegisterMetaType<RankEntry>("RankEntry");
	qRegisterMetaType<QVector<RankEntry>>("QVector<RankEntry>");
	// ---- 单实例 ----
	QLocalSocket sock;
	sock.connectToServer("SysMonitor-Instance");
	if (sock.waitForConnected(300)) {
		sock.write("SHOW");
		sock.waitForBytesWritten(300);
		return 0;
	}
	QLocalServer::removeServer("SysMonitor-Instance");
	QLocalServer server;
	server.listen("SysMonitor-Instance");

	// ---- 设置 ----
	Settings::inst().load();
	Tr::setLang(Settings::inst().language());

	// ---- 历史库 ----
	if (!HistoryStore::inst().open()) {
		QMessageBox::warning(nullptr, "SysMonitor", "无法打开 history.db");
	}

	// ---- 采样线程 ----
	Sampler sampler;
	QObject::connect(&sampler, &Sampler::ranksReady,
		&HistoryStore::inst(), &HistoryStore::enqueue);

	// ---- 主窗口 & 托盘 ----
	MainWindow win;
	TrayController tray(&win, &sampler);

	QObject::connect(&sampler, &Sampler::ranksReady,
		&win, &MainWindow::onRanksReady);

	QObject::connect(&server, &QLocalServer::newConnection, [&] {
		auto* c = server.nextPendingConnection();
		if (c) {
			c->waitForReadyRead(100);
			win.show(); win.raise(); win.activateWindow();
			c->deleteLater();
		}
		});

	sampler.start();
	win.show();

	int rc = app.exec();
	sampler.stop();
	HistoryStore::inst().close();
	return rc;
}