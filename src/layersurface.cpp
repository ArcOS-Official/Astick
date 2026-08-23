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

#include "layersurface.h"
#include "engine/engine.h"
#include "util.h"

void handle_layer_map(wl_listener *listener, void *)
{
    LayerSurface *self = wl_container_of(listener, self, map);
    struct wlr_layer_surface_v1 *layer = self->layer;
    if (layer->current.keyboard_interactive !=
            ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE) {
        struct wlr_seat *seat = self->server->getSeat();
        struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
        if (keyboard != nullptr) {
            wlr_seat_keyboard_notify_enter(seat, layer->surface,
                keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
        }
    }
    self->configure();
    emit self->mapped();
}

void handle_layer_unmap(wl_listener *listener, void *)
{
    LayerSurface *self = wl_container_of(listener, self, unmap);
    emit self->unmapped();
}

void handle_layer_commit(wl_listener *listener, void *)
{
    LayerSurface *self = wl_container_of(listener, self, commit);
    self->configure();
    emit self->committed();
}

void handle_layer_destroy(wl_listener *listener, void *)
{
    LayerSurface *self = wl_container_of(listener, self, destroy);
    wl_list_remove(&self->map.link);
    wl_list_remove(&self->unmap.link);
    wl_list_remove(&self->commit.link);
    wl_list_remove(&self->destroy.link);
    emit self->destroyed();
    delete self;
}

void LayerSurface::configure()
{
    if (layer->output == nullptr) return;

    struct wlr_scene_tree *target = server->getLayerTree(layer->current.layer);
    if (sceneLayer->tree->node.parent != target) {
        wlr_scene_node_reparent(&sceneLayer->tree->node, target);
    }

    struct wlr_output *wlr_out = layer->output;
    struct wlr_box full_area;
    wlr_output_layout_get_box(server->getOutputLayout(), wlr_out, &full_area);
    struct wlr_box usable_area = full_area;
    wlr_scene_layer_surface_v1_configure(sceneLayer, &full_area, &usable_area);
}

uint64_t LayerSurface::genId() {
    uint64_t h = (uint64_t)(uintptr_t)layer;
    return ResourceKind::LayerBase + (h % ResourceKind::CountPerKind);
}

LayerSurface::LayerSurface(
    astick::Engine *server_,
    struct wlr_layer_surface_v1 *surface
)
{
    generateId();
    server = server_;
    layer = surface;

    struct wlr_scene_tree *parent = server->getLayerTree(layer->current.layer);
    sceneLayer = wlr_scene_layer_surface_v1_create(parent, surface);
    // Expose the scene tree via layer->data so popups parented to this
    // layer surface can find their scene parent (mirrors Toplevel's
    // toplevel->base->data = sceneTree).
    layer->data = sceneLayer->tree;

    signal(map, &layer->surface->events.map, handle_layer_map);
    signal(unmap, &layer->surface->events.unmap, handle_layer_unmap);
    signal(commit, &layer->surface->events.commit, handle_layer_commit);
    signal(destroy, &layer->events.destroy, handle_layer_destroy);
}