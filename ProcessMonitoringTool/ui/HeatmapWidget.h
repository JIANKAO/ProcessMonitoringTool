#pragma once
#include <QWidget>
#include "core/HistoryStore.h"

class HeatmapWidget : public QWidget {
	Q_OBJECT
public:
	explicit HeatmapWidget(QWidget* parent = nullptr);
	void setData(const QVector<HistoryStore::Record>& recs, qint64 from, qint64 to);
	void clear();

protected:
	void paintEvent(QPaintEvent*) override;

private:
	QVector<HistoryStore::Record> m_recs;
	qint64 m_from = 0, m_to = 0;
};