#pragma once

#include <QObject>
#include <qobject.h>
#include "wlroots.h"

class Compositor;

class Toplevel : public QObject
{
    Q_OBJECT

public:
    Toplevel(
        Compositor *server,
        struct wlr_xdg_toplevel *toplevel,
        struct wlr_scene_tree *sceneTree
    );
    struct wlr_xdg_toplevel *get() const { return toplevel; }
    struct wlr_scene_tree *getSceneTree() const { return sceneTree; }
    Compositor *getServer() const { return server; }

    friend void handle_map(wl_listener *listener, void *data);
    friend void handle_unmap(wl_listener *listener, void *data);
    friend void handle_commit(wl_listener *listener, void *data);
    friend void handle_destroy(wl_listener *listener, void *data);
    friend void handle_request_move(wl_listener *listener, void *data);
    friend void handle_request_resize(wl_listener *listener, void *data);
    friend void handle_request_maximize(wl_listener *listener, void *data);
    friend void handle_request_fullscreen(wl_listener *listener, void *data);

signals:
    void mapped();
    void unmapped();
    void committed();
    void destroyed();
    void moveRequested();
    void resizeRequested(uint32_t edges);
    void maximizeRequested();
    void fullscreenRequested();

private:
    struct wlr_xdg_toplevel *toplevel;
    struct wlr_scene_tree *sceneTree;
    Compositor *server;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
};