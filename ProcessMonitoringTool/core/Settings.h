#pragma once
#include <QObject>
#include <QStringList>
#include <QPoint>
#include <QSize>
#include <QSettings>

class Settings : public QObject {
    Q_OBJECT
public:
    static Settings& inst();

    void load();
    void save();

    // 基础
    QString language() const;
    int  sampleIntervalMs() const;   // >=1000
    int  topN() const;               // 5..20
    int  retentionHours() const;     // >=1
    int  gpuDivisor() const;         // >=1
    bool networkEnabled() const;
    QStringList filterList() const;

    // 外观
    double opacity() const;          // 0.2..1.0
    double scale() const;            // 0.5..2.0
    QPoint windowPos() const;
    QSize  windowSize() const;
    int    activeMetric() const;
    bool   pinned() const;

    // 系统
    bool autoStart() const;

    void setLanguage(const QString&);
    void setSampleIntervalMs(int);
    void setTopN(int);
    void setRetentionHours(int);
    void setGpuDivisor(int);
    void setNetworkEnabled(bool);
    void setFilterList(const QStringList&);
    void setOpacity(double);
    void setScale(double);
    void setWindowPos(const QPoint&);
    void setWindowSize(const QSize&);
    void setActiveMetric(int);
    void setPinned(bool);
    void setAutoStart(bool);

signals:
    void changed();

private:
    Settings();
    QSettings* m_qs = nullptr;
};