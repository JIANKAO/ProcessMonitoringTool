#include "TrayController.h"
#include "MainWindow.h"
#include "SettingsDialog.h"
#include "StatsWindow.h"
#include "core/Settings.h"
#include "core/Tr.h"

#include <QApplication>
#include <QInputDialog>
#include <QIcon>
#include <QPainter>
#include <QPixmap>

static QIcon makeTrayIcon() {
	QPixmap pm(32, 32);
	pm.fill(Qt::transparent);
	QPainter p(&pm);
	p.setRenderHint(QPainter::Antialiasing);
	p.setBrush(QColor(70, 130, 220));
	p.setPen(Qt::NoPen);
	p.drawRoundedRect(2, 2, 28, 28, 6, 6);
	p.setPen(Qt::white);
	QFont f = p.font(); f.setBold(true); f.setPointSize(12);
	p.setFont(f);
	p.drawText(pm.rect(), Qt::AlignCenter, "S");
	return QIcon(pm);
}

TrayController::TrayController(MainWindow* win, Sampler* sampler, QObject* parent)
	: QObject(parent), m_win(win), m_sampler(sampler) {

	m_tray = new QSystemTrayIcon(makeTrayIcon(), this);
	m_tray->setToolTip(Tr::s("app.title"));
	m_menu = new QMenu;
	m_tray->setContextMenu(m_menu);

	connect(m_tray, &QSystemTrayIcon::activated, this, &TrayController::onTrayActivated);

	connect(m_win, &MainWindow::requestSettings, this, &TrayController::openSettings);
	connect(m_win, &MainWindow::requestStats, this, &TrayController::openStats);
	connect(m_win, &MainWindow::requestHide, m_win, &QWidget::hide);
	connect(m_win, &MainWindow::requestQuit, qApp, &QApplication::quit);

	rebuildMenu();
	m_tray->show();
}

TrayController::~TrayController() {
	delete m_menu;
}

void TrayController::rebuildMenu() {
	m_menu->clear();

	m_menu->addAction(Tr::s("menu.stats"), this, &TrayController::openStats);
	m_menu->addAction(Tr::s("menu.settings"), this, &TrayController::openSettings);
	m_menu->addSeparator();

	// 显示/隐藏
	m_menu->addAction(Tr::s("menu.hide"), this, [this] {
		if (m_win->isVisible()) m_win->hide();
		else { m_win->show(); m_win->raise(); }
		});

	// 钉住 / 解锁
	if (m_win->isPinned()) {
		m_menu->addAction(Tr::s("menu.unpin"), this, &TrayController::togglePin);
	}
	else {
		m_menu->addAction(Tr::s("menu.pin"), this, &TrayController::togglePin);
	}

	m_menu->addAction(Tr::s("menu.opacity"), this, &TrayController::setOpacity);
	m_menu->addAction(Tr::s("menu.scale"), this, &TrayController::setScale);

	m_menu->addSeparator();
	m_menu->addAction(Tr::s("menu.quit"), qApp, &QApplication::quit);
}

void TrayController::onTrayActivated(QSystemTrayIcon::ActivationReason r) {
	if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) {
		if (m_win->isVisible()) {
			m_win->raise();
			m_win->activateWindow();
		}
		else {
			m_win->show();
			m_win->raise();
		}
	}
	rebuildMenu();
}

void TrayController::togglePin() {
	m_win->setPinned(!m_win->isPinned());
	rebuildMenu();
}

void TrayController::openSettings() {
	SettingsDialog dlg(m_win);
	if (dlg.exec() == QDialog::Accepted) {
		m_win->applySettings();
		rebuildMenu();
	}
}

void TrayController::openStats() {
	if (!m_stats) {
		m_stats = new StatsWindow;
		m_stats->setAttribute(Qt::WA_DeleteOnClose);
		connect(m_stats, &QObject::destroyed, this, [this] { m_stats = nullptr; });
	}
	m_stats->show();
	m_stats->raise();
	m_stats->activateWindow();
}

void TrayController::setOpacity() {
	bool ok = false;
	int v = QInputDialog::getInt(m_win, Tr::s("menu.opacity"), Tr::s("set.opacity"),
		qRound(Settings::inst().opacity() * 100), 20, 100, 5, &ok);
	if (ok) {
		Settings::inst().setOpacity(v / 100.0);
		m_win->applySettings();
	}
}

void TrayController::setScale() {
	bool ok = false;
	int v = QInputDialog::getInt(m_win, Tr::s("menu.scale"), Tr::s("set.scale"),
		qRound(Settings::inst().scale() * 100), 50, 200, 5, &ok);
	if (ok) {
		Settings::inst().setScale(v / 100.0);
		m_win->applySettings();
	}
}