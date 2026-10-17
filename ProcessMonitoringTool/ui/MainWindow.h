#pragma once
#include <QWidget>
#include "core/Metrics.h"

class MainWindow : public QWidget {
	Q_OBJECT
public:
	explicit MainWindow(QWidget* parent = nullptr);
	~MainWindow() override;

	void setPinned(bool on);
	bool isPinned() const { return m_pinned; }
	void setMetric(MetricType m);
	MetricType metric() const { return m_metric; }

	void applySettings();

public slots:
	void onRanksReady(int metric, QVector<RankEntry> entries, qint64 ts);

signals:
	void requestSettings();
	void requestStats();
	void requestHide();
	void requestQuit();

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent*) override;
	void mouseMoveEvent(QMouseEvent*) override;
	void mouseReleaseEvent(QMouseEvent*) override;
	void contextMenuEvent(QContextMenuEvent*) override;
	void closeEvent(QCloseEvent*) override;

private:
	QSize baseSize() const;
	QRectF tabRect(int i) const;

	bool m_pinned = false;
	double m_opacity = 0.8;
	double m_scale = 1.0;
	MetricType m_metric = MetricType::CPU;

	QVector<RankEntry> m_entries[(int)MetricType::Count];

	// 拖动
	bool m_dragging = false;
	QPoint m_dragOffset;
private:
	// 右键交互
	int  entryIndexAt(const QPoint& pos) const;
	void beginFreeze();
	void endFreeze();
	bool restartProcess(const RankEntry& e);

	bool m_freezeUpdates = false;
	QVector<RankEntry> m_pendingEntries[(int)MetricType::Count];
	bool m_pendingDirty[(int)MetricType::Count] = {};
};