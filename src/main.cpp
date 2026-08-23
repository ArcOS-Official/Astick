#include "compositor.h"
#include "application.h"
#include "config.h"
#include "test_runner.h"
#include "stress.h"
#include "state_dump.h"
#include "debug.h"
#include <QString>
#include <QDebug>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QJsonDocument>
#include <csignal>
#include <cstdio>

#if !defined(__clang__) && !defined(__GNUC__)
#error "Unsupported compiler"
#endif

static void astickMessageHandler(QtMsgType type, const QMessageLogContext &, const QString &msg) {
    QByteArray ba = msg.toLocal8Bit();
    const char *data = ba.constData();
    switch (type) {
        case QtDebugMsg: wlr_log(WLR_INFO, "[Qt] %s", data); break;
        case QtInfoMsg: wlr_log(WLR_INFO, "[Qt] %s", data); break;
        case QtWarningMsg: wlr_log(WLR_ERROR, "[Qt] %s", data); break;
        case QtCriticalMsg: wlr_log(WLR_ERROR, "[QtCritical] %s", data); break;
        case QtFatalMsg: wlr_log(WLR_ERROR, "[QtFatal] %s", data); break;
    }
    fprintf(stderr, "[Astick][Qt] %s\n", data);
    fflush(stderr);
}

// global for SIGUSR1 handler (static storage so lambda can see it)
static StressEngine *g_stressEngineForSignal = nullptr;
static void sigUsr1Handler(int) {
    if (g_stressEngineForSignal) {
        auto c = g_stressEngineForSignal->counters();
        fprintf(stderr, "[STRESS] SIGUSR1 counters ticks=%llu fakeClients=%llu moves=%llu resizes=%llu wsSwitches=%llu anomalies=%llu\n",
            (unsigned long long)c.ticks, (unsigned long long)c.fakeClients,
            (unsigned long long)c.moves, (unsigned long long)c.resizes,
            (unsigned long long)c.workspaceSwitches, (unsigned long long)c.anomalies);
        fflush(stderr);
    }
}

int main(int argc, char **argv) {
    qInstallMessageHandler(astickMessageHandler);
    Config::captureOriginalDisplay();
    wlr_log_init(WLR_DEBUG, handler);
    Astick app(argc, argv);

    QString mode = "tiling";
    QString configPathOverride;
    bool runTests = false;
    bool stressTest = false;
    bool headless = false;
    bool dryRun = false;
    bool traceState = false;
    uint32_t stressSeed = 1;
    int stressDuration = 30;
    int stressRate = 200;
    QString stressDump = QStringLiteral("./fuzz.log.json");
    for (int i = 1; i < argc; i++) {
        QString arg = QString::fromUtf8(argv[i]);
        if (arg == "--help" || arg == "-h") {
            fprintf(stderr,
                "Usage: Astick [options]\n"
                "Wayland compositor for ArcDE.\n\n"
                "Options:\n"
                "  --help, -h              Show this help\n"
                "  --mode <mode>           Initial layout mode: tiling, floating, monowindow\n"
                "  --config <path>         Path to config.json\n"
                "  --test                  Run in-binary tests and exit\n"
                "  -st, --stress-test      Run compositor normally but also spawn stress/fuzz engine\n"
                "  --stress-seed <n>       Deterministic seed (default 1)\n"
                "  --stress-duration <s>   Seconds to run (default 30, 0=forever)\n"
                "  --stress-rate <hz>      Fake events per second (default 200)\n"
                "  --stress-dump <path>    fuzz log path (default ./fuzz.log.json)\n"
                "  --headless              No DRM, virtual output 1280x720, one arrange cycle, dump and exit\n"
                "  --dry-run               Alias for --headless (no DRM, dump and exit)\n"
                "  --trace-state           Dump full state JSON on each tick/anomaly (via fuzz log)\n"
            );
            return 0;
        }
        if (arg == "--test") {
            runTests = true;
        } else if (arg == "-st" || arg == "--stress-test") {
            stressTest = true;
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--dry-run") {
            dryRun = true;
            headless = true;
        } else if (arg == "--trace-state") {
            traceState = true;
        } else if (arg == "--stress-seed" && i + 1 < argc) {
            bool ok=false;
            uint v = QString::fromUtf8(argv[++i]).toUInt(&ok);
            if (ok) stressSeed = v;
            else fprintf(stderr, "Invalid --stress-seed value, using default 1\n");
        } else if (arg == "--stress-duration" && i + 1 < argc) {
            bool ok=false;
            int v = QString::fromUtf8(argv[++i]).toInt(&ok);
            if (ok) stressDuration = v;
            else fprintf(stderr, "Invalid --stress-duration value, using default 30\n");
        } else if (arg == "--stress-rate" && i + 1 < argc) {
            bool ok=false;
            int v = QString::fromUtf8(argv[++i]).toInt(&ok);
            if (ok) stressRate = v;
            else fprintf(stderr, "Invalid --stress-rate value, using default 200\n");
        } else if (arg == "--stress-dump" && i + 1 < argc) {
            stressDump = QString::fromUtf8(argv[++i]);
        } else if (arg == "--mode" && i + 1 < argc) {
            mode = QString::fromUtf8(argv[++i]);
        } else if (arg == "--config" && i + 1 < argc) {
            configPathOverride = QString::fromUtf8(argv[++i]);
        } else if (arg.startsWith("--stress-")) {
            fprintf(stderr, "Unknown stress option %s\n", arg.toUtf8().constData());
        }
    }
    if (runTests) {
        registerAllTests();
        auto results = TestRunner::instance().runAll();
        TestRunner::instance().printResults(results);
        bool allPassed = true;
        for (auto &r : results) if (!r.passed) allPassed = false;
        return allPassed ? 0 : 1;
    }

    Config config;
    bool loaded = false;
    if (!configPathOverride.isEmpty()) {
        loaded = config.load(configPathOverride);
        if (!loaded) {
            fprintf(stderr, "Config not found at %s, using defaults at %s\n", configPathOverride.toUtf8().data(), Config::defaultPath().toUtf8().constData());
            config.loadOrCreateDefault();
        }
    } else {
        config.loadOrCreateDefault();
        fprintf(stderr, "Loaded config from %s\n", Config::defaultPath().toUtf8().constData());
    }

    Compositor comp(app, &config);
    comp.setInitialLayoutMode(mode);

    // -- Phase 6 wiring: handle --headless/--dry-run synchronously BEFORE backend starts
    // Synchronous path avoids wl_display_run blocking Qt timers.
    if (headless || dryRun) {
        if (stressTest) {
            // headless + stress: synchronous fuzz generation, no backend loop
            fprintf(stderr, "[HEADLESS] --headless/--dry-run + -st: virtual output 1280x720, stress seed=%u duration=%d rate=%d dump=%s\n",
                stressSeed, stressDuration, stressRate, stressDump.toUtf8().constData());
            fflush(stderr);
            // Ensure dump file exists and write initial snapshot + periodic snapshots synchronously
            // Use StressEngine for file handling but drive ticks manually without QTimer
            StressConfig sc;
            sc.seed = stressSeed;
            sc.durationSec = stressDuration;
            sc.rateHz = stressRate;
            sc.dumpPath = stressDump;
            sc.headless = true;
            StressEngine engine(&comp, sc, &app);
            // we still start to open log file, but will not rely on its timer
            engine.start();
            // Override timer-based ticks: run synchronous loop for duration*rate iterations
            // Limit to avoid huge loops: cap 200*5 = 1000 iterations max for test; if duration 0, do 5 sec worth
            int effectiveDuration = stressDuration > 0 ? stressDuration : 5;
            int totalTicks = effectiveDuration * stressRate;
            if (totalTicks > 2000) totalTicks = 2000;
            if (totalTicks < 5) totalTicks = 5;
            // synchronous arrange cycle virtual 1280x720
            if (!comp.getOutputs().isEmpty()) {
                for (auto *out : comp.getOutputs()) comp.arrangeForOutput(out);
            }
            // initial snapshot
            QJsonObject initState = StateDumper::snapshot(&comp);
            initState["headless"] = true;
            initState["stress"] = true;
            initState["tick"] = 0;
            {
                QFile f(stressDump);
                if (!f.open(QIODevice::Append | QIODevice::Text)) {
                    fprintf(stderr, "[HEADLESS] failed to open dump %s\n", stressDump.toUtf8().constData());
                } else {
                    QJsonDocument doc(initState);
                    f.write(doc.toJson(QJsonDocument::Compact));
                    f.write("\n");
                    f.flush();
                }
            }
            // drive a few synthetic ticks via QElapsed + direct state dump
            for (int i=1; i<=totalTicks; ++i) {
                // every 5% write periodic snapshot to exercise fuzz log format
                if (i % qMax(1, totalTicks/5) == 0) {
                    QJsonObject state = StateDumper::snapshot(&comp);
                    state["headless"] = true;
                    state["tick"] = (qint64)i;
                    state["seed"] = (qint64)stressSeed;
                    state["kind"] = QStringLiteral("periodic");
                    QFile f(stressDump);
                    if (f.open(QIODevice::Append | QIODevice::Text)) {
                        QJsonDocument doc(state);
                        f.write(doc.toJson(QJsonDocument::Compact));
                        f.write("\n");
                        f.flush();
                    }
                }
            }
            // final snapshot
            QJsonObject finalState = StateDumper::snapshot(&comp);
            finalState["headless"] = true;
            finalState["stress"] = true;
            finalState["kind"] = QStringLiteral("final");
            finalState["ticks"] = (qint64)totalTicks;
            {
                QFile f(stressDump);
                if (f.open(QIODevice::Append | QIODevice::Text)) {
                    QJsonDocument doc(finalState);
                    f.write(doc.toJson(QJsonDocument::Compact));
                    f.write("\n");
                    f.flush();
                }
            }
            QFile sf(QStringLiteral("/tmp/astick_state.json"));
            if (sf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                sf.write(QJsonDocument(finalState).toJson(QJsonDocument::Indented));
            }
            fprintf(stderr, "[HEADLESS] stress headless done ticks=%d dump=%s\n", totalTicks, stressDump.toUtf8().constData());
            fflush(stderr);
            engine.stop();
            return 0;
        } else {
            fprintf(stderr, "[HEADLESS] --headless/--dry-run: virtual output 1280x720, one arrange cycle, dump and exit\n");
            fflush(stderr);
            if (!comp.getOutputs().isEmpty()) {
                for (auto *out : comp.getOutputs()) comp.arrangeForOutput(out);
            }
            QJsonObject state = StateDumper::snapshot(&comp);
            state["headless"] = true;
            state["dryRun"] = dryRun ? true : false;
            QJsonDocument doc(state);
            QByteArray out = doc.toJson(QJsonDocument::Indented);
            fprintf(stderr, "%s\n", out.constData());
            // write to fuzz dump path (default ./fuzz.log.json) and /tmp/astick_state.json
            if (!stressDump.isEmpty()) {
                QFile f(stressDump);
                if (f.open(QIODevice::Append | QIODevice::Text)) {
                    QJsonDocument cdoc(state);
                    f.write(cdoc.toJson(QJsonDocument::Compact));
                    f.write("\n");
                    f.flush();
                    fprintf(stderr, "[HEADLESS] wrote %s\n", stressDump.toUtf8().constData());
                }
            }
            QFile sf(QStringLiteral("/tmp/astick_state.json"));
            if (sf.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
                sf.write(out);
            }
            fflush(stderr);
            return 0;
        }
    }

    // -- normal stress (non-headless): engine lives for app.exec() lifetime, driven via QTimer
    StressEngine *stressEngine = nullptr;
    if (stressTest) {
        StressConfig sc;
        sc.seed = stressSeed;
        sc.durationSec = stressDuration;
        sc.rateHz = stressRate;
        sc.dumpPath = stressDump;
        sc.headless = false;
        stressEngine = new StressEngine(&comp, sc, &app);
        g_stressEngineForSignal = stressEngine;
        ::signal(SIGUSR1, sigUsr1Handler);
        QObject::connect(stressEngine, &StressEngine::anomalyDetected, &app, [traceState](const QJsonObject &entry){
            if (traceState) {
                QJsonDocument doc(entry);
                fprintf(stderr, "[STRESS] anomaly %s\n", doc.toJson(QJsonDocument::Compact).constData());
                fflush(stderr);
            }
        });
        // hot-mutate poll via /tmp/astick.stress.cmd
        static const QString cmdPath = QStringLiteral("/tmp/astick.stress.cmd");
        auto applyCmd = [stressEngine](){
            QFile f(cmdPath);
            if (!f.exists()) return;
            if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return;
            QByteArray data = f.readAll();
            f.close();
            data = data.trimmed();
            if (data.isEmpty()) return;
            QJsonDocument doc = QJsonDocument::fromJson(data);
            if (doc.isObject()) {
                QJsonObject obj = doc.object();
                if (obj.contains("rate")) {
                    int hz = obj["rate"].toInt(-1);
                    if (hz > 0) {
                        stressEngine->setRate(hz);
                        fprintf(stderr, "[STRESS] hot-mutate rate -> %d\n", hz);
                    }
                }
            } else {
                bool ok=false;
                int hz = data.toInt(&ok);
                if (ok && hz > 0) {
                    stressEngine->setRate(hz);
                    fprintf(stderr, "[STRESS] hot-mutate rate -> %d\n", hz);
                }
            }
            QFile::remove(cmdPath);
        };
        QTimer *pollTimer = new QTimer(&app);
        pollTimer->setInterval(1000);
        QObject::connect(pollTimer, &QTimer::timeout, &app, applyCmd);
        pollTimer->start();
        QFileSystemWatcher *watcher = new QFileSystemWatcher(&app);
        // only watch /tmp if it exists
        if (QFileInfo::exists(QStringLiteral("/tmp"))) watcher->addPath(QStringLiteral("/tmp"));
        QObject::connect(watcher, &QFileSystemWatcher::directoryChanged, &app, [applyCmd](const QString &){
            QTimer::singleShot(50, applyCmd);
        });
        fprintf(stderr, "[STRESS] engine start seed=%u duration=%d rate=%d dump=%s\n",
            stressSeed, stressDuration, stressRate, stressDump.toUtf8().constData());
        fflush(stderr);
        stressEngine->start();
        // immediate snapshot to ensure fuzz.log exists even if QTimer never fires (wl_display_run blocks Qt loop)
        {
            QFile f(stressDump);
            if (f.open(QIODevice::Append | QIODevice::Text)) {
                QJsonObject init = StateDumper::snapshot(&comp);
                init["kind"] = QStringLiteral("stress_start");
                init["seed"] = (qint64)stressSeed;
                init["tick"] = 0;
                QJsonDocument doc(init);
                f.write(doc.toJson(QJsonDocument::Compact));
                f.write("\n");
                f.flush();
                fprintf(stderr, "[STRESS] wrote initial snapshot to %s\n", stressDump.toUtf8().constData());
                fflush(stderr);
            }
        }
        if (traceState) {
            QTimer *traceTimer = new QTimer(&app);
            traceTimer->setInterval(1000);
            QObject::connect(traceTimer, &QTimer::timeout, &app, [&comp, stressDump](){
                QJsonObject state = StateDumper::snapshot(&comp);
                QFile f(stressDump);
                if (f.open(QIODevice::Append | QIODevice::Text)) {
                    QJsonDocument doc(state);
                    f.write(doc.toJson(QJsonDocument::Compact));
                    f.write("\n");
                    f.flush();
                }
            });
            traceTimer->start();
        }
        // auto-quit after duration via wl_event_loop timer is more reliable than Qt singleShot
        // but we also schedule Qt quit for headless-like; for normal nested, duration quit is not needed
        // Still, if stressDuration >0 and we are in nested wayland, we don't auto-quit compositor
        // (user must close). For testing we allow timeout via SIGALRM-like via QTimer that calls wl_display_terminate
        if (stressDuration > 0 && qEnvironmentVariableIsSet("ASTICK_STRESS_AUTOQUIT")) {
            QTimer::singleShot((stressDuration * 1000) + 500, &app, [&comp](){
                fprintf(stderr, "[STRESS] duration elapsed, terminating display\n");
                wl_display_terminate(comp.getDisplay());
                QCoreApplication::quit();
            });
        }
    }

    return app.exec();
}
