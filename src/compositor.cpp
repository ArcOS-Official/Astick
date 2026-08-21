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
#include <cmath>
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

void handle_newLayerNotify(wl_listener *listener, void *data)
{
    Compositor *self = getComp(self, newLayerNotify);
    emit self->layerAdded((struct wlr_layer_surface_v1 *)data);
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

    std::string oid = Config::outputId(output);
    OutputEntry entry{};
    bool hasEntry = false;
    DefaultOutput def{};
    if (config) {
        auto it = config->monitors.find(oid);
        if (it != config->monitors.end()) {
            entry = it->second;
            hasEntry = true;
            wlr_log(WLR_INFO, "Output %s matched config %dx%d@%.2f scale %.2f", oid.c_str(), entry.width, entry.height, entry.refresh, entry.scale);
        } else {
            def = config->defaultOutput;
            wlr_log(WLR_INFO, "Output %s not in config, using default/recommended (default %dx%d@%.2f)", oid.c_str(), def.width, def.height, def.refresh);
        }
        double dpi = config->detectDpi(output);
        wlr_log(WLR_INFO, "Output %s DPI detected: %.1f (phys %dx%d mm, mode %dx%d)", oid.c_str(), dpi, output->phys_width, output->phys_height, output->width, output->height);
    } else {
        def = DefaultOutput{};
    }

    // Build single state with mode + scale and try to commit once
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);

    struct wlr_output_mode *preferred = wlr_output_preferred_mode(output);
    double desiredScale = 1.0;
    if (config) {
        // Determine scale via DPI detector / config
        OutputEntry scaleProbe = hasEntry ? entry : OutputEntry{};
        if (!hasEntry) {
            scaleProbe.width = def.width;
            scaleProbe.height = def.height;
            scaleProbe.refresh = def.refresh;
            scaleProbe.scale = def.scale;
        }
        desiredScale = config->getOutputScale(output, scaleProbe);
        if (desiredScale < 0.5) desiredScale = 1.0;
        wlr_log(WLR_INFO, "Output %s: scale %.2f", oid.c_str(), desiredScale);
        wlr_output_state_set_scale(&state, (float)desiredScale);
    }

    // Try config mode first if present
    if (hasEntry && entry.width > 0 && entry.height > 0) {
        int refresh_mhz = entry.refresh > 0 ? (int)(entry.refresh * 1000) : 0;
        if (refresh_mhz == 0 && preferred) refresh_mhz = preferred->refresh;
        if (refresh_mhz == 0) refresh_mhz = (int)(def.refresh * 1000);
        wlr_output_state_set_custom_mode(&state, entry.width, entry.height, refresh_mhz);
        wlr_log(WLR_INFO, "Output %s: trying config mode %dx%d@%d mHz", oid.c_str(), entry.width, entry.height, refresh_mhz);
    } else if (preferred) {
        wlr_output_state_set_mode(&state, preferred);
        wlr_log(WLR_INFO, "Output %s: using preferred mode %dx%d@%d", oid.c_str(), preferred->width, preferred->height, preferred->refresh);
    } else {
        int w = hasEntry && entry.width > 0 ? entry.width : def.width;
        int h = hasEntry && entry.height > 0 ? entry.height : def.height;
        double ref = hasEntry && entry.refresh > 0 ? entry.refresh : def.refresh;
        int refresh_mhz = (int)(ref * 1000);
        if (w <= 0) w = output->width ? output->width : 1280;
        if (h <= 0) h = output->height ? output->height : 720;
        wlr_output_state_set_custom_mode(&state, w, h, refresh_mhz);
        wlr_log(WLR_INFO, "Output %s: using fallback custom mode %dx%d@%d", oid.c_str(), w, h, refresh_mhz);
    }

    bool committed = wlr_output_commit_state(output, &state);
    if (!committed) {
        wlr_log(WLR_INFO, "Output %s: state commit failed, retrying without custom mode/scale", oid.c_str());
        wlr_output_state_finish(&state);
        wlr_output_state_init(&state);
        wlr_output_state_set_enabled(&state, true);
        if (preferred) {
            wlr_output_state_set_mode(&state, preferred);
            wlr_log(WLR_INFO, "Output %s: retry with preferred mode", oid.c_str());
        }
        // try without scale if scale was the issue
        committed = wlr_output_commit_state(output, &state);
        if (!committed) {
            wlr_log(WLR_INFO, "Output %s: retry without mode, just enable", oid.c_str());
            wlr_output_state_finish(&state);
            wlr_output_state_init(&state);
            wlr_output_state_set_enabled(&state, true);
            wlr_output_commit_state(output, &state);
        }
    } else {
        wlr_log(WLR_INFO, "Output %s: committed state %dx%d scale %.2f", oid.c_str(), output->width, output->height, desiredScale);
    }
    wlr_output_state_finish(&state);

    // Persist new output if not in config
    if (config && !hasEntry) {
        OutputEntry newEntry;
        newEntry.width = output->width;
        newEntry.height = output->height;
        newEntry.refresh = output->refresh ? output->refresh / 1000.0 : def.refresh;
        newEntry.scale = desiredScale;
        newEntry.enabled = true;
        config->monitors[oid] = newEntry;
        config->save();
        wlr_log(WLR_INFO, "Output %s: saved new entry to config", oid.c_str());
    }

    Output *out = new Output(output, renderer, allocator, scene);
    outputs.append(out);

    connect(out, &Output::workspaceChanged, this, [this, out](int oldWs, int newWs) {
        layout->deactivateWorkspace(oldWs);
        layout->activateWorkspace(newWs);
        arrangeForOutput(out);
    });

    layout->activateWorkspace(out->getWorkspace());
    rearrangeTiled();

    struct wlr_output_layout_output *lout = nullptr;
    if (config) {
        auto it = config->monitors.find(oid);
        if (it != config->monitors.end() && it->second.x != INT_MIN && it->second.y != INT_MIN) {
            lout = wlr_output_layout_add(outputLayout, output, it->second.x, it->second.y);
            wlr_log(WLR_INFO, "Output %s: placed at %d,%d from config", oid.c_str(), it->second.x, it->second.y);
        } else {
            lout = wlr_output_layout_add_auto(outputLayout, output);
        }
    } else {
        lout = wlr_output_layout_add_auto(outputLayout, output);
    }
    struct wlr_scene_output *rout = wlr_scene_output_create(scene, output);
    wlr_scene_output_layout_add_output(sceneLayout, lout, rout);
}

void Compositor::onToplevelAdded(struct wlr_xdg_toplevel *xtoplevel)
{
    struct wlr_scene_tree *tree = wlr_scene_xdg_surface_create(
        &scene->tree, xtoplevel->base
    );
    if (popupTree) {
        wlr_scene_node_place_below(&tree->node, &popupTree->node);
    } else {
        wlr_scene_node_place_below(&tree->node,
            &layerTrees[ZWLR_LAYER_SHELL_V1_LAYER_TOP]->node);
    }
    Toplevel *toplevel = new Toplevel(this, xtoplevel, tree);
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
    Popup *popup = new Popup(this, xpopup);
    popups.append(popup);
    connect(popup, &Popup::destroyed, this, [this, popup]() {
        popups.removeOne(popup);
    });
    rearrangeTiled();
}

void Compositor::onLayerAdded(struct wlr_layer_surface_v1 *lsurface)
{
    if (lsurface->output == nullptr) {
        if (outputs.isEmpty()) return;
        lsurface->output = outputs.first()->get();
    }
    wlr_log(WLR_INFO, "New layer surface (layer %d, %dx%d)",
        lsurface->current.layer, lsurface->current.desired_width, lsurface->current.desired_height);
    LayerSurface *layer = new LayerSurface(this, lsurface);
    layers.append(layer);
    auto reflowForLayer = [this, layer]() {
        struct wlr_output *wout = layer->get()->output;
        for (Output *out : outputs) {
            if (out->get() == wout) {
                arrangeForOutput(out);
                break;
            }
        }
    };
    connect(layer, &LayerSurface::mapped, this, reflowForLayer);
    connect(layer, &LayerSurface::unmapped, this, reflowForLayer);
    connect(layer, &LayerSurface::committed, this, reflowForLayer);
    connect(layer, &LayerSurface::destroyed, this, [this, layer]() {
        layers.removeOne(layer);
        for (Output *out : outputs) arrangeForOutput(out);
    });
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
    if (popupTree) {
        wlr_scene_node_place_below(&toplevel->getSceneTree()->node, &popupTree->node);
    } else {
        wlr_scene_node_place_below(&toplevel->getSceneTree()->node,
            &layerTrees[ZWLR_LAYER_SHELL_V1_LAYER_TOP]->node);
    }
    toplevels.removeOne(toplevel);
    toplevels.prepend(toplevel);
    wlr_xdg_toplevel_set_activated(toplevel->get(), true);
    layout->raiseWindow(toplevel);

    int ws = layout->getWindowWorkspace(toplevel);
    if (ws > 0) {
        for (Output *out : outputs) {
            if (out->getWorkspace() == ws &&
                layout->getWorkspaceLayoutMode(ws) == LayoutManager::Mode::MonoWindow)
                arrangeForOutput(out);
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

struct wlr_box Compositor::usableAreaForOutput(struct wlr_output *wlr_output)
{
    struct wlr_box full = {0, 0, 0, 0};
    if (outputLayout && wlr_output) {
        wlr_output_layout_get_box(outputLayout, wlr_output, &full);
        if (full.width == 0 && full.height == 0) {
            full = {0, 0, wlr_output->width, wlr_output->height};
        }
    } else if (wlr_output) {
        full = {0, 0, wlr_output->width, wlr_output->height};
    }
    struct wlr_box usable = full;
    for (LayerSurface *ls : layers) {
        struct wlr_layer_surface_v1 *l = ls->get();
        if (l->output != wlr_output) continue;
        if (!l->surface->mapped) continue;
        struct wlr_layer_surface_v1_state *state = &l->current;
        if (state->exclusive_zone <= 0) continue;

        // Prefer exclusive_edge if set, otherwise infer from anchor
        uint32_t anchor = state->anchor;
        if (state->exclusive_edge != 0) {
            anchor = state->exclusive_edge;
        }

        // Apply the same logic as wlr_scene_layer_surface_v1 exclusive handling
        // plus margin. The scene helper handles 4 specific anchor combos; handle
        // both those and a generic per-edge fallback.
        bool handled = false;
        switch (anchor) {
        case ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP:
        case (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
              ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
              ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT):
            usable.y += state->exclusive_zone + state->margin.top;
            usable.height -= state->exclusive_zone + state->margin.top;
            handled = true;
            break;
        case ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM:
        case (ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
              ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
              ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT):
            usable.height -= state->exclusive_zone + state->margin.bottom;
            handled = true;
            break;
        case ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT:
        case (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
              ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
              ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT):
            usable.x += state->exclusive_zone + state->margin.left;
            usable.width -= state->exclusive_zone + state->margin.left;
            handled = true;
            break;
        case ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT:
        case (ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
              ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
              ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT):
            usable.width -= state->exclusive_zone + state->margin.right;
            handled = true;
            break;
        default:
            break;
        }
        if (!handled) {
            // Generic fallback: treat each anchored edge with an exclusive
            // zone. This covers custom exclusive_edge values or unusual anchors.
            if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) {
                usable.y += state->exclusive_zone + state->margin.top;
                usable.height -= state->exclusive_zone + state->margin.top;
            } else if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) {
                usable.height -= state->exclusive_zone + state->margin.bottom;
            }
            if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) {
                usable.x += state->exclusive_zone + state->margin.left;
                usable.width -= state->exclusive_zone + state->margin.left;
            } else if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) {
                usable.width -= state->exclusive_zone + state->margin.right;
            }
        }
        if (usable.width < 0) usable.width = 0;
        if (usable.height < 0) usable.height = 0;
    }
    return usable;
}

void Compositor::arrangeForOutput(Output *out)
{
    if (!out) return;
    struct wlr_box usable = usableAreaForOutput(out->get());
    layout->arrange(usable, out->getWorkspace());
}

void Compositor::rearrangeTiled()
{
    for (Output *out : outputs) {
        int ws = out->getWorkspace();
        if (layout->getWorkspaceLayoutMode(ws) == LayoutManager::Mode::Tiling)
            arrangeForOutput(out);
    }
}

// Input device helpers

void Compositor::addKeyboard(struct wlr_input_device *device)
{
    Keyboard *kb = new Keyboard(device, seat);
    if (config) kb->applyConfig(config->keyboard);
    keyboards.append(kb);
    connect(kb, &Keyboard::keyPressed, this, [this, kb](struct wlr_keyboard_key_event *event) {
        uint32_t keycode = event->keycode + 8;
        const xkb_keysym_t *syms;
        int nsyms = xkb_state_key_get_syms(
            kb->getKeyboard()->xkb_state, keycode, &syms);

        bool handled = false;
        uint32_t modifiers = wlr_keyboard_get_modifiers(kb->getKeyboard());

        if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            for (int i = 0; i < nsyms && !handled; i++) {
                xkb_keysym_t sym = syms[i];
                const Keybind *kbnd = nullptr;
                if (config) kbnd = config->findKeybind(modifiers, sym);
                if (kbnd) {
                    const std::string &act = kbnd->action;
                    if (act == "quit" || act == "exit" || act == "terminate") {
                        wl_display_terminate(display);
                    } else if (act == "focus_prev") {
                        if (toplevels.size() >= 2) focusToplevel(toplevels.last());
                    } else if (act == "focus_next") {
                        if (!toplevels.isEmpty()) focusToplevel(toplevels.first());
                    } else if (act == "toggle_layout" || act == "toggle") {
                        if (!outputs.isEmpty()) {
                            Output *out = outputs.first();
                            int ws = out->getWorkspace();
                            auto mode = layout->getWorkspaceLayoutMode(ws);
                            int next = ((int)mode + 1) % 3;
                            layout->setWorkspaceLayoutMode(ws, (LayoutManager::Mode)next);
                            arrangeForOutput(out);
                        }
                    } else if (act == "new_workspace") {
                        if (!outputs.isEmpty()) {
                            Output *out = outputs.first();
                            int newWs = layout->createWorkspace();
                            out->setWorkspace(newWs);
                        }
                    } else if (act == "goto_workspace") {
                        if (!outputs.isEmpty() && !kbnd->arg.empty()) {
                            int ws = std::atoi(kbnd->arg.c_str());
                            if (ws > 0) outputs.first()->setWorkspace(ws);
                        }
                    } else if (act == "close_window" || act == "kill") {
                        if (seat->keyboard_state.focused_surface) {
                            struct wlr_xdg_toplevel *tl = wlr_xdg_toplevel_try_from_wlr_surface(seat->keyboard_state.focused_surface);
                            if (tl) wlr_xdg_toplevel_send_close(tl);
                        }
                    } else {
                        wlr_log(WLR_INFO, "Unknown keybind action %s", act.c_str());
                        continue;
                    }
                    handled = true;
                } else {
                    // Fallback hardcoded Alt+ keys for backwards compat when no config match
                    if ((modifiers & WLR_MODIFIER_ALT) && !config) {
                        switch (sym) {
                        case XKB_KEY_Escape: wl_display_terminate(display); handled = true; break;
                        case XKB_KEY_F1: if (toplevels.size() >= 2) focusToplevel(toplevels.last()); handled = true; break;
                        case XKB_KEY_F2: {
                            if (!outputs.isEmpty()) {
                                Output *out = outputs.first();
                                int ws = out->getWorkspace();
                                auto mode = layout->getWorkspaceLayoutMode(ws);
                                int next = ((int)mode + 1) % 3;
                                layout->setWorkspaceLayoutMode(ws, (LayoutManager::Mode)next);
                                arrangeForOutput(out);
                            }
                            handled = true; break;
                        }
                        case XKB_KEY_F3: {
                            if (!outputs.isEmpty()) {
                                Output *out = outputs.first();
                                int newWs = layout->createWorkspace();
                                out->setWorkspace(newWs);
                            }
                            handled = true; break;
                        }
                        case XKB_KEY_F4: {
                            if (!outputs.isEmpty()) {
                                Output *out = outputs.first();
                                int ws = out->getWorkspace();
                                if (ws > 1) out->setWorkspace(1);
                            }
                            handled = true; break;
                        }
                        default: break;
                        }
                    }
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
    if (config) mouse->applyConfig(config->mouse);
    mice.append(mouse);
    wlr_cursor_attach_input_device(cursor, device);
    if (config && config->mouse.speed != 0) {
        wlr_log(WLR_INFO, "Mouse %s speed multiplier %.2f", device->name ? device->name : "unknown", config->mouse.speed);
    }
}

// Compositor lifecycle

Compositor::Compositor(const Astick &app, Config *cfg)
    : config(cfg)
{
    connect(&app, &Astick::aboutToRun, this, &Compositor::run);

    connect(this, &Compositor::outputAdded, this, &Compositor::onOutputAdded);
    connect(this, &Compositor::toplevelAdded, this, &Compositor::onToplevelAdded);
    connect(this, &Compositor::popupAdded, this, &Compositor::onPopupAdded);
    connect(this, &Compositor::layerAdded, this, &Compositor::onLayerAdded);
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
    wlr_presentation_create(display, backend, 1);

    outputLayout = wlr_output_layout_create(display);
    scene = wlr_scene_create();
    sceneLayout = wlr_scene_attach_output_layout(scene, outputLayout);

    for (int i = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND;
            i <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; i++) {
        layerTrees[i] = wlr_scene_tree_create(&scene->tree);
    }
    popupTree = wlr_scene_tree_create(&scene->tree);
    wlr_scene_node_place_below(&popupTree->node,
        &layerTrees[ZWLR_LAYER_SHELL_V1_LAYER_TOP]->node);

    xdgShell = wlr_xdg_shell_create(display, 3);
    layerShell = wlr_layer_shell_v1_create(display, 5);
    cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor, outputLayout);
    cursorMgr = wlr_xcursor_manager_create(nullptr, 24);
    wlr_xcursor_manager_load(cursorMgr, 1);

    seat = wlr_seat_create(display, "seat0");

    cursorMgrObj = new CursorManager(this);
    layout = new LayoutManager();

    connect(cursorMgrObj, &CursorManager::interactiveEnded, this, [this](Toplevel *toplevel, CursorMode mode) {
        if (mode == CURSOR_MOVE && toplevel == detachedWindow && detachedFromWorkspace > 0) {
            Output *out = outputForToplevel(toplevel);
            if (!out && !outputs.isEmpty()) out = outputs.first();
            if (out) {
                struct wlr_box usable = usableAreaForOutput(out->get());
                struct wlr_scene_tree *tree = toplevel->getSceneTree();
                struct wlr_box *geo = &toplevel->get()->base->geometry;
                double winW = geo->width > 0 ? geo->width : 800;
                double winH = geo->height > 0 ? geo->height : 600;
                double cx = tree->node.x + geo->x + winW / 2.0;
                double cy = tree->node.y + geo->y + winH / 2.0;
                int remaining = layout->windowCount(detachedFromWorkspace);
                int total = remaining + 1;
                int cols = std::ceil(std::sqrt((double)total));
                int rows = std::ceil((double)total / cols);
                int cell_w = cols > 0 ? usable.width / cols : usable.width;
                int cell_h = rows > 0 ? usable.height / rows : usable.height;
                if (cell_w <= 0) cell_w = usable.width;
                if (cell_h <= 0) cell_h = usable.height;
                int best = 0;
                double bestDist = 1e18;
                for (int i = 0; i < total; ++i) {
                    int col = i % cols;
                    int row = i / cols;
                    double tileCx = usable.x + col * cell_w + cell_w / 2.0;
                    double tileCy = usable.y + row * cell_h + cell_h / 2.0;
                    double dx = cx - tileCx;
                    double dy = cy - tileCy;
                    double dist = dx * dx + dy * dy;
                    if (dist < bestDist) {
                        bestDist = dist;
                        best = i;
                    }
                }
                layout->insertWindowAt(toplevel, detachedFromWorkspace, best);
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
    signal(newLayerNotifyListener, &layerShell->events.new_surface, handle_newLayerNotify);
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
        wl_list_remove(&newLayerNotifyListener.link);
        wl_list_remove(&setSelectionListener.link);
        wl_list_remove(&newInputListener.link);

        delete cursorMgrObj;
        delete layout;

        for (Output *out : outputs) delete out;
        for (Toplevel *t : toplevels) delete t;
        for (Popup *p : popups) delete p;
        for (LayerSurface *l : layers) delete l;
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

void Compositor::closePopup(Popup *popup)
{
    if (popup == nullptr) return;
    wlr_log(WLR_INFO, "Closing popup");
    wlr_xdg_popup_destroy(popup->get());
}
