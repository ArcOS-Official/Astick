#pragma once
#include <QString>
#include <QList>
#include <functional>

struct TestCase {
    QString name;
    std::function<bool(QString *outDetail)> run;
    bool enabled = true;
};

struct TestResult {
    QString name;
    bool passed = false;
    QString detail;
    qint64 elapsedMs = 0;
};

class TestRunner {
public:
    static TestRunner &instance();
    void add(TestCase tc);
    QList<TestResult> runAll();
    void printResults(const QList<TestResult> &results);
    void clear();
    int count() const;
private:
    QList<TestCase> cases;
};

void registerAllTests();
