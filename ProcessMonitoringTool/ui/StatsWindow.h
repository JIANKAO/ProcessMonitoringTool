#pragma once
#include <QWidget>
#include "core/Metrics.h"

class HeatmapWidget;
class QDateTimeEdit;
class QTabBar;

class StatsWindow : public QWidget {
	Q_OBJECT
public:
	explicit StatsWindow(QWidget* parent = nullptr);
	~StatsWindow();

private slots:
	void reload();
	void quickRange(int hours);

private:
	QTabBar* m_tabs = nullptr;
	QDateTimeEdit* m_from = nullptr;
	QDateTimeEdit* m_to = nullptr;
	HeatmapWidget* m_heat = nullptr;
	class QLabel* m_status = nullptr;

	void loadInChunks(int metric, qint64 from, qint64 to);
};