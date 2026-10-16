#include "Sampler.h"
#include "Settings.h"
#include "ProcessQuery.h"
#include "GpuQuery.h"
#include "NetQuery.h"

#include <QTimer>
#include <QDateTime>
#include <QFileInfo>
#include <QSet>
#include <algorithm>
#include <windows.h>

Sampler::Sampler(QObject* p) : QThread(p) {}
Sampler::~Sampler() { stop(); }

void Sampler::stop() {
	NetQuery::stop();
	m_stop = true;
	quit();
	wait(3000);
}

void Sampler::run() {
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	m_cores = qMax(1, (int)si.dwNumberOfProcessors);

	// 预热一次，建立 CPU / IO 差分基线
	QVector<RawProc> warm;
	ProcessQuery::enumAll(warm);
	for (const auto& r : warm)
		m_prev[r.pid] = { r.createTime, r.cpuTime, r.ioBytes };
	m_lastTick = QDateTime::currentMSecsSinceEpoch();

	m_timer = new QTimer;
	m_timer->setInterval(Settings::inst().sampleIntervalMs());
	connect(m_timer, &QTimer::timeout, this, &Sampler::doSample, Qt::DirectConnection);
	m_timer->start();

	exec();
	m_timer->stop();
	delete m_timer;
	m_timer = nullptr;
}

void Sampler::doSample() {
	// 动态响应网络开关
	bool wantNet = Settings::inst().networkEnabled();
	bool hasNet = NetQuery::isRunning();
	if (wantNet && !hasNet) {
		if (!NetQuery::start()) {
			// 启动失败就不要再反复重试，避免刷屏和持续开销
			static std::atomic_bool warned{ false };
			if (!warned.exchange(true))
				qWarning() << "NetQuery start failed; network stats unavailable";
		}
	}
	else if (!wantNet && hasNet) {
		NetQuery::stop();
	}
	if (m_stop) return;

	// 运行时可改间隔
	int want = Settings::inst().sampleIntervalMs();
	if (m_timer->interval() != want) m_timer->setInterval(want);

	qint64 now = QDateTime::currentMSecsSinceEpoch();
	qint64 dt = now - m_lastTick;
	if (dt < 100) dt = 100;

	QVector<RawProc> raws;
	if (!ProcessQuery::enumAll(raws)) return;

	// GPU（可以低频率刷新）
	int div = Settings::inst().gpuDivisor();
	if (div <= 1 || (m_tickCount % div) == 0) GpuQuery::query(m_gpuCache);

	// 网络（默认关闭）
	QHash<quint32, quint64> netBytes;
	bool netOk = false;
	if (Settings::inst().networkEnabled()) {
		netOk = NetQuery::take(netBytes);  // 取出累积字节，单位 byte
	}

	QVector<ProcessInfo> procs;
	procs.reserve(raws.size());

	for (const auto& r : raws) {
		ProcessInfo p;
		p.pid = r.pid;
		p.createTime = r.createTime;
		p.name = r.name;
		p.workingSet = r.workingSet;

		auto it = m_prev.find(r.pid);
		if (it != m_prev.end() && it->createTime == r.createTime) {
			quint64 dCpu = r.cpuTime > it->cpuTime ? r.cpuTime - it->cpuTime : 0;
			p.cpu = 100.0 * double(dCpu) / (double(dt) * 10000.0 * m_cores);
			if (p.cpu < 0) p.cpu = 0;

			quint64 dIo = r.ioBytes > it->ioBytes ? r.ioBytes - it->ioBytes : 0;
			p.diskBps = (quint64)(double(dIo) * 1000.0 / double(dt));
		}
		m_prev[r.pid] = { r.createTime, r.cpuTime, r.ioBytes };

		auto git = m_gpuCache.find(r.pid);
		if (git != m_gpuCache.end()) p.gpu = git.value();
		if (netOk) {
			auto nit = netBytes.find(r.pid);
			if (nit != netBytes.end()) {
				// nit.value() 是自上次 take 以来的总字节数，除以采样间隔得到 B/s
				p.netBps = (quint64)(double(nit.value()) * 1000.0 / double(dt));
			}
		}

		procs.push_back(p);
	}

	// 定期清理已退出进程
	if (m_prev.size() > procs.size() * 2 + 128) {
		QHash<quint32, Prev> fresh;
		fresh.reserve(procs.size());
		for (const auto& p : procs) {
			auto it = m_prev.find(p.pid);
			if (it != m_prev.end()) fresh[p.pid] = it.value();
		}
		m_prev = std::move(fresh);
	}

	m_lastTick = now;
	++m_tickCount;

	static const MetricType kAll[] = { MetricType::CPU, MetricType::Memory,
									  MetricType::Disk, MetricType::Network,
									  MetricType::GPU };
	for (MetricType m : kAll) {
		if (m == MetricType::Network && !Settings::inst().networkEnabled()) continue;
		auto ranks = computeRank(m, procs, now);
		emit ranksReady((int)m, ranks, now);
	}
}

QVector<RankEntry> Sampler::computeRank(MetricType m, const QVector<ProcessInfo>& procs, qint64 now) {
	auto valueOf = [m](const ProcessInfo& p) -> double {
		switch (m) {
		case MetricType::CPU:     return p.cpu;
		case MetricType::Memory:  return double(p.workingSet);
		case MetricType::Disk:    return double(p.diskBps);
		case MetricType::Network: return double(p.netBps);
		case MetricType::GPU:     return p.gpu;
		default: return 0;
		}
		};

	QStringList filters = Settings::inst().filterList();
	QVector<const ProcessInfo*> sorted;
	sorted.reserve(procs.size());
	for (const auto& p : procs) {
		bool drop = false;
		for (const auto& f : filters) {
			if (!f.isEmpty() && p.name.contains(f, Qt::CaseInsensitive)) { drop = true; break; }
		}
		if (drop) continue;
		sorted.push_back(&p);
	}
	std::sort(sorted.begin(), sorted.end(), [&](const ProcessInfo* a, const ProcessInfo* b) {
		return valueOf(*a) > valueOf(*b);
		});

	int n = qMin(Settings::inst().topN(), sorted.size());
	QVector<RankEntry> out;
	out.reserve(n);

	auto& firstMap = m_firstSeen[(int)m];
	QSet<quint32> seen;

	for (int i = 0; i < n; ++i) {
		const auto* p = sorted[i];
		double v = valueOf(*p);
		if (v <= 0.0) continue;

		RankEntry e;
		e.pid = p->pid;
		e.path = ProcessQuery::resolvePath(p->pid, p->createTime);
		e.name = e.path.isEmpty() ? p->name : QFileInfo(e.path).fileName();
		e.value = v;

		auto it = firstMap.find(p->pid);
		if (it == firstMap.end()) {
			firstMap[p->pid] = now;
			e.firstSeenMs = now;
		}
		else {
			e.firstSeenMs = it.value();
		}
		e.durationMs = now - e.firstSeenMs;
		out.push_back(e);
		seen.insert(p->pid);
	}

	for (auto it = firstMap.begin(); it != firstMap.end(); ) {
		if (!seen.contains(it.key())) it = firstMap.erase(it);
		else ++it;
	}

	return out;
}