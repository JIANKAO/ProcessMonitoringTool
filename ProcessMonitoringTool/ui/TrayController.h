#pragma once
#include <QObject>
#include <QMenu>
#include <QSystemTrayIcon>

class MainWindow;
class Sampler;
class StatsWindow;

class TrayController : public QObject {
	Q_OBJECT
public:
	TrayController(MainWindow* win, Sampler* sampler, QObject* parent = nullptr);
	~TrayController();

private slots:
	void onTrayActivated(QSystemTrayIcon::ActivationReason r);
	void openSettings();
	void openStats();
	void togglePin();
	void setOpacity();
	void setScale();

private:
	void rebuildMenu();

	MainWindow* m_win = nullptr;
	Sampler* m_sampler = nullptr;
	QSystemTrayIcon* m_tray = nullptr;
	QMenu* m_menu = nullptr;
	StatsWindow* m_stats = nullptr;
};