#include "HeatmapWidget.h"
#include "core/Tr.h"

#include <QPainter>
#include <QDateTime>
#include <QHash>
#include <QFileInfo>
#include <algorithm>

HeatmapWidget::HeatmapWidget(QWidget* parent) : QWidget(parent) {
	setMinimumHeight(240);
}

void HeatmapWidget::clear() {
	m_recs.clear();
	m_from = m_to = 0;
	update();
}

void HeatmapWidget::setData(const QVector<HistoryStore::Record>& recs, qint64 from, qint64 to) {
	m_recs = recs;
	m_from = from;
	m_to = to;
	update();
}

void HeatmapWidget::paintEvent(QPaintEvent*) {
	QPainter p(this);
	p.fillRect(rect(), QColor(24, 26, 32));
	p.setRenderHint(QPainter::Antialiasing, false);

	if (m_recs.isEmpty() || m_to <= m_from) {
		p.setPen(QColor(200, 200, 200));
		p.drawText(rect(), Qt::AlignCenter, Tr::s("stats.empty"));
		return;
	}

	// 1. 按 path 聚合总量，取 Top 60 行
	QHash<QString, double> peak;
	for (const auto& r : m_recs) {
		double& v = peak[r.path];
		v = qMax(v, r.value);
	}
	QList<QPair<QString, double>> rows;
	for (auto it = peak.constBegin(); it != peak.constEnd(); ++it)
		rows.push_back({ it.key(), it.value() });
	std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
	if (rows.size() > 60) rows = rows.mid(0, 60);

	QHash<QString, int> rowIdx;
	for (int i = 0; i < rows.size(); ++i) rowIdx[rows[i].first] = i;

	// 2. 绘制区
	int leftMargin = 180;
	int topMargin = 24;
	int rightMargin = 12;
	int bottomMargin = 24;

	QRect plot(leftMargin, topMargin,
		width() - leftMargin - rightMargin,
		height() - topMargin - bottomMargin);
	if (plot.width() < 10 || plot.height() < 10) return;

	// 3. 分桶
	int cols = plot.width();
	int rowsCount = rows.size();
	int rowH = qMax(3, plot.height() / qMax(1, rowsCount));

	// 计算全局最大
	double maxVal = 0.0;
	for (const auto& r : m_recs) maxVal = qMax(maxVal, r.value);
	if (maxVal <= 0) maxVal = 1.0;

	// 网格值
	QVector<QVector<double>> grid(rowsCount, QVector<double>(cols, 0.0));
	qint64 span = m_to - m_from;
	for (const auto& r : m_recs) {
		int ri = rowIdx.value(r.path, -1);
		if (ri < 0) continue;
		int c = int(double(r.ts - m_from) / double(span) * (cols - 1));
		if (c < 0) c = 0; if (c >= cols) c = cols - 1;
		if (r.value > grid[ri][c]) grid[ri][c] = r.value;
	}

	// 颜色映射（对数刻度）
	auto colorFor = [&](double v) -> QColor {
		if (v <= 0) return QColor(32, 34, 40);
		double t = std::log1p(v) / std::log1p(maxVal);
		t = qBound(0.0, t, 1.0);
		int r = int(20 + t * 220);
		int g = int(80 + t * 60);
		int b = int(220 - t * 200);
		return QColor(r, g, b);
		};

	// 4. 绘制热力块
	for (int ri = 0; ri < rowsCount; ++ri) {
		int y = plot.top() + ri * rowH;
		for (int c = 0; c < cols; ++c) {
			QColor col = colorFor(grid[ri][c]);
			p.fillRect(plot.left() + c, y, 1, rowH, col);
		}

		// 行标签
		p.setPen(QColor(230, 230, 230));
		QFont f = font(); f.setPointSize(8); p.setFont(f);
		QString name = QFileInfo(rows[ri].first).fileName();
		if (name.isEmpty()) name = rows[ri].first;
		name = p.fontMetrics().elidedText(name, Qt::ElideMiddle, leftMargin - 12);
		p.drawText(QRect(4, y, leftMargin - 8, rowH),
			Qt::AlignVCenter | Qt::AlignRight, name);
	}

	// 5. 时间轴
	p.setPen(QColor(180, 180, 180));
	QFont f = font(); f.setPointSize(8); p.setFont(f);
	QDateTime dtFrom = QDateTime::fromMSecsSinceEpoch(m_from);
	QDateTime dtTo = QDateTime::fromMSecsSinceEpoch(m_to);
	int ticks = 6;
	for (int i = 0; i <= ticks; ++i) {
		int x = plot.left() + plot.width() * i / ticks;
		QString label = QDateTime::fromMSecsSinceEpoch(
			m_from + span * i / ticks).toString("MM-dd HH:mm");
		p.drawLine(x, plot.top(), x, plot.bottom() + 3);
		p.drawText(QRect(x - 40, plot.bottom() + 4, 80, 18),
			Qt::AlignCenter, label);
	}
}