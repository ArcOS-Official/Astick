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
#include "compositor.h"
#include "util.h"

void handle_map(wl_listener *listener, void *)
{
    Toplevel *self = wl_container_of(listener, self, map);
    emit self->mapped();
}

void handle_unmap(wl_listener *listener, void *)
{
    Toplevel *self = wl_container_of(listener, self, unmap);
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
    wl_list_remove(&self->map.link);
    wl_list_remove(&self->unmap.link);
    wl_list_remove(&self->commit.link);
    wl_list_remove(&self->destroy.link);
    wl_list_remove(&self->request_move.link);
    wl_list_remove(&self->request_resize.link);
    wl_list_remove(&self->request_maximize.link);
    wl_list_remove(&self->request_fullscreen.link);
    emit self->destroyed();
    delete self;
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

Toplevel::Toplevel(
    Compositor *server_,
    struct wlr_xdg_toplevel *toplevel_,
    struct wlr_scene_tree *sceneTree_
)
{
    server = server_;
    toplevel = toplevel_;
    sceneTree = sceneTree_;
    sceneTree->node.data = this;
    toplevel->base->data = (void *)sceneTree;

    signal(map, &toplevel->base->surface->events.map, handle_map);
    signal(unmap, &toplevel->base->surface->events.unmap, handle_unmap);
    signal(commit, &toplevel->base->surface->events.commit, handle_commit);
    signal(destroy, &toplevel->events.destroy, handle_destroy);
    signal(request_move, &toplevel->events.request_move, handle_request_move);
    signal(request_resize, &toplevel->events.request_resize, handle_request_resize);
    signal(request_maximize, &toplevel->events.request_maximize, handle_request_maximize);
    signal(request_fullscreen, &toplevel->events.request_fullscreen, handle_request_fullscreen);
}
