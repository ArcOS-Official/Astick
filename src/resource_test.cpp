#include "resource.h"
#include "test_runner.h"
#include <QString>

// Helper resources for testing deterministic IDs without needing wlroots objects
struct TestWindowResource : public Resource {
    uint64_t genId() override { return allocateId(ResourceKind::WindowBase); }
};
struct TestOutputResource : public Resource {
    uint64_t genId() override { return allocateId(ResourceKind::OutputBase); }
};
struct TestGenericResource : public Resource {
    // uses default base 9001 via Resource::genId
};

struct TitledWindowResource : public Resource {
    QString title;
    explicit TitledWindowResource(const QString &t) : title(t) {}
    uint64_t genId() override { (void)title; return allocateId(ResourceKind::WindowBase); }
};

void registerResourceTests() {
    TestRunner::instance().add(TestCase{
        QStringLiteral("resource.id.monotonic"),
        [](QString *out) -> bool {
            Resource::resetForTests();
            TestGenericResource r1, r2, r3;
            r1.generateId();
            r2.generateId();
            r3.generateId();
            if (r2.id != r1.id + 1) {
                if (out) *out = QStringLiteral("generic monotonic fail: %1 -> %2 expected +1").arg(r1.id).arg(r2.id);
                return false;
            }
            if (r3.id != r2.id + 1) {
                if (out) *out = QStringLiteral("generic monotonic fail2: %1 -> %2").arg(r2.id).arg(r3.id);
                return false;
            }
            if (r1.id < 9001 || r1.id >= 9001 + ResourceKind::CountPerKind) {
                if (out) *out = QStringLiteral("generic base range violated: %1").arg(r1.id);
                return false;
            }
            // per-kind isolation: Window vs Output separate counters
            Resource::resetForTests();
            TestWindowResource w1, w2;
            TestOutputResource o1;
            w1.generateId();
            w2.generateId();
            o1.generateId();
            if (w1.id != ResourceKind::WindowBase) {
                if (out) *out = QStringLiteral("window base start expected %1 got %2").arg(ResourceKind::WindowBase).arg(w1.id);
                return false;
            }
            if (w2.id != ResourceKind::WindowBase + 1) {
                if (out) *out = QStringLiteral("window monotonic expected %1 got %2").arg(ResourceKind::WindowBase+1).arg(w2.id);
                return false;
            }
            if (o1.id != ResourceKind::OutputBase) {
                if (out) *out = QStringLiteral("output base start expected %1 got %2").arg(ResourceKind::OutputBase).arg(o1.id);
                return false;
            }
            // after reset counters restart at base
            Resource::resetForTests();
            TestWindowResource w3;
            w3.generateId();
            if (w3.id != ResourceKind::WindowBase) {
                if (out) *out = QStringLiteral("reset failed: expected %1 got %2").arg(ResourceKind::WindowBase).arg(w3.id);
                return false;
            }
            return true;
        }
    });

    TestRunner::instance().add(TestCase{
        QStringLiteral("resource.id.no_title_hash"),
        [](QString *out) -> bool {
            Resource::resetForTests();
            TitledWindowResource a(QStringLiteral("Alpha"));
            TitledWindowResource b(QStringLiteral("Beta Different Title That Would Have Hashed Differently Before"));
            a.generateId();
            b.generateId();
            // Should be sequential because IDs are deterministic per-kind, not title-hashed
            if (b.id != a.id + 1) {
                if (out) *out = QStringLiteral("title should not affect id: %1 vs %2 diff %3").arg(a.id).arg(b.id).arg((long long)(b.id - a.id));
                return false;
            }
            // Another pair with drastically different titles
            Resource::resetForTests();
            TitledWindowResource c(QStringLiteral(""));
            TitledWindowResource d(QStringLiteral("VeryLongTitle_With_Special_Chars_!@#$%^&*()_1234567890"));
            c.generateId();
            d.generateId();
            if (d.id != c.id + 1) {
                if (out) *out = QStringLiteral("empty vs long title sequential fail: %1 %2").arg(c.id).arg(d.id);
                return false;
            }
            if (c.id != ResourceKind::WindowBase) {
                if (out) *out = QStringLiteral("expected WindowBase %1 got %2").arg(ResourceKind::WindowBase).arg(c.id);
                return false;
            }
            return true;
        }
    });
}
