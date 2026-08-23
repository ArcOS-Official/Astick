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

#include "toplevel.h"
#include "engine/engine.h"
#include "util.h"

void handle_map(wl_listener *listener, void *)
{
    Toplevel *self = wl_container_of(listener, self, map);
    emit self->mapped();
}

void handle_unmap(wl_listener *listener, void *)
{
    Toplevel *self = wl_container_of(listener, self, unmap);
    wlr_log(WLR_INFO, "handle_unmap: tl %p id %lu", (void*)self, (unsigned long)self->id);
    emit self->unmapped();
}

void handle_commit(wl_listener *listener, void *)
{
    Toplevel *self = wl_container_of(listener, self, commit);
    if (self->toplevel->base->initial_commit) {
        wlr_xdg_toplevel_set_size(self->toplevel, 0, 0);
    }
    emit self->committed();
}

void handle_destroy(wl_listener *listener, void *)
{
    Toplevel *self = wl_container_of(listener, self, destroy);
    wlr_log(WLR_INFO, "handle_destroy: tl %p id %lu", (void*)self, (unsigned long)self->id);
    wl_list_remove(&self->map.link);
    wl_list_remove(&self->unmap.link);
    wl_list_remove(&self->commit.link);
    wl_list_remove(&self->destroy.link);
    wl_list_remove(&self->request_move.link);
    wl_list_remove(&self->request_resize.link);
    wl_list_remove(&self->request_maximize.link);
    wl_list_remove(&self->request_fullscreen.link);
    // Engine handles close animation via State commands; immediate destroy for now
    emit self->destroyed();
    self->destroyCloseSnapshot();
    delete self;
}

void Toplevel::destroy_cb(QObject *) {
    handle_destroy(&destroy, nullptr);
}

void handle_request_move(wl_listener *listener, void *)
{
    Toplevel *self = wl_container_of(listener, self, request_move);
    emit self->moveRequested();
}

void handle_request_resize(wl_listener *listener, void *data)
{
    Toplevel *self = wl_container_of(listener, self, request_resize);
    auto *event = (struct wlr_xdg_toplevel_resize_event *)data;
    emit self->resizeRequested(event->edges);
}

void handle_request_maximize(wl_listener *listener, void *)
{
    Toplevel *self = wl_container_of(listener, self, request_maximize);
    if (self->toplevel->base->initialized) {
        wlr_xdg_surface_schedule_configure(self->toplevel->base);
    }
    emit self->maximizeRequested();
}

void handle_request_fullscreen(wl_listener *listener, void *)
{
    Toplevel *self = wl_container_of(listener, self, request_fullscreen);
    if (self->toplevel->base->initialized) {
        wlr_xdg_surface_schedule_configure(self->toplevel->base);
    }
    emit self->fullscreenRequested();
}

uint64_t Toplevel::genId() {
    uint64_t h = (uint64_t)(uintptr_t)toplevel;
    h = hashCombine(h, (uint64_t)(uintptr_t)sceneTree);
    if (toplevel && toplevel->title) {
        for (const char *c = toplevel->title; *c; ++c) h = hashCombine(h, (uint64_t)(*c));
    }
    return ResourceKind::WindowBase + (h % ResourceKind::CountPerKind);
}

static bool close_snapshot_point_accepts_input(struct wlr_scene_buffer *buffer, double *sx, double *sy) {
    (void)buffer; (void)sx; (void)sy;
    return false;
}

void Toplevel::createCloseSnapshot() {
    if (closeSnapshot) return;
    if (!toplevel || !toplevel->base || !toplevel->base->surface) return;
    struct wlr_surface *surf = toplevel->base->surface;
    if (!surf || !surf->buffer) return;
    struct wlr_buffer *buf = &surf->buffer->base;
    wlr_buffer_lock(buf);
    closeBuffer = buf;
    // Create a scene buffer as child of sceneTree's parent, so it stays visible after xdg surface is destroyed
    struct wlr_scene_tree *parent = nullptr;
    if (sceneTree && sceneTree->node.parent) {
        // parent is wlr_scene_tree, try to cast
        parent = (struct wlr_scene_tree*)sceneTree->node.parent;
    } else {
        // fallback to sceneTree itself
        parent = sceneTree;
    }
    if (!parent) return;
    closeSnapshot = wlr_scene_buffer_create(parent, buf);
    if (closeSnapshot) {
        // Make snapshot non-interactive so pointer events fall through to windows underneath
        closeSnapshot->point_accepts_input = close_snapshot_point_accepts_input;
        // Place snapshot at same position as original
        wlr_scene_node_set_position(&closeSnapshot->node, sceneTree->node.x, sceneTree->node.y);
        wlr_scene_buffer_set_dest_size(closeSnapshot, surf->current.width, surf->current.height);
        // Keep snapshot on top of original during animation, then hide original
        // wlr_scene_node_raise_to_top(&closeSnapshot->node);
        // Optionally set opacity via filter? For fade we can use wlr_scene_buffer_set_opacity if available
    }
}

void Toplevel::destroyCloseSnapshot() {
    if (closeSnapshot) {
        wlr_scene_node_destroy(&closeSnapshot->node);
        closeSnapshot = nullptr;
    }
    if (closeBuffer) {
        wlr_buffer_unlock(closeBuffer);
        closeBuffer = nullptr;
    }
}

Toplevel::Toplevel(
    astick::Engine *server_,
    struct wlr_xdg_toplevel *toplevel_,
    struct wlr_scene_tree *sceneTree_
)
{
    server = server_;
    toplevel = toplevel_;
    sceneTree = sceneTree_;
    sceneTree->node.data = this;
    toplevel->base->data = (void *)sceneTree;
    generateId();

    signal(map, &toplevel->base->surface->events.map, handle_map);
    signal(unmap, &toplevel->base->surface->events.unmap, handle_unmap);
    signal(commit, &toplevel->base->surface->events.commit, handle_commit);
    signal(destroy, &toplevel->events.destroy, handle_destroy);
    signal(request_move, &toplevel->events.request_move, handle_request_move);
    signal(request_resize, &toplevel->events.request_resize, handle_request_resize);
    signal(request_maximize, &toplevel->events.request_maximize, handle_request_maximize);
    signal(request_fullscreen, &toplevel->events.request_fullscreen, handle_request_fullscreen);
}
