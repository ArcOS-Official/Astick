#pragma once
#include <QObject>
#include <QTimer>
#include <QRandomGenerator>
#include <QJsonObject>
#include <QFile>
#include <QString>

class Compositor;
struct StressConfig {
    uint32_t seed = 1;
    int durationSec = 30; // 0 = forever
    int rateHz = 200;
    QString dumpPath = QStringLiteral("./fuzz.log.json");
    bool headless = false;
};

class StressEngine : public QObject {
    Q_OBJECT
public:
    explicit StressEngine(Compositor *comp, const StressConfig &cfg, QObject *parent=nullptr);
    ~StressEngine() override;
    void start();
    void stop();
    struct Counters { uint64_t ticks=0, fakeClients=0, moves=0, resizes=0, workspaceSwitches=0, anomalies=0; };
    Counters counters() const { return cnts; }
    StressConfig config() const { return cfg; }
    void setRate(int hz);
signals:
    void anomalyDetected(const QJsonObject &entry);
private slots:
    void onTick();
private:
    void maybeCreateFakeClient();
    void maybeDestroyFakeClient();
    void randomMoveOrResize();
    void randomWorkspaceSwitch();
    void directlyMutateState();
    void checkInvariantsAndLog();
    void logAnomaly(const QString &kind, const QString &name, const QJsonObject &detail);

    Compositor *comp = nullptr;
    StressConfig cfg;
    QTimer timer;
    QRandomGenerator rng;
    Counters cnts;
    QFile logFile;
    qint64 startMs = 0;
    // lightweight fake client pool
    struct FakeClient { uint64_t id=0; int x=0,y=0,w=800,h=600; bool mapped=false; };
    QVector<FakeClient> fakeClients;
    QList<QJsonObject> ringBuffer;
};
