#include "resource.h"
#include "wlroots.h"
#include "util.h"
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QDebug>

QHash<uint64_t, uint64_t> Resource::s_nextId;
bool Resource::s_loaded = false;

Resource::Resource(QObject *parent) : QObject(parent) {}
Resource::~Resource() {
    emit resourceDestroyed(this);
}

uint64_t Resource::hashCombine(uint64_t a, uint64_t b) {
    a ^= b + 0x9e3779b97f4a7c15ULL + (a<<6) + (a>>2);
    return a;
}

QString Resource::sessionPath() {
    QString cfgDir = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    if (cfgDir.isEmpty()) {
        const char *xdg = getenv("XDG_CONFIG_HOME");
        if (xdg && *xdg)
            cfgDir = QString::fromUtf8(xdg);
        else {
            const char *home = getenv("HOME");
            if (home)
                cfgDir = QString::fromUtf8(home) + "/.config";
            else cfgDir = QStringLiteral(".");
        }
    }
    QDir d(cfgDir);
    return d.filePath("Astick/session.json");
}

void Resource::loadNextIds() {
    s_nextId.reserve(16);
    s_loaded = true;
    QString path = sessionPath();
    QFile f(path);
    if (!f.exists()
        ) return;
    if (!f.open(QIODevice::ReadOnly)) {
        wlr_log(WLR_ERROR, "resource.load loadNextIds failed to open %s: %s", path.toUtf8().constData(), f.errorString().toUtf8().constData());
        return;
    }
    QByteArray data = f.readAll();
    f.close();
    QJsonParseError err{};
    QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        wlr_log(WLR_ERROR, "resource.load session.json parse error %s", err.errorString().toUtf8().constData());
        return;
    }
    QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        bool okBase=false, okVal=false;
        uint64_t base = it.key().toULongLong(&okBase);
        uint64_t val = 0;
        if (it.value()
            .isDouble()) val = (uint64_t)it.value().toDouble();
        else if (it.value().isString()) val = it.value().toString().toULongLong(&okVal);
        else val = (uint64_t)it.value().toInt();
        if (okBase) {
            if (!okVal && it.value().isString()) { /* already tried */ }
            s_nextId.insert(base, val);
        }
    }
}

void Resource::saveNextIds() {
    QString path = sessionPath();
    QDir dir = QFileInfo(path).dir();
    if (!dir.exists()) {
        if (!dir.mkpath(".")) {
            wlr_log(WLR_ERROR, "resource.save saveNextIds mkpath failed %s", dir.path().toUtf8().constData());
            return;
        }
    }
    QJsonObject obj;
    for (auto it = s_nextId.begin(); it != s_nextId.end(); ++it) {
        obj.insert(QString::number(it.key()), (double)it.value());
    }
    QJsonDocument doc(obj);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        wlr_log(WLR_ERROR, "resource.save saveNextIds open failed %s: %s", path.toUtf8().constData(), f.errorString().toUtf8().constData());
        return;
    }
    f.write(doc.toJson(QJsonDocument::Compact));
    f.close();
}

void Resource::resetForTests() {
    s_nextId.clear();
    s_nextId.reserve(16);
    s_loaded = true;
}

uint64_t Resource::allocateId(uint64_t base) {
    if (!s_loaded)
        loadNextIds();
    if (s_nextId.capacity()
        == 0) s_nextId.reserve(16);
    uint64_t counter = s_nextId.value(base, 0);
    uint64_t id = base + (counter % ResourceKind::CountPerKind);
    counter++;
    s_nextId.insert(base, counter);
    if ((counter % ResourceKind::CountPerKind) == 0 && counter != 0) {
        wlr_log(WLR_ERROR, "resource.id ID wrap for base %lu counter %lu", (unsigned long)base, (unsigned long)counter);
    }
    saveNextIds();
    return id;
}

uint64_t Resource::genId() {
    return allocateId(9001);
}

void Resource::generateId() {
    id = genId();
}
