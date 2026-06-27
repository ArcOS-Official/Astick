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

#include "compositor.h"
#include <QObject>
#include "debug.h"
#include <cstdlib>
#include <wayland-server-core.h>
#include "util.h"
#include "wlroots.h"
#include "output.h"
#include "toplevel.h"
#include "popup.h"
#include "cursor.h"
#include "layout.h"

#define ADD_TOKS(a, b) a##b
#define getComp(name, listenerName) \
    wl_container_of(listener, name, ADD_TOKS(listenerName, Listener))

void handle_newOutput(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, newOutput);
    emit self->outputAdded((wlr_output *)data);
}

void handle_newXdgToplevelNotify(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, newXdgToplevelNotify);
    emit self->toplevelAdded((struct wlr_xdg_toplevel *)data);
}

void handle_newXdgPopupNotify(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, newXdgPopupNotify);
    emit self->popupAdded((struct wlr_xdg_popup *)data);
}

void handle_setSelection(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, setSelection);
    emit self->setSelection((struct wlr_seat_request_set_selection_event *)data);
}

void handle_newInput(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, newInput);
    emit self->inputAdded((struct wlr_input_device *)data);
}

// Compositor private slots

void Compositor::onOutputAdded(struct wlr_output *output)
{
    wlr_output_init_render(output, allocator, renderer);

    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *mode = wlr_output_preferred_mode(output);
    if (mode != nullptr)
        wlr_output_state_set_mode(&state, mode);

    wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);

    Output *out = new Output(output, renderer, allocator, scene);
    outputs.append(out);

    connect(out, &Output::workspaceChanged, this, [this, out](int oldWs, int newWs) {
        layout->deactivateWorkspace(oldWs);
        layout->activateWorkspace(newWs);
        layout->arrange(out->get(), newWs);
    });

    layout->activateWorkspace(out->getWorkspace());
    rearrangeTiled();

    struct wlr_output_layout_output *lout =
        wlr_output_layout_add_auto(outputLayout, output);
    struct wlr_scene_output *rout = wlr_scene_output_create(scene, output);
    wlr_scene_output_layout_add_output(sceneLayout, lout, rout);
}

void Compositor::onToplevelAdded(struct wlr_xdg_toplevel *xtoplevel)
{
    Toplevel *toplevel = new Toplevel(this, xtoplevel, wlr_scene_xdg_surface_create(
        &scene->tree, xtoplevel->base
    ));
    toplevels.append(toplevel);

    connect(toplevel, &Toplevel::mapped, this, [this, toplevel]() {
        onToplevelMapped(toplevel);
    });
    connect(toplevel, &Toplevel::unmapped, this, [this, toplevel]() {
        onToplevelUnmapped(toplevel);
    });
    connect(toplevel, &Toplevel::moveRequested, this, [this, toplevel]() {
        cursorMgrObj->beginInteractive(toplevel, CURSOR_MOVE, 0);
        int ws = layout->getWindowWorkspace(toplevel);
        if (ws > 0 && layout->getWorkspaceLayoutMode(ws) == LayoutManager::Mode::Tiling) {
            detachedWindow = toplevel;
            detachedFromWorkspace = ws;
            layout->removeWindow(toplevel);
            rearrangeTiled();
        }
    });
    connect(toplevel, &Toplevel::resizeRequested, this, [this, toplevel](uint32_t edges) {
        cursorMgrObj->beginInteractive(toplevel, CURSOR_RESIZE, edges);
        rearrangeTiled();
    });
}

void Compositor::onPopupAdded(struct wlr_xdg_popup *xpopup)
{
    popups.append(new Popup(this, xpopup));
    rearrangeTiled();
}

void Compositor::onSetSelection(struct wlr_seat_request_set_selection_event *event)
{
    wlr_seat_set_selection(seat, event->source, event->serial);
}

void Compositor::onInputAdded(struct wlr_input_device *device)
{
    switch (device->type) {
    case WLR_INPUT_DEVICE_KEYBOARD:
        addKeyboard(device);
        break;
    case WLR_INPUT_DEVICE_POINTER:
        addMouse(device);
        break;
    default:
        break;
    }
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!keyboards.isEmpty()) {
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    }
    wlr_seat_set_capabilities(seat, caps);
}

void Compositor::onToplevelMapped(Toplevel *toplevel)
{
    focusToplevel(toplevel);
    int ws = outputs.isEmpty() ? 1 : outputs.first()->getWorkspace();
    layout->addWindow(toplevel, ws);
    rearrangeTiled();
    emit toplevelMapped(toplevel);
}

void Compositor::onToplevelUnmapped(Toplevel *toplevel)
{
    if (toplevel == cursorMgrObj->getGrabbed()) {
        cursorMgrObj->resetMode();
    }
    if (toplevel == detachedWindow) {
        detachedWindow = nullptr;
        detachedFromWorkspace = -1;
    }
    toplevels.removeOne(toplevel);
    layout->removeWindow(toplevel);
    rearrangeTiled();
    emit toplevelUnmapped(toplevel);
}

// Focus management

void Compositor::focusToplevel(Toplevel *toplevel)
{
    if (toplevel == nullptr) return;

    struct wlr_surface *surface = toplevel->get()->base->surface;
    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;

    if (prev_surface == surface) return;

    if (prev_surface) {
        struct wlr_xdg_toplevel *prev = wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev != nullptr) {
            wlr_xdg_toplevel_set_activated(prev, false);
        }
    }

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    wlr_scene_node_raise_to_top(&toplevel->getSceneTree()->node);
    toplevels.removeOne(toplevel);
    toplevels.prepend(toplevel);
    wlr_xdg_toplevel_set_activated(toplevel->get(), true);
    layout->raiseWindow(toplevel);

    int ws = layout->getWindowWorkspace(toplevel);
    if (ws > 0) {
        for (Output *out : outputs) {
            if (out->getWorkspace() == ws &&
                layout->getWorkspaceLayoutMode(ws) == LayoutManager::Mode::MonoWindow)
                layout->arrange(out->get(), ws);
        }
    }

    if (keyboard != nullptr) {
        wlr_seat_keyboard_notify_enter(seat, surface,
            keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
}

Output *Compositor::outputForToplevel(Toplevel *)
{
    if (outputs.isEmpty()) return nullptr;
    return outputs.first();
}

void Compositor::setInitialLayoutMode(const QString &mode)
{
    QString m = mode.toLower().trimmed();
    LayoutManager::Mode lm = LayoutManager::Mode::Tiling;
    if (m == "floating")
        lm = LayoutManager::Mode::Floating;
    else if (m == "monowindow")
        lm = LayoutManager::Mode::MonoWindow;
    layout->setWorkspaceLayoutMode(1, lm);
}

void Compositor::rearrangeTiled()
{
    for (Output *out : outputs) {
        int ws = out->getWorkspace();
        if (layout->getWorkspaceLayoutMode(ws) == LayoutManager::Mode::Tiling)
            layout->arrange(out->get(), ws);
    }
}

// Input device helpers

void Compositor::addKeyboard(struct wlr_input_device *device)
{
    Keyboard *kb = new Keyboard(device, seat);
    keyboards.append(kb);
    connect(kb, &Keyboard::keyPressed, this, [this, kb](struct wlr_keyboard_key_event *event) {
        uint32_t keycode = event->keycode + 8;
        const xkb_keysym_t *syms;
        int nsyms = xkb_state_key_get_syms(
            kb->getKeyboard()->xkb_state, keycode, &syms);

        bool handled = false;
        uint32_t modifiers = wlr_keyboard_get_modifiers(kb->getKeyboard());
        if ((modifiers & WLR_MODIFIER_ALT) &&
                event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            for (int i = 0; i < nsyms; i++) {
                switch (syms[i]) {
                case XKB_KEY_Escape:
                    wl_display_terminate(display);
                    handled = true;
                    break;
                case XKB_KEY_F1:
                    if (toplevels.size() >= 2) {
                        focusToplevel(toplevels.last());
                    }
                    handled = true;
                    break;
                case XKB_KEY_F2: {
                    if (outputs.isEmpty()) break;
                    Output *out = outputs.first();
                    int ws = out->getWorkspace();
                    auto mode = layout->getWorkspaceLayoutMode(ws);
                    int next = ((int)mode + 1) % 3;
                    layout->setWorkspaceLayoutMode(ws, (LayoutManager::Mode)next);
                    layout->arrange(out->get(), ws);
                    handled = true;
                    break;
                }
                case XKB_KEY_F3: {
                    if (outputs.isEmpty()) break;
                    Output *out = outputs.first();
                    int newWs = layout->createWorkspace();
                    out->setWorkspace(newWs);
                    handled = true;
                    break;
                }
                case XKB_KEY_F4: {
                    if (outputs.isEmpty()) break;
                    Output *out = outputs.first();
                    int ws = out->getWorkspace();
                    if (ws > 1) {
                        out->setWorkspace(1);
                    }
                    handled = true;
                    break;
                }
                default:
                    break;
                }
            }
        }

        if (!handled) {
            wlr_seat_set_keyboard(seat, kb->getKeyboard());
            wlr_seat_keyboard_notify_key(seat, event->time_msec,
                event->keycode, event->state);
        }
    });
}

void Compositor::addMouse(struct wlr_input_device *device)
{
    Mouse *mouse = new Mouse(device);
    mice.append(mouse);
    wlr_cursor_attach_input_device(cursor, device);
}

// Compositor lifecycle

Compositor::Compositor(const Astick &app)
{
    connect(&app, &Astick::aboutToRun, this, &Compositor::run);

    connect(this, &Compositor::outputAdded, this, &Compositor::onOutputAdded);
    connect(this, &Compositor::toplevelAdded, this, &Compositor::onToplevelAdded);
    connect(this, &Compositor::popupAdded, this, &Compositor::onPopupAdded);
    connect(this, &Compositor::setSelection, this, &Compositor::onSetSelection);
    connect(this, &Compositor::inputAdded, this, &Compositor::onInputAdded);

    display = wl_display_create();
    loop = wl_display_get_event_loop(display);
    backend = wlr_backend_autocreate(loop, nullptr);
    if (backend == nullptr) {
        wlr_log(WLR_ERROR, "Failed to create backend");
        return;
    }
    renderer = wlr_renderer_autocreate(backend);
    if (renderer == nullptr) {
        wlr_log(WLR_ERROR, "failed to create renderer");
        return;
    }
    wlr_renderer_init_wl_display(renderer, display);
    allocator = wlr_allocator_autocreate(backend, renderer);
    if (allocator == nullptr) {
        wlr_log(WLR_ERROR, "failed to create allocator");
        return;
    }

    wlr_compositor_create(display, 5, renderer);
    wlr_subcompositor_create(display);
    wlr_data_device_manager_create(display);

    outputLayout = wlr_output_layout_create(display);
    scene = wlr_scene_create();
    sceneLayout = wlr_scene_attach_output_layout(scene, outputLayout);

    xdgShell = wlr_xdg_shell_create(display, 3);
    cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor, outputLayout);
    cursorMgr = wlr_xcursor_manager_create(nullptr, 24);
    wlr_xcursor_manager_load(cursorMgr, 1);

    seat = wlr_seat_create(display, "seat0");

    cursorMgrObj = new CursorManager(this);
    layout = new LayoutManager();

    connect(cursorMgrObj, &CursorManager::interactiveEnded, this, [this](Toplevel *toplevel, CursorMode mode) {
        if (mode == CURSOR_MOVE && toplevel == detachedWindow && detachedFromWorkspace > 0) {
            if (!outputs.isEmpty()) {
                Output *out = outputs.first();
                int mid = out->get()->width / 2;
                if (cursor->x < mid)
                    layout->prependWindow(toplevel, detachedFromWorkspace);
                else
                    layout->addWindow(toplevel, detachedFromWorkspace);
            } else {
                layout->addWindow(toplevel, detachedFromWorkspace);
            }
            rearrangeTiled();
            detachedWindow = nullptr;
            detachedFromWorkspace = -1;
        }
    });

    socket = QString(wl_display_add_socket_auto(display));
    wlr_log(WLR_INFO, "Successfully initialized on socket %s", socket.toUtf8().data());
    initialized = true;

    signal(newOutputListener, &backend->events.new_output, handle_newOutput);
    signal(newXdgToplevelNotifyListener, &xdgShell->events.new_toplevel, handle_newXdgToplevelNotify);
    signal(newXdgPopupNotifyListener, &xdgShell->events.new_popup, handle_newXdgPopupNotify);
    signal(setSelectionListener, &seat->events.request_set_selection, handle_setSelection);
    signal(newInputListener, &backend->events.new_input, handle_newInput);
}

Compositor::~Compositor()
{
    if (initialized) {
        wl_display_destroy_clients(display);

        wl_list_remove(&newOutputListener.link);
        wl_list_remove(&newXdgToplevelNotifyListener.link);
        wl_list_remove(&newXdgPopupNotifyListener.link);
        wl_list_remove(&setSelectionListener.link);
        wl_list_remove(&newInputListener.link);

        delete cursorMgrObj;
        delete layout;

        for (Output *out : outputs) delete out;
        for (Toplevel *t : toplevels) delete t;
        for (Popup *p : popups) delete p;
        for (Keyboard *k : keyboards) delete k;
        for (Mouse *m : mice) delete m;
    }
    if (cursorMgr) wlr_xcursor_manager_destroy(cursorMgr);
    if (scene) wlr_scene_node_destroy(&scene->tree.node);
    if (cursor) wlr_cursor_destroy(cursor);
    if (allocator) wlr_allocator_destroy(allocator);
    if (renderer) wlr_renderer_destroy(renderer);
    if (backend) wlr_backend_destroy(backend);
    if (display) wl_display_destroy(display);
    initialized = false;
}

void Compositor::run()
{
    if (!initialized) {
        wlr_log(WLR_ERROR, "Astick not initialized, will not run");
        return;
    }

    if (!wlr_backend_start(backend)) {
        wlr_log(WLR_ERROR, "Failed to start backend");
        return;
    }

    setenv("WAYLAND_DISPLAY", socket.toUtf8().data(), true);
    wlr_log(WLR_INFO, "Running Astick on socket \"%s\"", socket.toUtf8().data());
    wl_display_run(display);
}

void Compositor::closePopup()
{
    if (!popups.isEmpty()) {
        Popup *popup = popups.takeLast();
        wlr_log(WLR_INFO, "Closing popup");
        emit popup->destroyed();
        delete popup;
    }
}
