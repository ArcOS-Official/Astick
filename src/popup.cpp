#include "popup.h"
#include "compositor.h"
#include "util.h"
#include <cstdio>

void handle_popup_commit(wl_listener *listener, void *)
{
    Popup *self = wl_container_of(listener, self, commit);
    if (self->popup->base->initial_commit) {
        if (!self->server->getOutputs().isEmpty()) {
            struct wlr_output *wout = self->server->getOutputs().first()->get();
            struct wlr_box box;
            wlr_output_layout_get_box(
                self->server->getOutputLayout(), wout, &box);
            if (box.width == 0 && box.height == 0) {
                box = {0, 0, wout->width, wout->height};
            }
            wlr_xdg_popup_unconstrain_from_box(self->popup, &box);
        }
        wlr_xdg_surface_schedule_configure(self->popup->base);
    }
    emit self->committed();
}

void handle_popup_destroy(wl_listener *listener, void *)
{
    Popup *self = wl_container_of(listener, self, destroy);
    wl_list_remove(&self->commit.link);
    wl_list_remove(&self->destroy.link);
    emit self->destroyed();
    delete self;
}

struct wlr_xdg_popup *Popup::get() const { return popup; }

uint64_t Popup::genId() {
    return allocateId(ResourceKind::PopupBase);
}

Popup::Popup(
    Compositor *server_,
    struct wlr_xdg_popup *popup_
)
{
    generateId();
    server = server_;
    popup = popup_;

    struct wlr_layer_surface_v1 *layer_parent =
        wlr_layer_surface_v1_try_from_wlr_surface(popup->parent);
    if (layer_parent != nullptr) {
        // quickshell popups (and any layer-surface popups) must appear on the
        // top layer above all layershell/background/window layers. Force them
        // into OVERLAY so they are not occluded by TOP/BOTTOM panels while
        // preserving their position relative to the parent layer surface.
        struct wlr_scene_tree *overlay =
            server->getLayerTree(ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY);
        struct wlr_scene_tree *layer_tree = nullptr;
        if (layer_parent->data)
            layer_tree = (struct wlr_scene_tree *)layer_parent->data;
        struct wlr_scene_tree *target = overlay ? overlay : layer_tree;
        if (!target) target = &server->getScene()->tree;
        popup->base->data = wlr_scene_xdg_surface_create(target, popup->base);
        // If we forced the popup into OVERLAY, its position is currently
        // relative to the parent surface (0,0 in overlay). Make it absolute
        // by adding the parent layer's absolute position so it appears
        // anchored to the parent layer (e.g. TopBar at 0,0, right panel at
        // 1240,0). For xdg parents this is not needed as they are in the
        // same tree.
        if (target == overlay && layer_tree && popup->base->data) {
            struct wlr_scene_tree *popup_tree =
                (struct wlr_scene_tree *)popup->base->data;
            wlr_scene_node_set_position(&popup_tree->node,
                popup_tree->node.x + layer_tree->node.x,
                popup_tree->node.y + layer_tree->node.y);
        }
    } else {
        struct wlr_xdg_surface *xdg_parent =
            wlr_xdg_surface_try_from_wlr_surface(popup->parent);
        struct wlr_scene_tree *parent_tree = nullptr;
        if (xdg_parent != nullptr && xdg_parent->data != nullptr) {
            parent_tree = (struct wlr_scene_tree *)xdg_parent->data;
        }
        // xdg popups (window context menus etc.) must be on top of the
        // current window layer, not buried under a sibling tiled window.
        // Put them in popupTree (between windows and TOP) and offset to
        // parent's absolute position.
        struct wlr_scene_tree *popup_parent = server->getPopupTree();
        if (!popup_parent) popup_parent = parent_tree;
        if (!popup_parent) {
            popup_parent =
                server->getLayerTree(ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY);
        }
        if (!popup_parent) popup_parent = &server->getScene()->tree;
        popup->base->data =
            wlr_scene_xdg_surface_create(popup_parent, popup->base);
        if (parent_tree && popup_parent != parent_tree && popup->base->data) {
            struct wlr_scene_tree *popup_tree =
                (struct wlr_scene_tree *)popup->base->data;
            // popup position is currently relative to parent surface (0,0 in
            // popupTree). Make it absolute so it appears anchored to the
            // parent window at its current tiled position.
            wlr_scene_node_set_position(&popup_tree->node,
                popup_tree->node.x + parent_tree->node.x,
                popup_tree->node.y + parent_tree->node.y);
        }
    }

    signal(commit, &popup->base->surface->events.commit, handle_popup_commit);
    signal(destroy, &popup->events.destroy, handle_popup_destroy);
}
