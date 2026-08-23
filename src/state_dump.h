#pragma once
#include <QJsonObject>
#include <QJsonDocument>
#include <QString>

class Compositor;
class LayoutManager;
struct wlr_box;

class StateDumper {
public:
    static QJsonObject snapshot(Compositor *comp);
    static QJsonObject snapshotBspNode(void *node, int depth = 0);
    static QString toJsonString(const QJsonObject &obj);
    static uint64_t hashState(const QJsonObject &obj);
};
