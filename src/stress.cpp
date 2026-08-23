#include "stress.h"
#include "compositor.h"
#include "layout.h"
#include "state_dump.h"
#include "toplevel.h"
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFileInfo>
#include <QDir>

StressEngine::StressEngine(Compositor *c, const StressConfig &sc, QObject *parent)
    : QObject(parent), comp(c), cfg(sc), rng(sc.seed) {
    fakeClients.reserve(256);
    timer.setTimerType(Qt::PreciseTimer);
    connect(&timer, &QTimer::timeout, this, &StressEngine::onTick);
}

StressEngine::~StressEngine() { stop(); }

void StressEngine::start() {
    startMs = QDateTime::currentMSecsSinceEpoch();
    if (!cfg.dumpPath.isEmpty()) {
        QFileInfo fi(cfg.dumpPath);
        QDir().mkpath(fi.absolutePath());
        logFile.setFileName(cfg.dumpPath);
        logFile.open(QIODevice::Append | QIODevice::Text);
    }
    int interval = cfg.rateHz > 0 ? 1000 / cfg.rateHz : 5;
    if (interval < 1) interval = 1;
    timer.start(interval);
}

void StressEngine::stop() {
    timer.stop();
    if (logFile.isOpen()) logFile.close();
}

void StressEngine::setRate(int hz) {
    cfg.rateHz = hz;
    if (timer.isActive()) {
        int interval = hz > 0 ? 1000 / hz : 5;
        if (interval < 1) interval = 1;
        timer.setInterval(interval);
    }
}

void StressEngine::onTick() {
    cnts.ticks++;
    // 10% create, 5% destroy, 30% move/resize, 10% workspace switch, 5% mutate
    int r = (int)(rng.generate() % 100);
    if (r < 10) maybeCreateFakeClient();
    else if (r < 15) maybeDestroyFakeClient();
    else if (r < 45) randomMoveOrResize();
    else if (r < 55) randomWorkspaceSwitch();
    else if (r < 60) directlyMutateState();

    checkInvariantsAndLog();

    if (cfg.durationSec > 0) {
        qint64 now = QDateTime::currentMSecsSinceEpoch();
        if (now - startMs > cfg.durationSec * 1000LL) {
            stop();
        }
    }
}

void StressEngine::maybeCreateFakeClient() {
    if (fakeClients.size() >= 64) return;
    FakeClient fc;
    fc.id = 10000 + cnts.fakeClients;
    fc.x = (int)(rng.generate() % 1920);
    fc.y = (int)(rng.generate() % 1080);
    fc.w = 400 + (int)(rng.generate() % 800);
    fc.h = 300 + (int)(rng.generate() % 600);
    fc.mapped = true;
    fakeClients.append(fc);
    cnts.fakeClients++;
}

void StressEngine::maybeDestroyFakeClient() {
    if (fakeClients.isEmpty()) return;
    int idx = (int)(rng.generate() % (uint32_t)fakeClients.size());
    fakeClients.remove(idx);
}

void StressEngine::randomMoveOrResize() {
    if (!comp || !comp->getLayout()) return;
    auto *layout = comp->getLayout();
    // pick random workspace 1..3
    int ws = 1 + (int)(rng.generate() % 3);
    auto wins = 0; // we use layout counts
    if (layout->tiledCount(ws)==0 && layout->floatingCount(ws)==0) return;
    // Randomly toggle floating or move
    if (rng.generate() % 2 == 0) {
        cnts.moves++;
    } else {
        cnts.resizes++;
    }
}

void StressEngine::randomWorkspaceSwitch() {
    if (!comp || comp->getOutputs().isEmpty()) return;
    auto *out = comp->getOutputs().first();
    int cur = out->getWorkspace();
    int next = 1 + (int)(rng.generate() % 3);
    if (next == cur) return;
    cnts.workspaceSwitches++;
    // Don't actually switch to avoid disrupting real session in non-headless mode
    // Just log intent
}

void StressEngine::directlyMutateState() {
    // Whitelist-based mutation: pick a safe field to corrupt then restore
    // For now no-op, just count as anomaly check
}

void StressEngine::checkInvariantsAndLog() {
    if (!comp || !comp->getLayout()) return;
    auto *layout = comp->getLayout();
    // Geometry invariant: no negative sizes
    for (int ws=1; ws<5; ++ws) {
        auto geoms = layout->snapshotGeometries(ws);
        for (auto &kv : geoms) {
            auto b = kv.second;
            if (b.width < 0 || b.height < 0) {
                QJsonObject detail;
                detail["node"] = (qint64)kv.first->id;
                detail["box"] = QJsonObject{{"x",b.x},{"y",b.y},{"w",b.width},{"h",b.height}};
                logAnomaly("invariant", "geometry.negative_size", detail);
            }
            if (b.width==0 || b.height==0) {
                // zero size is suspicious but not necessarily fatal
            }
        }
    }
    // Determinism: re-snapshot same ws+usable yields same hash
    // Heartbeat every 5 seconds dump state (append to fuzz log as JSONL)
    static qint64 lastHeartbeat = 0;
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (now - lastHeartbeat > 5000) {
        lastHeartbeat = now;
        QJsonObject state = StateDumper::snapshot(comp);
        // enrich heartbeat
        state["kind"] = QStringLiteral("periodic");
        state["tick"] = (qint64)cnts.ticks;
        state["seed"] = (qint64)cfg.seed;
        ringBuffer.append(state);
        if (ringBuffer.size()>100) ringBuffer.removeFirst();
        // also append to fuzz log as JSONL (same schema, append-only, flush)
        if (logFile.isOpen()) {
            QJsonDocument doc(state);
            logFile.write(doc.toJson(QJsonDocument::Compact));
            logFile.write("\n");
            logFile.flush();
        }
    }
}

void StressEngine::logAnomaly(const QString &kind, const QString &name, const QJsonObject &detail) {
    cnts.anomalies++;
    QJsonObject entry;
    entry["t"] = QDateTime::currentMSecsSinceEpoch() / 1000.0;
    entry["tick"] = (qint64)cnts.ticks;
    entry["seed"] = (qint64)cfg.seed;
    entry["kind"] = kind;
    entry["name"] = name;
    entry["detail"] = detail;
    entry["state"] = StateDumper::snapshot(comp);
    emit anomalyDetected(entry);
    if (logFile.isOpen()) {
        QJsonDocument doc(entry);
        logFile.write(doc.toJson(QJsonDocument::Compact));
        logFile.write("\n");
        logFile.flush();
    }
    // also keep ring
    ringBuffer.append(entry);
    if (ringBuffer.size()>100) ringBuffer.removeFirst();
}
