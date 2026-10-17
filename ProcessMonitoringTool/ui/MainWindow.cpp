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

#include <QMessageBox>
#include <QClipboard>
#include <QDesktopServices>
#include <QUrl>
#include <QCoreApplication>
#include <QFileInfo>
#include "core/ProcessQuery.h"

namespace {
	// 按可执行文件路径收集当前所有运行实例的 pid
	QVector<quint32> collectPidsByPath(const QString& path) {
		QVector<quint32> pids;
		if (path.isEmpty()) return pids;
		QVector<RawProc> raws;
		if (!ProcessQuery::enumAll(raws)) return pids;
		for (const auto& r : raws) {
			if (ProcessQuery::resolvePath(r.pid, r.createTime)
				.compare(path, Qt::CaseInsensitive) == 0)
				pids.push_back(r.pid);
		}
		return pids;
	}
}

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

int MainWindow::entryIndexAt(const QPoint& pos) const {
	int rowH = qRound(22 * m_scale);
	int top = qRound(30 * m_scale);
	if (pos.y() < top) return -1;
	int idx = (pos.y() - top) / rowH;
	const auto& list = m_entries[(int)m_metric];
	if (idx >= 0 && idx < list.size()) return idx;
	return -1;
}

void MainWindow::beginFreeze() {
	m_freezeUpdates = true;
}

void MainWindow::endFreeze() {
	m_freezeUpdates = false;
	for (int i = 0; i < (int)MetricType::Count; ++i) {
		if (m_pendingDirty[i]) {
			m_entries[i] = std::move(m_pendingEntries[i]);
			m_pendingEntries[i].clear();
			m_pendingDirty[i] = false;
		}
	}
	QSize sz = baseSize();
	if (size() != sz) resize(sz);
	update();
}

bool MainWindow::restartProcess(const RankEntry& e) {
	// ---- 前置校验 ----
	if (e.path.isEmpty()) {
		QMessageBox::warning(this, Tr::s("restart.fail"), Tr::s("restart.noPath"));
		return false;
	}

	// ---- 从设置读保护名单（每次实时读，用户改完立刻生效） ----
	const QStringList protectedList = Settings::inst().protectedList();
	for (const auto& name : protectedList) {
		if (!name.isEmpty() && e.name.compare(name, Qt::CaseInsensitive) == 0) {
			QMessageBox::warning(this, Tr::s("restart.fail"),
				Tr::s("restart.protected").arg(e.name));
			return false;
		}
	}

	// ---- 自身保护（不可关） ----
	if (e.path.compare(QCoreApplication::applicationFilePath(),
		Qt::CaseInsensitive) == 0) {
		QMessageBox::warning(this, Tr::s("restart.fail"), Tr::s("restart.self"));
		return false;
	}

	// ---- 按路径收集所有实例 ----
	QVector<quint32> pids = collectPidsByPath(e.path);
	if (pids.isEmpty()) {
		QMessageBox::warning(
			this, Tr::s("restart.fail"),
			Tr::s("restart.alreadyClosed").arg(e.name));
		return false;
	}

	// ---- 终止 ----
	int killed = 0;
	for (quint32 pid : pids) {
		HANDLE h = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pid);
		if (!h) continue;
		if (TerminateProcess(h, 0)) {
			WaitForSingleObject(h, 3000);
			++killed;
		}
		CloseHandle(h);
	}
	if (killed == 0) {
		QMessageBox::warning(this, Tr::s("restart.fail"),
			Tr::s("restart.cantKill").arg(e.name));
		return false;
	}

	Sleep(500);   // 释放句柄、端口、单实例锁

	// ---- 重新启动 ----
	std::wstring wpath = e.path.toStdWString();
	std::wstring wdir = QFileInfo(e.path).absolutePath().toStdWString();
	std::wstring cmd = L"\"" + wpath + L"\"";

	STARTUPINFOW si = { sizeof(si) };
	PROCESS_INFORMATION pi = {};
	BOOL ok = CreateProcessW(
		wpath.c_str(), cmd.data(),
		nullptr, nullptr, FALSE, 0, nullptr,
		wdir.c_str(), &si, &pi);

	if (!ok) {
		DWORD err = GetLastError();
		QString detail;
		LPWSTR buf = nullptr;
		if (FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM
			| FORMAT_MESSAGE_IGNORE_INSERTS,
			nullptr, err, 0, (LPWSTR)&buf, 0, nullptr) && buf) {
			detail = QString::fromWCharArray(buf).trimmed();
			LocalFree(buf);
		}
		QMessageBox::warning(this, Tr::s("restart.fail"),
			Tr::s("restart.spawnFail").arg(e.name)
			.arg(detail.isEmpty() ? QString::number(err)
				: QStringLiteral("%1 (%2)").arg(err).arg(detail)));
		return false;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
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

	if (m_freezeUpdates) {
		m_pendingEntries[metric] = std::move(entries);
		m_pendingDirty[metric] = true;
		return;
	}

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
	if (m_pinned) return;

	// ---- 捕获目标行（菜单关闭后仍然有效） ----
	int idx = entryIndexAt(e->pos());
	RankEntry target;
	bool hasTarget = false;
	if (idx >= 0) {
		const auto& list = m_entries[(int)m_metric];
		if (idx < list.size()) {
			target = list[idx];
			hasTarget = true;
		}
	}

	beginFreeze();

	QMenu menu;
	QAction* actRestart = nullptr;
	QAction* actLocate = nullptr;
	QAction* actCopyPath = nullptr;

	if (hasTarget) {
		QAction* title = menu.addAction(Tr::s("menu.target").arg(target.name));
		title->setEnabled(false);
		menu.addSeparator();
		if (!target.stale && !target.path.isEmpty())
			actRestart = menu.addAction(Tr::s("menu.restart"));
		actLocate = menu.addAction(Tr::s("menu.locate"));
		actCopyPath = menu.addAction(Tr::s("menu.copyPath"));
		menu.addSeparator();
	}

	QAction* actStats = menu.addAction(Tr::s("menu.stats"));
	QAction* actSettings = menu.addAction(Tr::s("menu.settings"));
	menu.addSeparator();
	QAction* actPin = menu.addAction(Tr::s("menu.pin"));
	QAction* actHide = menu.addAction(Tr::s("menu.hide"));
	menu.addSeparator();
	QAction* actQuit = menu.addAction(Tr::s("menu.quit"));

	QAction* chosen = menu.exec(e->globalPos());
	endFreeze();

	if (!chosen) return;

	// ---- 分发 ----
	if (chosen == actStats)          emit requestStats();
	else if (chosen == actSettings)  emit requestSettings();
	else if (chosen == actPin)       setPinned(true);
	else if (chosen == actHide)      emit requestHide();
	else if (chosen == actQuit)      emit requestQuit();
	else if (chosen == actCopyPath) {
		QApplication::clipboard()->setText(target.path);
	}
	else if (chosen == actLocate) {
		if (!target.path.isEmpty())
			QDesktopServices::openUrl(
				QUrl::fromLocalFile(QFileInfo(target.path).absolutePath()));
	}
	else if (chosen == actRestart) {
		// 预检：菜单期间进程可能已经退出
		QVector<quint32> pids = collectPidsByPath(target.path);
		if (pids.isEmpty()) {
			QMessageBox::warning(
				this, Tr::s("restart.fail"),
				Tr::s("restart.alreadyClosed").arg(target.name));
			return;
		}

		auto ret = QMessageBox::question(
			this, Tr::s("menu.restart"),
			Tr::s("restart.confirm").arg(target.name),
			QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (ret == QMessageBox::Yes)
			restartProcess(target);
	}
}

void MainWindow::closeEvent(QCloseEvent* e) {
	Settings::inst().setWindowPos(pos());
	e->ignore();
	emit requestHide();
}