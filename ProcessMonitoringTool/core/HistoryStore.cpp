#include "HistoryStore.h"
#include "Settings.h"
#include <QCoreApplication>
#include <QDir>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include <QDebug>

HistoryStore& HistoryStore::inst() { static HistoryStore s; return s; }

HistoryStore::HistoryStore() : QObject(nullptr) {}

bool HistoryStore::open() {
	m_path = QDir(QCoreApplication::applicationDirPath()).filePath("history.db");
	m_db = QSqlDatabase::addDatabase("QSQLITE", "history");
	m_db.setDatabaseName(m_path);
	if (!m_db.open()) {
		qWarning() << "HistoryStore open failed:" << m_db.lastError().text();
		return false;
	}

	QSqlQuery q(m_db);
	q.exec("PRAGMA journal_mode=WAL");
	q.exec("PRAGMA synchronous=NORMAL");
	q.exec("PRAGMA temp_store=MEMORY");

	ensureSchema();

	m_flushTimer = new QTimer(this);
	m_flushTimer->setInterval(5000);
	connect(m_flushTimer, &QTimer::timeout, this, &HistoryStore::flush);
	m_flushTimer->start();

	purgeOld();
	return true;
}

void HistoryStore::close() {
	if (m_flushTimer) { m_flushTimer->stop(); }
	flush();
	if (m_db.isOpen()) m_db.close();
}

void HistoryStore::ensureSchema() {
	QSqlQuery q(m_db);
	q.exec("CREATE TABLE IF NOT EXISTS samples("
		"ts INTEGER NOT NULL,"
		"metric INTEGER NOT NULL,"
		"rank INTEGER NOT NULL,"
		"pid INTEGER NOT NULL,"
		"path TEXT NOT NULL,"
		"name TEXT NOT NULL,"
		"value REAL NOT NULL)");
	q.exec("CREATE INDEX IF NOT EXISTS idx_metric_ts ON samples(metric, ts)");
	q.exec("CREATE INDEX IF NOT EXISTS idx_path_ts  ON samples(path, ts)");
}

void HistoryStore::enqueue(int metric, const QVector<RankEntry>& entries, qint64 ts) {
	Pending p; p.metric = metric; p.rows = entries; p.ts = ts;
	m_buf.push_back(std::move(p));
}

void HistoryStore::flush() {
	if (m_buf.isEmpty()) return;
	auto buf = std::move(m_buf);
	m_buf.clear();

	m_db.transaction();
	QSqlQuery q(m_db);
	q.prepare("INSERT INTO samples(ts,metric,rank,pid,path,name,value) VALUES(?,?,?,?,?,?,?)");
	for (const auto& batch : buf) {
		for (int i = 0; i < batch.rows.size(); ++i) {
			const auto& r = batch.rows[i];
			q.addBindValue(batch.ts);
			q.addBindValue(batch.metric);
			q.addBindValue(i);
			q.addBindValue((qint64)r.pid);
			q.addBindValue(r.path);
			q.addBindValue(r.name);
			q.addBindValue(r.value);
			q.exec();
		}
	}
	m_db.commit();
}

QVector<HistoryStore::Record> HistoryStore::query(int metric, qint64 from, qint64 to, int limit, int offset) {
	QVector<Record> out;
	QSqlQuery q(m_db);
	q.prepare("SELECT ts,metric,rank,pid,path,name,value FROM samples "
		"WHERE metric=? AND ts>=? AND ts<=? "
		"ORDER BY ts ASC LIMIT ? OFFSET ?");
	q.addBindValue(metric);
	q.addBindValue(from);
	q.addBindValue(to);
	q.addBindValue(limit);
	q.addBindValue(offset);
	if (!q.exec()) return out;
	while (q.next()) {
		Record r;
		r.ts = q.value(0).toLongLong();
		r.metric = q.value(1).toInt();
		r.rank = q.value(2).toInt();
		r.pid = (quint32)q.value(3).toLongLong();
		r.path = q.value(4).toString();
		r.name = q.value(5).toString();
		r.value = q.value(6).toDouble();
		out.push_back(r);
	}
	return out;
}

int HistoryStore::count(int metric, qint64 from, qint64 to) {
	QSqlQuery q(m_db);
	q.prepare("SELECT COUNT(*) FROM samples WHERE metric=? AND ts>=? AND ts<=?");
	q.addBindValue(metric); q.addBindValue(from); q.addBindValue(to);
	if (q.exec() && q.next()) return q.value(0).toInt();
	return 0;
}

void HistoryStore::purgeOld() {
	qint64 cut = QDateTime::currentMSecsSinceEpoch() - qint64(Settings::inst().retentionHours()) * 3600LL * 1000LL;
	QSqlQuery q(m_db);
	q.prepare("DELETE FROM samples WHERE ts < ?");
	q.addBindValue(cut);
	q.exec();
}