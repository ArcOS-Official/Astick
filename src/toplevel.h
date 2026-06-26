/*
 *  Astick, the wayland compositor for ArcDE.
 *  Copyright (C) 2026 Eyad Ahmed Ragheb

 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

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
