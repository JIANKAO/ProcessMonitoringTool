#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QVector>
#include <QHash>
#include <QTimer>
#include "Metrics.h"

class HistoryStore : public QObject {
	Q_OBJECT
public:
	static HistoryStore& inst();

	bool open();
	void close();

	struct Record {
		qint64  ts = 0;
		int     metric = 0;
		int     rank = 0;
		quint32 pid = 0;
		QString path;
		QString name;
		double  value = 0.0;
	};

	// 写入（主线程槽，会批量落库）
	void enqueue(int metric, const QVector<RankEntry>& entries, qint64 ts);

	// 查询
	QVector<Record> query(int metric, qint64 from, qint64 to, int limit, int offset);
	int count(int metric, qint64 from, qint64 to);
	void purgeOld();

private slots:
	void flush();

private:
	HistoryStore();
	void ensureSchema();

	QSqlDatabase m_db;
	QString m_path;
	struct Pending { int metric; QVector<RankEntry> rows; qint64 ts; };
	QVector<Pending> m_buf;
	QTimer* m_flushTimer = nullptr;
};