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

static constexpr qint64 kStaleTtlMs = 30 * 1000;   // stale 条目保留 30 秒
static constexpr int    kMaxStaleShown = 5;        // 最多显示 5 个 stale

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

    const QStringList filters = Settings::inst().filterList();

    // ---- 第一遍：算 key + 过滤 + 按 key 聚合 ----
    struct Agg {
        QString key;
        QString path;
        QString name;
        double  value = 0.0;
        int     count = 0;
    };
    QHash<QString, Agg> aggMap;
    aggMap.reserve(procs.size() / 2 + 16);

    // aliveKeys 用于判断"某程序是否还有活着的进程"——
    // 注意：被过滤的程序也要进 aliveKeys，否则会误判为"已终止"而 stale 化
    QSet<QString> aliveKeys;
    aliveKeys.reserve(procs.size());

    for (const auto& p : procs) {
        const QString path = ProcessQuery::resolvePath(p.pid, p.createTime);
        const QString key = path.isEmpty()
            ? (QStringLiteral("pid:") + QString::number(p.pid))
            : path;

        aliveKeys.insert(key);

        // 过滤（按进程名，大小写不敏感）
        bool drop = false;
        for (const auto& f : filters) {
            if (!f.isEmpty() && p.name.contains(f, Qt::CaseInsensitive)) { drop = true; break; }
        }
        if (drop) continue;

        const double v = valueOf(p);
        if (v <= 0.0) continue;   // 无占用的实例不计入 procCount

        auto it = aggMap.find(key);
        if (it == aggMap.end()) {
            Agg a;
            a.key = key;
            a.path = path;
            a.name = path.isEmpty() ? p.name : QFileInfo(path).fileName();
            a.value = v;
            a.count = 1;
            aggMap.insert(key, a);
        }
        else {
            it->value += v;
            ++it->count;
        }
    }

    // ---- 排序 ----
    QVector<Agg> sorted;
    sorted.reserve(aggMap.size());
    for (auto it = aggMap.constBegin(); it != aggMap.constEnd(); ++it)
        sorted.push_back(it.value());

    std::sort(sorted.begin(), sorted.end(),
        [](const Agg& a, const Agg& b) { return a.value > b.value; });

    const int n = qMin(Settings::inst().topN(), sorted.size());

    auto& firstMap = m_firstSeen[(int)m];
    auto& lastMap = m_lastEntry[(int)m];
    auto& staleMap = m_staleCache[(int)m];

    // ---- 构建本轮活跃榜单 ----
    QVector<RankEntry> fresh;
    fresh.reserve(n);
    QSet<QString> freshKeys;
    freshKeys.reserve(n);

    for (int i = 0; i < n; ++i) {
        const Agg& a = sorted[i];

        double v = a.value;
        // 聚合后 CPU/GPU 理论上限 100%，但采样误差可能略超，clamp 一下
        if (v > 100.0 && (m == MetricType::CPU || m == MetricType::GPU)) v = 100.0;

        RankEntry e;
        e.key = a.key;
        e.path = a.path;
        e.name = a.name;
        e.value = v;
        e.procCount = a.count;

        auto it = firstMap.find(a.key);
        if (it == firstMap.end()) {
            firstMap[a.key] = now;
            e.firstSeenMs = now;
        }
        else {
            e.firstSeenMs = it.value();
        }
        e.durationMs = now - e.firstSeenMs;

        fresh.push_back(e);
        freshKeys.insert(a.key);
        lastMap[a.key] = e;
        staleMap.remove(a.key);   // 程序重新上榜，清掉同名 stale 避免重复显示
    }

    // ---- 处理掉出榜单 / 已终止 ----
    QStringList toRemove;
    for (auto it = firstMap.constBegin(); it != firstMap.constEnd(); ++it) {
        const QString& key = it.key();
        if (freshKeys.contains(key)) continue;

        if (aliveKeys.contains(key)) {
            // 程序还有进程活着，只是跌出 Top N —— 重置跟踪，不 stale 化
            toRemove.push_back(key);
        }
        else {
            // 所有进程都已终止 —— 用上一轮快照冻结成 stale
            auto lastIt = lastMap.constFind(key);
            if (lastIt != lastMap.constEnd()) {
                RankEntry e = lastIt.value();
                e.stale = true;
                e.staleSinceMs = now;
                // value / durationMs 沿用上一轮，不再更新
                staleMap[key] = e;
            }
            toRemove.push_back(key);
        }
    }
    for (const QString& key : toRemove) {
        firstMap.remove(key);
        lastMap.remove(key);
    }

    // ---- 清理过期 stale ----
    for (auto it = staleMap.begin(); it != staleMap.end(); ) {
        if (now - it.value().staleSinceMs > kStaleTtlMs)
            it = staleMap.erase(it);
        else
            ++it;
    }

    // ---- 追加 stale 到结果末尾 ----
    QVector<RankEntry> staleList;
    staleList.reserve(staleMap.size());
    for (auto it = staleMap.constBegin(); it != staleMap.constEnd(); ++it)
        staleList.push_back(it.value());

    std::sort(staleList.begin(), staleList.end(),
        [](const RankEntry& a, const RankEntry& b) {
            return a.staleSinceMs > b.staleSinceMs;
        });
    if (staleList.size() > kMaxStaleShown)
        staleList.resize(kMaxStaleShown);

    fresh += staleList;
    return fresh;
}