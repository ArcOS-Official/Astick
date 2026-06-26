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

#include "cursor.h"
#include "toplevel.h"
#include "compositor.h"
#include "util.h"

void cursor_handle_motion(wl_listener *listener, void *data)
{
    CursorManager *self = wl_container_of(listener, self, motionListener);
    struct wlr_pointer_motion_event *event = (struct wlr_pointer_motion_event *)data;
    Compositor *comp = self->compositor;
    wlr_cursor_move(comp->getCursor(), &event->pointer->base,
        event->delta_x, event->delta_y);
    self->processMotion(event->time_msec);
}

void cursor_handle_motion_absolute(wl_listener *listener, void *data)
{
    CursorManager *self = wl_container_of(listener, self, motionAbsoluteListener);
    struct wlr_pointer_motion_absolute_event *event =
        (struct wlr_pointer_motion_absolute_event *)data;
    Compositor *comp = self->compositor;
    wlr_cursor_warp_absolute(comp->getCursor(), &event->pointer->base,
        event->x, event->y);
    self->processMotion(event->time_msec);
}

void cursor_handle_button(wl_listener *listener, void *data)
{
    CursorManager *self = wl_container_of(listener, self, buttonListener);
    struct wlr_pointer_button_event *event = (struct wlr_pointer_button_event *)data;
    Compositor *comp = self->compositor;

    wlr_seat_pointer_notify_button(comp->getSeat(),
        event->time_msec, event->button, event->state);

    if (event->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        self->resetMode();
    } else {
        double sx, sy;
        struct wlr_surface *surface = nullptr;
        Toplevel *toplevel = self->toplevelAt(
            comp->getCursor()->x, comp->getCursor()->y, &surface, &sx, &sy);
        comp->focusToplevel(toplevel);
    }
}

void cursor_handle_axis(wl_listener *listener, void *data)
{
    CursorManager *self = wl_container_of(listener, self, axisListener);
    struct wlr_pointer_axis_event *event = (struct wlr_pointer_axis_event *)data;
    Compositor *comp = self->compositor;
    wlr_seat_pointer_notify_axis(comp->getSeat(), event->time_msec,
        event->orientation, event->delta, event->delta_discrete,
        event->source, event->relative_direction);
}

void cursor_handle_frame(wl_listener *listener, void *)
{
    CursorManager *self = wl_container_of(listener, self, frameListener);
    wlr_seat_pointer_notify_frame(self->compositor->getSeat());
}

void cursor_handle_request_cursor(wl_listener *listener, void *data)
{
    CursorManager *self = wl_container_of(listener, self, requestCursorListener);
    struct wlr_seat_pointer_request_set_cursor_event *event =
        (struct wlr_seat_pointer_request_set_cursor_event *)data;
    Compositor *comp = self->compositor;
    struct wlr_seat_client *focused = comp->getSeat()->pointer_state.focused_client;
    if (focused == event->seat_client) {
        wlr_cursor_set_surface(comp->getCursor(), event->surface,
            event->hotspot_x, event->hotspot_y);
    }
}

CursorManager::CursorManager(Compositor *comp)
{
    compositor = comp;
    signal(motionListener, &compositor->getCursor()->events.motion, cursor_handle_motion);
    signal(motionAbsoluteListener, &compositor->getCursor()->events.motion_absolute, cursor_handle_motion_absolute);
    signal(buttonListener, &compositor->getCursor()->events.button, cursor_handle_button);
    signal(axisListener, &compositor->getCursor()->events.axis, cursor_handle_axis);
    signal(frameListener, &compositor->getCursor()->events.frame, cursor_handle_frame);
    signal(requestCursorListener, &compositor->getSeat()->events.request_set_cursor, cursor_handle_request_cursor);
}

CursorManager::~CursorManager()
{
    wl_list_remove(&motionListener.link);
    wl_list_remove(&motionAbsoluteListener.link);
    wl_list_remove(&buttonListener.link);
    wl_list_remove(&axisListener.link);
    wl_list_remove(&frameListener.link);
    wl_list_remove(&requestCursorListener.link);
}

Toplevel *CursorManager::toplevelAt(double lx, double ly,
    struct wlr_surface **surface, double *sx, double *sy)
{
    struct wlr_scene_node *node = wlr_scene_node_at(
        &compositor->getScene()->tree.node, lx, ly, sx, sy);
    if (node == nullptr || node->type != WLR_SCENE_NODE_BUFFER) {
        return nullptr;
    }
    struct wlr_scene_buffer *scene_buffer = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *scene_surface =
        wlr_scene_surface_try_from_buffer(scene_buffer);
    if (!scene_surface) {
        return nullptr;
    }
    *surface = scene_surface->surface;

    struct wlr_scene_tree *tree = node->parent;
    while (tree != nullptr && tree->node.data == nullptr) {
        tree = tree->node.parent;
    }
    return (Toplevel *)(tree ? tree->node.data : nullptr);
}

void CursorManager::processMotion(uint32_t time)
{
    if (cursorMode == CURSOR_MOVE) {
        processMove();
        return;
    } else if (cursorMode == CURSOR_RESIZE) {
        processResize();
        return;
    }

    struct wlr_cursor *cursor = compositor->getCursor();
    struct wlr_seat *seat = compositor->getSeat();

    double sx, sy;
    struct wlr_surface *surface = nullptr;
    Toplevel *toplevel = toplevelAt(cursor->x, cursor->y, &surface, &sx, &sy);
    if (!toplevel) {
        wlr_cursor_set_xcursor(cursor, compositor->getCursorMgr(), "default");
    }
    if (surface) {
        wlr_seat_pointer_notify_enter(seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(seat);
    }
}

void CursorManager::processMove()
{
    Toplevel *toplevel = grabbedToplevel;
    struct wlr_cursor *cursor = compositor->getCursor();
    wlr_scene_node_set_position(&toplevel->getSceneTree()->node,
        cursor->x - grabX, cursor->y - grabY);
}

void CursorManager::processResize()
{
    Toplevel *toplevel = grabbedToplevel;
    struct wlr_cursor *cursor = compositor->getCursor();
    double border_x = cursor->x - grabX;
    double border_y = cursor->y - grabY;

    int new_left = grabGeobox.x;
    int new_right = grabGeobox.x + grabGeobox.width;
    int new_top = grabGeobox.y;
    int new_bottom = grabGeobox.y + grabGeobox.height;

    if (resizeEdges & WLR_EDGE_TOP) {
        new_top = border_y;
        if (new_top >= new_bottom) new_top = new_bottom - 1;
    } else if (resizeEdges & WLR_EDGE_BOTTOM) {
        new_bottom = border_y;
        if (new_bottom <= new_top) new_bottom = new_top + 1;
    }
    if (resizeEdges & WLR_EDGE_LEFT) {
        new_left = border_x;
        if (new_left >= new_right) new_left = new_right - 1;
    } else if (resizeEdges & WLR_EDGE_RIGHT) {
        new_right = border_x;
        if (new_right <= new_left) new_right = new_left + 1;
    }

    struct wlr_box *geo_box = &toplevel->get()->base->geometry;
    wlr_scene_node_set_position(&toplevel->getSceneTree()->node,
        new_left - geo_box->x, new_top - geo_box->y);

    int new_width = new_right - new_left;
    int new_height = new_bottom - new_top;
    wlr_xdg_toplevel_set_size(toplevel->get(), new_width, new_height);
}

void CursorManager::resetMode()
{
    cursorMode = CURSOR_PASSTHROUGH;
    grabbedToplevel = nullptr;
}

void CursorManager::beginInteractive(Toplevel *toplevel, CursorMode mode, uint32_t edges)
{
    struct wlr_cursor *cursor = compositor->getCursor();
    grabbedToplevel = toplevel;
    cursorMode = mode;

    if (mode == CURSOR_MOVE) {
        grabX = cursor->x - toplevel->getSceneTree()->node.x;
        grabY = cursor->y - toplevel->getSceneTree()->node.y;
    } else {
        struct wlr_box *geo_box = &toplevel->get()->base->geometry;

        double border_x = (toplevel->getSceneTree()->node.x + geo_box->x) +
            ((edges & WLR_EDGE_RIGHT) ? geo_box->width : 0);
        double border_y = (toplevel->getSceneTree()->node.y + geo_box->y) +
            ((edges & WLR_EDGE_BOTTOM) ? geo_box->height : 0);
        grabX = cursor->x - border_x;
        grabY = cursor->y - border_y;

        grabGeobox = *geo_box;
        grabGeobox.x += toplevel->getSceneTree()->node.x;
        grabGeobox.y += toplevel->getSceneTree()->node.y;

        resizeEdges = edges;
    }
}
