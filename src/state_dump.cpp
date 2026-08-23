#include "state_dump.h"
#include "compositor.h"
#include "layout.h"
#include "toplevel.h"
#include "output.h"
#include "config.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QCryptographicHash>

QJsonObject StateDumper::snapshot(Compositor *comp) {
    QJsonObject root;
    if (!comp) return root;
    root["t"] = QDateTime::currentMSecsSinceEpoch();
    QJsonObject compObj;
    compObj["outputs"] = (int)comp->getOutputs().size();
    compObj["toplevels"] = (int)comp->getToplevels().size();
    compObj["popups"] = 0;
    root["compositor"] = compObj;

    QJsonArray outputsArr;
    for (auto *out : comp->getOutputs()) {
        QJsonObject o;
        o["id"] = (qint64)out->id;
        o["workspace"] = out->getWorkspace();
        auto *wout = out->get();
        if (wout) {
            o["name"] = wout->name ? QString::fromUtf8(wout->name) : QStringLiteral("unknown");
            QJsonObject box;
            box["x"] = 0; box["y"] = 0;
            box["w"] = wout->width;
            box["h"] = wout->height;
            o["box"] = box;
            o["scale"] = wout->scale;
        }
        outputsArr.append(o);
    }
    root["outputs"] = outputsArr;

    if (auto *layout = comp->getLayout()) {
        QJsonArray wsArr;
        // Iterate workspaces via known API: we snapshot via layout's public methods where possible
        // For now, dump minimal workspace info
        for (int wsId = 1; wsId < 10; ++wsId) {
            auto mode = layout->getWorkspaceLayoutMode(wsId);
            int cnt = layout->tiledCount(wsId) + layout->floatingCount(wsId);
            if (cnt==0 && wsId!=1) continue;
            QJsonObject w;
            w["id"] = wsId;
            QString modeStr = (mode==LayoutManager::Mode::Tiling)?"Tiling":(mode==LayoutManager::Mode::Floating)?"Floating":"MonoWindow";
            w["mode"] = modeStr;
            w["count"] = cnt;
            auto geoms = layout->snapshotGeometries(wsId);
            QJsonArray boxes;
            for (auto &kv : geoms) {
                QJsonObject b;
                b["id"] = (qint64)kv.first->id;
                b["x"] = kv.second.x;
                b["y"] = kv.second.y;
                b["w"] = kv.second.width;
                b["h"] = kv.second.height;
                boxes.append(b);
            }
            w["boxes"] = boxes;
            wsArr.append(w);
        }
        root["workspaces"] = wsArr;
    }

    QJsonArray wins;
    for (auto *tl : comp->getToplevels()) {
        QJsonObject w;
        w["id"] = (qint64)tl->id;
        w["mapped"] = tl->get()->base->surface->mapped;
        if (tl->get()->title) w["title"] = QString::fromUtf8(tl->get()->title);
        if (tl->getSceneTree()) {
            QJsonObject box;
            box["x"] = tl->getSceneTree()->node.x;
            box["y"] = tl->getSceneTree()->node.y;
            box["w"] = tl->get()->base->geometry.width;
            box["h"] = tl->get()->base->geometry.height;
            w["box"] = box;
        }
        wins.append(w);
    }
    root["windows"] = wins;

    if (auto *cfg = comp->getConfig()) {
        QJsonObject cfgObj;
        cfgObj["modkey"] = QString::fromStdString(cfg->modkey);
        root["config"] = cfgObj;
    }
    return root;
}

QString StateDumper::toJsonString(const QJsonObject &obj) {
    QJsonDocument doc(obj);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

uint64_t StateDumper::hashState(const QJsonObject &obj) {
    auto s = toJsonString(obj);
    auto h = QCryptographicHash::hash(s.toUtf8(), QCryptographicHash::Sha256);
    uint64_t v = 0;
    for (int i=0;i<8 && i<h.size();++i) v = (v<<8) | (uint8_t)h[i];
    return v;
}

QJsonObject StateDumper::snapshotBspNode(void *node, int depth) {
    Q_UNUSED(node); Q_UNUSED(depth);
    return QJsonObject();
}
