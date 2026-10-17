#include "MainWindow.h"
#include "core/Settings.h"
#include "core/Tr.h"

#include <QPainter>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QMenu>
#include <QApplication>
#include <QScreen>
#include <QPainterPath>
#include <windows.h>

MainWindow::MainWindow(QWidget* parent) : QWidget(parent) {
	setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
	setAttribute(Qt::WA_TranslucentBackground);
	setMouseTracking(true);
	setWindowTitle(Tr::s("app.title"));

	applySettings();
	m_metric = (MetricType)Settings::inst().activeMetric();

	QPoint pos = Settings::inst().windowPos();
	if (pos.x() < 0 || pos.y() < 0) {
		auto* sc = QApplication::primaryScreen();
		QRect r = sc->availableGeometry();
		pos = QPoint(r.right() - width() - 20, r.top() + 40);
	}
	move(pos);

	if (Settings::inst().pinned()) setPinned(true);

	setWindowIcon(QApplication::windowIcon());
}

MainWindow::~MainWindow() = default;

void MainWindow::applySettings() {
	m_opacity = Settings::inst().opacity();
	m_scale = Settings::inst().scale();
	resize(baseSize());
	update();
}

QSize MainWindow::baseSize() const {
	int rows = m_entries[(int)m_metric].size();
	if (rows < 3) rows = 3;
	int w = qRound(300 * m_scale);
	int h = qRound((30 + rows * 22 + 12) * m_scale);
	return QSize(w, h);
}

QRectF MainWindow::tabRect(int i) const {
	int w = width() / 5;
	return QRectF(i * w, 0, w, 26 * m_scale);
}

void MainWindow::setMetric(MetricType m) {
	m_metric = m;
	Settings::inst().setActiveMetric((int)m);
	resize(baseSize());
	update();
}

void MainWindow::setPinned(bool on) {
	m_pinned = on;
	Settings::inst().setPinned(on);

	HWND hwnd = (HWND)winId();
	LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
	if (on) ex |= WS_EX_TRANSPARENT;
	else    ex &= ~WS_EX_TRANSPARENT;
	SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);

	update();
}

void MainWindow::onRanksReady(int metric, QVector<RankEntry> entries, qint64) {
	if (metric < 0 || metric >= (int)MetricType::Count) return;
	m_entries[metric] = std::move(entries);
	if (metric == (int)m_metric) {
		QSize sz = baseSize();
		if (size() != sz) resize(sz);
		update();
	}
}

void MainWindow::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing, true);
	p.setRenderHint(QPainter::TextAntialiasing, true);

	// 背景
	QColor bg(18, 22, 30);
	bg.setAlphaF(m_opacity);
	QPainterPath path;
	path.addRoundedRect(QRectF(rect()), 8 * m_scale, 8 * m_scale);
	p.fillPath(path, bg);

	// 顶部 tab
	QFont f = font();
	f.setPointSizeF(9.5 * m_scale);
	p.setFont(f);

	static const MetricType kAll[] = { MetricType::CPU, MetricType::Memory, MetricType::Disk, MetricType::Network, MetricType::GPU };

	for (int i = 0; i < 5; ++i) {
		QRectF r = tabRect(i);
		bool active = (kAll[i] == m_metric);
		if (active) {
			QColor c(90, 150, 240, 90);
			p.fillRect(r, c);
		}
		p.setPen(active ? QColor(255, 255, 255, 255) : QColor(200, 200, 210, 220));
		p.drawText(r, Qt::AlignCenter, Tr::s(metricKey(kAll[i])));
	}

	// 列表
	const auto& list = m_entries[(int)m_metric];
	QFont f2 = font();
	f2.setPointSizeF(9 * m_scale);
	p.setFont(f2);

	int rowH = qRound(22 * m_scale);
	int top = qRound(30 * m_scale);
	int padX = qRound(8 * m_scale);

	int shown = qMin(list.size(), Settings::inst().topN());
	for (int i = 0; i < shown; ++i) {
		const auto& e = list[i];
		const bool stale = e.stale;
		QRectF r(0, top + i * rowH, width(), rowH);

		// 斑马纹：stale 行用更弱的对比
		if (i % 2 == 0) {
			QColor alt = stale ? QColor(255, 255, 255, 6) : QColor(255, 255, 255, 12);
			p.fillRect(r, alt);
		}

		// 排名
		p.setPen(stale ? QColor(130, 150, 180, 180) : QColor(160, 200, 255, 230));
		p.drawText(QRectF(padX, r.top(), 24 * m_scale, r.height()), Qt::AlignVCenter | Qt::AlignLeft, QString::number(i + 1));

		// 名称
		QString name = e.name;
		if (name.isEmpty()) name = "?";
		if (e.procCount > 1)
			name += QStringLiteral(" (%1)").arg(e.procCount);   // ← 新增
		p.setPen(stale ? QColor(165, 165, 165, 220) : QColor(240, 240, 240, 255));
		double nameW = r.width() - 24 * m_scale - 120 * m_scale - padX;
		QString elided = p.fontMetrics().elidedText(name, Qt::ElideMiddle, qMax(10.0, nameW));
		p.drawText(QRectF(padX + 24 * m_scale, r.top(), nameW, r.height()), Qt::AlignVCenter | Qt::AlignLeft, elided);

		// 数值（格式逻辑不变）
		QString valStr;
		switch (m_metric) {
		case MetricType::CPU:
		case MetricType::GPU:
			valStr = QString::number(e.value, 'f', 1) + "%";
			break;
		case MetricType::Memory: {
			double mb = e.value / 1024.0 / 1024.0;
			valStr = mb >= 1024 ? QString::number(mb / 1024, 'f', 2) + " GB" : QString::number(mb, 'f', 0) + " MB";
			break;
		}
		case MetricType::Disk:
		case MetricType::Network: {
			double kb = e.value / 1024.0;
			valStr = kb >= 1024 ? QString::number(kb / 1024, 'f', 2) + " MB/s" : QString::number(kb, 'f', 1) + " KB/s";
			break;
		}
		default: break;
		}
		p.setPen(stale ? QColor(140, 170, 150, 200) : QColor(120, 230, 170, 255));
		p.drawText(QRectF(width() - 120 * m_scale, r.top(), 70 * m_scale, r.height()), Qt::AlignVCenter | Qt::AlignRight, valStr);

		// 持续时长（stale 的时长已在 Sampler 里冻结）
		qint64 ms = e.durationMs;
		QString dur;
		qint64 s = ms / 1000;
		if (s < 60) dur = QString::number(s) + "s";
		else if (s < 3600) dur = QString("%1m%2s").arg(s / 60).arg(s % 60);
		else dur = QString("%1h%2m").arg(s / 3600).arg((s % 3600) / 60);
		if (stale) dur += " ·";   // 小标记，加在末尾

		p.setPen(stale ? QColor(150, 150, 150, 180) : QColor(200, 200, 200, 210));
		p.drawText(QRectF(width() - 50 * m_scale, r.top(), 46 * m_scale, r.height()), Qt::AlignVCenter | Qt::AlignRight, dur);
	}
}

void MainWindow::mousePressEvent(QMouseEvent* e) {
	if (e->button() == Qt::LeftButton) {
		for (int i = 0; i < 5; ++i) {
			if (tabRect(i).contains(e->pos())) {
				static const MetricType kAll[] = { MetricType::CPU, MetricType::Memory,	MetricType::Disk, MetricType::Network, MetricType::GPU };
				setMetric(kAll[i]);
				return;
			}
		}
		m_dragging = true;
		// Qt 6：globalPosition() 返回 QPointF，转成 QPoint 用
		m_dragOffset = e->globalPosition().toPoint() - frameGeometry().topLeft();
	}
}

void MainWindow::mouseMoveEvent(QMouseEvent* e) {
	if (m_dragging && (e->buttons() & Qt::LeftButton)) {
		move(e->globalPosition().toPoint() - m_dragOffset);
	}
}

void MainWindow::mouseReleaseEvent(QMouseEvent*) {
	if (m_dragging) {
		m_dragging = false;
		Settings::inst().setWindowPos(pos());
	}
}

void MainWindow::contextMenuEvent(QContextMenuEvent* e) {
	if (m_pinned) return; // 钉住时忽略

	QMenu menu;
	menu.addAction(Tr::s("menu.stats"), this, &MainWindow::requestStats);
	menu.addAction(Tr::s("menu.settings"), this, &MainWindow::requestSettings);
	menu.addSeparator();
	menu.addAction(Tr::s("menu.pin"), this, [this] { setPinned(true); });
	menu.addAction(Tr::s("menu.hide"), this, &MainWindow::requestHide);
	menu.addSeparator();
	menu.addAction(Tr::s("menu.quit"), this, &MainWindow::requestQuit);
	menu.exec(e->globalPos());
}

void MainWindow::closeEvent(QCloseEvent* e) {
	Settings::inst().setWindowPos(pos());
	e->ignore();
	emit requestHide();
}