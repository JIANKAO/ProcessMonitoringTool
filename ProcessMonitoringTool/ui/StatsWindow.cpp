#include "StatsWindow.h"
#include "HeatmapWidget.h"
#include "core/HistoryStore.h"
#include "core/Tr.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabBar>
#include <QDateTimeEdit>
#include <QPushButton>
#include <QLabel>
#include <QDateTime>
#include <QTimer>

StatsWindow::StatsWindow(QWidget* parent) : QWidget(parent) {
	setWindowTitle(Tr::s("stats.title"));
	resize(960, 620);

	m_tabs = new QTabBar;
	for (int i = 0; i < (int)MetricType::Count; ++i) {
		auto m = (MetricType)i;
		m_tabs->addTab(Tr::s(metricKey(m)));
	}

	m_from = new QDateTimeEdit(QDateTime::currentDateTime().addSecs(-3600));
	m_to = new QDateTimeEdit(QDateTime::currentDateTime());
	for (auto* e : { m_from, m_to }) {
		e->setDisplayFormat("yyyy-MM-dd HH:mm");
		e->setCalendarPopup(true);
	}

	auto* btnLoad = new QPushButton(Tr::s("stats.load"));
	auto* btn1h = new QPushButton(Tr::s("stats.quick1h"));
	auto* btn6h = new QPushButton(Tr::s("stats.quick6h"));
	auto* btn24h = new QPushButton(Tr::s("stats.quick24h"));
	auto* btnAll = new QPushButton(Tr::s("stats.quickAll"));
	m_status = new QLabel;

	m_heat = new HeatmapWidget;

	auto* top = new QHBoxLayout;
	top->addWidget(m_tabs);
	top->addStretch();
	top->addWidget(new QLabel(Tr::s("stats.from"))); top->addWidget(m_from);
	top->addWidget(new QLabel(Tr::s("stats.to")));   top->addWidget(m_to);
	top->addWidget(btn1h); top->addWidget(btn6h); top->addWidget(btn24h); top->addWidget(btnAll);
	top->addWidget(btnLoad);

	auto* root = new QVBoxLayout(this);
	root->addLayout(top);
	root->addWidget(m_heat, 1);
	root->addWidget(m_status);

	connect(btnLoad, &QPushButton::clicked, this, &StatsWindow::reload);
	connect(btn1h, &QPushButton::clicked, this, [this] { quickRange(1); });
	connect(btn6h, &QPushButton::clicked, this, [this] { quickRange(6); });
	connect(btn24h, &QPushButton::clicked, this, [this] { quickRange(24); });
	connect(btnAll, &QPushButton::clicked, this, [this] { quickRange(0); });
	connect(m_tabs, &QTabBar::currentChanged, this, &StatsWindow::reload);

	QTimer::singleShot(0, this, &StatsWindow::reload);
}

StatsWindow::~StatsWindow() = default;

void StatsWindow::quickRange(int hours) {
	auto now = QDateTime::currentDateTime();
	m_to->setDateTime(now);
	if (hours <= 0) {
		auto oldest = now.addSecs(-3600LL * 24 * 90);
		m_from->setDateTime(oldest);
	}
	else {
		m_from->setDateTime(now.addSecs(-3600LL * hours));
	}
	reload();
}

void StatsWindow::reload() {
	int idx = m_tabs->currentIndex();
	if (idx < 0 || idx >= (int)MetricType::Count) return;
	MetricType m = (MetricType)idx;

	qint64 from = m_from->dateTime().toMSecsSinceEpoch();
	qint64 to = m_to->dateTime().toMSecsSinceEpoch();
	if (from >= to) to = from + 60000;

	loadInChunks((int)m, from, to);
}

void StatsWindow::loadInChunks(int metric, qint64 from, qint64 to) {
	const int kChunk = 20000;
	int total = HistoryStore::inst().count(metric, from, to);
	if (total == 0) {
		m_heat->setData({}, from, to);
		m_status->setText(Tr::s("stats.empty"));
		return;
	}

	auto all = std::make_shared<QVector<HistoryStore::Record>>();
	auto offset = std::make_shared<int>(0);
	auto totalPtr = std::make_shared<int>(total);

	auto step = std::make_shared<std::function<void()>>();
	*step = [this, metric, from, to, all, offset, totalPtr, step, kChunk]() {
		auto chunk = HistoryStore::inst().query(metric, from, to, kChunk, *offset);
		if (chunk.isEmpty()) {
			m_heat->setData(*all, from, to);
			m_status->setText(QString());
			return;
		}
		for (auto& r : chunk) all->push_back(r);
		*offset += chunk.size();

		int pct = qMin(100, int(100.0 * all->size() / qMax(1, *totalPtr)));
		m_status->setText(Tr::s("stats.loading").arg(pct));

		// 每块就刷新一次，让用户感知进度
		m_heat->setData(*all, from, to);

		QTimer::singleShot(0, this, [step]() { (*step)(); });
		};
	(*step)();
}