#include "test_runner.h"
#include <QDebug>
#include <QElapsedTimer>
#include <cstdio>

TestRunner &TestRunner::instance() {
    static TestRunner inst;
    return inst;
}

void TestRunner::add(TestCase tc) {
    cases.append(std::move(tc));
}

void TestRunner::clear() {
    cases.clear();
}

int TestRunner::count() const {
    return cases.size();
}

QList<TestResult> TestRunner::runAll() {
    QList<TestResult> results;
    results.reserve(cases.size());
    for (const auto &tc : cases) {
        if (!tc.enabled) continue;
        fprintf(stderr, "[TEST] RUNNING %s\n", tc.name.toUtf8().constData());
        fflush(stderr);
        QElapsedTimer t;
        t.start();
        QString detail;
        bool ok = false;
        try {
            ok = tc.run(&detail);
        } catch (const std::exception &e) {
            ok = false;
            detail = QString::fromUtf8(e.what());
        } catch (...) {
            ok = false;
            detail = QStringLiteral("unknown exception");
        }
        qint64 elapsed = t.elapsed();
        TestResult r;
        r.name = tc.name;
        r.passed = ok;
        r.detail = detail;
        r.elapsedMs = elapsed;
        if (ok) {
            fprintf(stderr, "[TEST] PASS   %s (%lld ms)\n", tc.name.toUtf8().constData(), (long long)elapsed);
        } else {
            if (detail.isEmpty())
                fprintf(stderr, "[TEST] FAIL   %s (%lld ms)\n", tc.name.toUtf8().constData(), (long long)elapsed);
            else
                fprintf(stderr, "[TEST] FAIL   %s (%lld ms) — %s\n", tc.name.toUtf8().constData(), (long long)elapsed, detail.toUtf8().constData());
        }
        fflush(stderr);
        results.append(std::move(r));
    }
    return results;
}

void TestRunner::printResults(const QList<TestResult> &results) {
    int passed = 0, failed = 0;
    for (const auto &r : results) {
        if (r.passed) passed++; else failed++;
    }
    fprintf(stderr, "[TEST] SUMMARY %d ran, %d passed, %d failed\n", (int)results.size(), passed, failed);
    fflush(stderr);
}

__attribute__((weak)) void registerResourceTests() {}
__attribute__((weak)) void registerLayoutTests() {}
__attribute__((weak)) void registerConfigTests() {}
__attribute__((weak)) void registerAnimationTests() {}
__attribute__((weak)) void registerStateDumpTests() {}
__attribute__((weak)) void registerUtilTests() {}

void registerAllTests() {
    registerResourceTests();
    registerLayoutTests();
    registerConfigTests();
    registerAnimationTests();
    registerStateDumpTests();
    registerUtilTests();
}
