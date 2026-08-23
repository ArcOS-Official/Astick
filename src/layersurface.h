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

class LayerSurface : public Resource
{
    Q_OBJECT
public:
    uint64_t genId() override;
    LayerSurface(
        astick::Engine *server,
        struct wlr_layer_surface_v1 *surface
    );
    struct wlr_layer_surface_v1 *get() const { return layer; }
    struct wlr_scene_layer_surface_v1 *getSceneLayer() const { return sceneLayer; }
    void configure();

    friend void handle_layer_map(wl_listener *listener, void *data);
    friend void handle_layer_unmap(wl_listener *listener, void *data);
    friend void handle_layer_commit(wl_listener *listener, void *data);
    friend void handle_layer_destroy(wl_listener *listener, void *data);

signals:
    void mapped();
    void unmapped();
    void committed();
    void destroyed();

private:
    astick::Engine *server;
    struct wlr_layer_surface_v1 *layer;
    struct wlr_scene_layer_surface_v1 *sceneLayer;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
};