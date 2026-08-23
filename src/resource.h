#pragma once
#include <QObject>
#include <QHash>
#include <QString>
#include <cstdint>

class Resource : public QObject {
    Q_OBJECT
public:
    uint64_t id = 0;

    explicit Resource(QObject *parent = nullptr);
    virtual ~Resource();

    virtual uint64_t genId();
    void generateId();

signals:
    void resourceDestroyed(Resource *self);

protected:
    static uint64_t hashCombine(uint64_t a, uint64_t b);
    static uint64_t allocateId(uint64_t base);

private:
    static QHash<uint64_t, uint64_t> s_nextId;
    static bool s_loaded;

public:
    static void resetForTests();
    static void loadNextIds();
    static void saveNextIds();
    static QString sessionPath();
};

// Helper to get kind base offsets (approved ranges)
namespace ResourceKind {
    constexpr uint64_t WindowBase = 1;
    constexpr uint64_t WorkspaceBase = 1001;
    constexpr uint64_t OutputBase = 2001;
    constexpr uint64_t InputBase = 3001;
    constexpr uint64_t PopupBase = 4001;
    constexpr uint64_t LayerBase = 5001;
    constexpr uint64_t AnimationBase = 6001;
    constexpr uint64_t DecorBase = 7001;
    constexpr uint64_t CountPerKind = 1000;
}
