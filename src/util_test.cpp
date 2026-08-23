#include "util.h"
#include "debug.h"
#include "test_runner.h"
#include <QString>

void registerUtilTests() {
    TestRunner::instance().add(TestCase{
        QStringLiteral("util.valueOrLog.int.fallback"),
        [](QString *out) -> bool {
            debugger.mutex.lock();
            debugger.errors.clear();
            debugger.mutex.unlock();
            int fallback = 42;
            int got = valueOrLog<int>(0, fallback, "util.test.int");
            if (got != fallback) {
                if (out) *out = QStringLiteral("expected fallback %1 got %2").arg(fallback).arg(got);
                return false;
            }
            // should have logged
            {
                QMutexLocker lock(&debugger.mutex);
                if (debugger.errors.isEmpty()) {
                    if (out) *out = QStringLiteral("no error logged for invalid int");
                    return false;
                }
                if (!debugger.errors.last().contains("util.test.int")) {
                    if (out) *out = QStringLiteral("logged ctx missing: %1").arg(debugger.errors.last());
                    return false;
                }
            }
            // valid path no fallback
            debugger.mutex.lock();
            debugger.errors.clear();
            debugger.mutex.unlock();
            int valid = valueOrLog<int>(7, fallback, "util.test.int2");
            if (valid != 7) {
                if (out) *out = QStringLiteral("valid value should not fallback, got %1").arg(valid);
                return false;
            }
            {
                QMutexLocker lock(&debugger.mutex);
                if (!debugger.errors.isEmpty()) {
                    if (out) *out = QStringLiteral("valid value should not log, got %1").arg(debugger.errors.last());
                    return false;
                }
            }
            return true;
        }
    });

    TestRunner::instance().add(TestCase{
        QStringLiteral("util.valueOrLog.pointer.fallback"),
        [](QString *out) -> bool {
            debugger.mutex.lock();
            debugger.errors.clear();
            debugger.mutex.unlock();
            int x = 1;
            int *pNull = nullptr;
            int *fallback = &x;
            int *got = valueOrLog<int*>(pNull, fallback, "util.test.ptr");
            if (got != fallback) {
                if (out) *out = QStringLiteral("null pointer should return fallback");
                return false;
            }
            // non-null returns original
            debugger.mutex.lock();
            debugger.errors.clear();
            debugger.mutex.unlock();
            int y = 2;
            int *pValid = &y;
            int *got2 = valueOrLog<int*>(pValid, fallback, "util.test.ptr2");
            if (got2 != pValid) {
                if (out) *out = QStringLiteral("valid pointer should not fallback");
                return false;
            }
            {
                QMutexLocker lock(&debugger.mutex);
                if (!debugger.errors.isEmpty()) {
                    if (out) *out = QStringLiteral("valid pointer should not log");
                    return false;
                }
            }
            return true;
        }
    });

    TestRunner::instance().add(TestCase{
        QStringLiteral("util.logIf.behavior"),
        [](QString *out) -> bool {
            debugger.mutex.lock();
            debugger.errors.clear();
            debugger.mutex.unlock();
            bool okTrue = logIf(true, "util.test.logIf", "should not log %d", 1);
            if (!okTrue) {
                if (out) *out = QStringLiteral("logIf(true) should return true");
                return false;
            }
            {
                QMutexLocker lock(&debugger.mutex);
                if (!debugger.errors.isEmpty()) {
                    if (out) *out = QStringLiteral("logIf(true) should not log");
                    return false;
                }
            }
            bool okFalse = logIf(false, "util.test.logIf", "failed %s", "detail");
            if (okFalse) {
                if (out) *out = QStringLiteral("logIf(false) should return false");
                return false;
            }
            {
                QMutexLocker lock(&debugger.mutex);
                if (debugger.errors.isEmpty()) {
                    if (out) *out = QStringLiteral("logIf(false) should log");
                    return false;
                }
                if (!debugger.errors.last().contains("util.test.logIf")) {
                    if (out) *out = QStringLiteral("logIf ctx missing: %1").arg(debugger.errors.last());
                    return false;
                }
            }
            return true;
        }
    });

    TestRunner::instance().add(TestCase{
        QStringLiteral("util.logAndContinue.append"),
        [](QString *out) -> bool {
            debugger.mutex.lock();
            debugger.errors.clear();
            debugger.mutex.unlock();
            logAndContinue(QStringLiteral("util.test.ctx"), QStringLiteral("detail message"));
            QMutexLocker lock(&debugger.mutex);
            if (debugger.errors.isEmpty()) {
                if (out) *out = QStringLiteral("logAndContinue should append to debugger.errors");
                return false;
            }
            const QString last = debugger.errors.last();
            if (!last.contains("util.test.ctx") || !last.contains("detail message")) {
                if (out) *out = QStringLiteral("logged message incorrect: %1").arg(last);
                return false;
            }
            return true;
        }
    });

    TestRunner::instance().add(TestCase{
        QStringLiteral("util.valueOrLog.double.fallback"),
        [](QString *out) -> bool {
            debugger.mutex.lock();
            debugger.errors.clear();
            debugger.mutex.unlock();
            double got = valueOrLog<double>(0.0, 3.14, "util.test.double");
            if (got != 3.14) {
                if (out) *out = QStringLiteral("double 0 should fallback to 3.14 got %1").arg(got);
                return false;
            }
            debugger.mutex.lock();
            debugger.errors.clear();
            debugger.mutex.unlock();
            double got2 = valueOrLog<double>(2.71, 3.14, "util.test.double2");
            if (got2 != 2.71) {
                if (out) *out = QStringLiteral("valid double should not fallback");
                return false;
            }
            return true;
        }
    });
}
