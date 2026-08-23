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

#include "resource.h"
#include "wlroots.h"

namespace astick { class Engine; }

class Toplevel : public Resource
{
    Q_OBJECT

public:
    Toplevel(
        astick::Engine *server,
        struct wlr_xdg_toplevel *toplevel,
        struct wlr_scene_tree *sceneTree
    );
    struct wlr_xdg_toplevel *get() const { return toplevel; }
    struct wlr_scene_tree *getSceneTree() const { return sceneTree; }
    astick::Engine *getServer() const { return server; }

    uint64_t genId() override;

    // Animatable geometry (for new AnimationPool target-driven flow)
    int animX = 0, animY = 0, animW = 0, animH = 0;
    void setTargetBox(const struct wlr_box &target, const std::string &kind, std::function<void()> apply = nullptr);

    // Close animation snapshot: keep last buffer visible while animating
    struct wlr_scene_buffer *closeSnapshot = nullptr;
    struct wlr_buffer *closeBuffer = nullptr;
    bool closeAnimationRunning = false;
    bool pendingDestroy = false;
    float animOpacity = 1.0f;
    void createCloseSnapshot();
    void destroyCloseSnapshot();
    void destroy_cb(QObject *);

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
    astick::Engine *server;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
};
