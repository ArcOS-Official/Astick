#include "popup.h"
#include "compositor.h"
#include "util.h"
#include <cstdio>

void handle_popup_commit(wl_listener *listener, void *)
{
    Popup *self = wl_container_of(listener, self, commit);
    if (self->popup->base->initial_commit) {
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

Popup::Popup(
    Compositor *server_,
    struct wlr_xdg_popup *popup_
)
{
    server = server_;
    popup = popup_;

    struct wlr_xdg_surface *parent = wlr_xdg_surface_try_from_wlr_surface(popup->parent);
    struct wlr_scene_tree *parent_tree = (struct wlr_scene_tree *)parent->data;
    popup->base->data = wlr_scene_xdg_surface_create(parent_tree, popup->base);

    signal(commit, &popup->base->surface->events.commit, handle_popup_commit);
    signal(destroy, &popup->base->surface->events.destroy, handle_popup_destroy);
}
