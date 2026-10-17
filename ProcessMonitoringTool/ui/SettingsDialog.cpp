#include "SettingsDialog.h"
#include "core/Settings.h"
#include "core/Tr.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QSlider>
#include <QLabel>

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
	setWindowTitle(Tr::s("set.title"));
	Settings& S = Settings::inst();

	QFormLayout* form = new QFormLayout;

	QComboBox* lang = new QComboBox;
	lang->addItem("简体中文", "zh_CN");
	lang->addItem("English", "en_US");
	int li = lang->findData(S.language());
	if (li >= 0) lang->setCurrentIndex(li);
	form->addRow(Tr::s("set.language"), lang);

	QSpinBox* interval = new QSpinBox;
	interval->setRange(1, 300);
	interval->setSuffix(" s");
	interval->setValue(S.sampleIntervalMs() / 1000);
	form->addRow(Tr::s("set.interval"), interval);

	QSpinBox* topN = new QSpinBox;
	topN->setRange(5, 20);
	topN->setValue(S.topN());
	form->addRow(Tr::s("set.topn"), topN);

	QSpinBox* retention = new QSpinBox;
	retention->setRange(1, 24 * 90);
	retention->setSuffix(" h");
	retention->setValue(S.retentionHours());
	form->addRow(Tr::s("set.retention"), retention);

	QSpinBox* gpuMul = new QSpinBox;
	gpuMul->setRange(1, 20);
	gpuMul->setValue(S.gpuDivisor());
	form->addRow(Tr::s("set.gpuMul"), gpuMul);

	QCheckBox* netEn = new QCheckBox;
	netEn->setChecked(S.networkEnabled());
	form->addRow(Tr::s("set.enableNet"), netEn);

	QPlainTextEdit* filter = new QPlainTextEdit;
	filter->setPlainText(S.filterList().join('\n'));
	filter->setMaximumHeight(90);
	form->addRow(Tr::s("set.filter"), filter);

	QPlainTextEdit* protectedList = new QPlainTextEdit;
	protectedList->setPlainText(Settings::inst().protectedList().join('\n'));
	protectedList->setMaximumHeight(120);
	form->addRow(Tr::s("set.protected"), protectedList);

	QLabel* protTip = new QLabel(Tr::s("set.protectedTip"));
	protTip->setWordWrap(true);
	protTip->setStyleSheet("color: gray; font-size: 11px;");
	form->addRow(QString(), protTip);

	QSlider* opacity = new QSlider(Qt::Horizontal);
	opacity->setRange(20, 100);
	opacity->setValue(qRound(S.opacity() * 100));
	form->addRow(Tr::s("set.opacity"), opacity);

	QSlider* scale = new QSlider(Qt::Horizontal);
	scale->setRange(50, 200);
	scale->setValue(qRound(S.scale() * 100));
	form->addRow(Tr::s("set.scale"), scale);

	QCheckBox* autostart = new QCheckBox;
	autostart->setChecked(S.autoStart());
	form->addRow(Tr::s("set.autoStart"), autostart);

	QDialogButtonBox* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
	buttons->button(QDialogButtonBox::Ok)->setText(Tr::s("set.ok"));
	buttons->button(QDialogButtonBox::Cancel)->setText(Tr::s("set.cancel"));

	connect(buttons, &QDialogButtonBox::accepted, this, [this, lang, interval, topN, retention, gpuMul, netEn,
		filter, protectedList, opacity, scale, autostart] {
			auto& S = Settings::inst();
			S.setLanguage(lang->currentData().toString());
			S.setSampleIntervalMs(interval->value() * 1000);
			S.setTopN(topN->value());
			S.setRetentionHours(retention->value());
			S.setGpuDivisor(gpuMul->value());
			S.setNetworkEnabled(netEn->isChecked());
			S.setFilterList(filter->toPlainText().split('\n', Qt::SkipEmptyParts));
			S.setProtectedList(protectedList->toPlainText().split('\n', Qt::SkipEmptyParts));
			S.setOpacity(opacity->value() / 100.0);
			S.setScale(scale->value() / 100.0);
			S.setAutoStart(autostart->isChecked());
			S.save();
			accept();
		});
	connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

	auto* root = new QVBoxLayout(this);
	root->addLayout(form);
	root->addWidget(buttons);
}