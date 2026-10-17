#pragma once
#include <QThread>
#include <QHash>
#include <QVector>
#include <QSet>
#include <atomic>
#include "Metrics.h"

class QTimer;

class Sampler : public QThread {
	Q_OBJECT
public:
	explicit Sampler(QObject* parent = nullptr);
	~Sampler() override;
	void stop();

signals:
	void ranksReady(int metric, QVector<RankEntry> entries, qint64 ts);

protected:
	void run() override;
	void doSample();
	QVector<RankEntry> computeRank(MetricType m, const QVector<ProcessInfo>& procs, qint64 now);

private:
	struct Prev {
		quint64 createTime = 0;
		quint64 cpuTime = 0;
		quint64 ioBytes = 0;
	};

	QTimer* m_timer = nullptr;
	QHash<quint32, Prev> m_prev;
	QHash<quint32, double> m_gpuCache;

	// 以下三张改为按聚合键（path）索引
	QHash<QString, qint64>    m_firstSeen[(int)MetricType::Count];
	QHash<QString, RankEntry> m_lastEntry[(int)MetricType::Count];
	QHash<QString, RankEntry> m_staleCache[(int)MetricType::Count];

	qint64 m_lastTick = 0;
	int    m_tickCount = 0;
	int    m_cores = 1;
	std::atomic_bool m_stop{ false };
};