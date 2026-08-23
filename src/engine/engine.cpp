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

#include "engine.h"
#include "../state/state.h"
#include <QObject>
#include "../debug.h"
#include <cstdlib>
#include <cmath>
#include <wayland-server-core.h>
#include "../util.h"
#include "../wlroots.h"
#include "../output.h"
#include "../toplevel.h"
#include "../popup.h"
#include "../cursor.h"
#include "../layout.h"

#define ADD_TOKS(a, b) a##b
#define getComp(name, listenerName) \
    wl_container_of(listener, name, ADD_TOKS(listenerName, Listener))

void handle_newOutput(wl_listener *listener, void *data)
{
    Engine *self = getComp(self, newOutput);
    emit self->outputAdded((wlr_output *)data);
}

void handle_newXdgToplevelNotify(wl_listener *listener, void *data)
{
    Engine *self = getComp(self, newXdgToplevelNotify);
    emit self->toplevelAdded((struct wlr_xdg_toplevel *)data);
}

void handle_newXdgPopupNotify(wl_listener *listener, void *data)
{
    Engine *self = getComp(self, newXdgPopupNotify);
    emit self->popupAdded((struct wlr_xdg_popup *)data);
}

void handle_newLayerNotify(wl_listener *listener, void *data)
{
    Engine *self = getComp(self, newLayerNotify);
    emit self->layerAdded((struct wlr_layer_surface_v1 *)data);
}

void handle_setSelection(wl_listener *listener, void *data)
{
    Engine *self = getComp(self, setSelection);
    emit self->setSelection((struct wlr_seat_request_set_selection_event *)data);
}

void handle_newInput(wl_listener *listener, void *data)
{
    Engine *self = getComp(self, newInput);
    emit self->inputAdded((struct wlr_input_device *)data);
}

// Compositor private slots

void Engine::onOutputAdded(struct wlr_output *output)
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
    // Drive new AnimationPool via output frame (vsync)
    if (animPool) {
        connect(out, &Output::frameReady, animPool, &AnimationPool::onFrame);
    }

    connect(out, &Output::workspaceChanged, this, [this, out](int oldWs, int newWs) {
        struct wlr_box usable = usableAreaForOutput(out->get());
        if (decorManager) {
            int outer = decorManager->config().outerGap;
            if (outer>0) { usable.x+=outer; usable.y+=outer; usable.width-=2*outer; usable.height-=2*outer; if(usable.width<0) usable.width=0; if(usable.height<0) usable.height=0; }
        }
        // Workspace switch animation: window-layer-only by default, uses slide/cube/fade variants
        bool shouldAnim = config && config->animations.enabled && config->animations.pairs.find("workspaceSwitch") != config->animations.pairs.end();
        if (shouldAnim) {
            // Capture before, then animate; layout activate will be done inside animate
            animateWorkspaceSwitch(out, oldWs, newWs, usable);
            // After animation, activation is handled inside animate; but ensure arrange after short delay
            // Fallback immediate arrange if anim disabled
        } else {
            layout->deactivateWorkspace(oldWs);
            layout->activateWorkspace(newWs);
            arrangeForOutput(out);
        }
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

    // Update global max FPS for animations (highest output refresh)
    if (animManager) {
        int maxFps = 60;
        for (Output *o : outputs) {
            int fps = 0;
            if (o->get()->refresh) fps = (int)(o->get()->refresh / 1000.0 + 0.5);
            else if (o->get()->current_mode) fps = o->get()->current_mode->refresh / 1000;
            if (fps > maxFps) maxFps = fps;
        }
        // also consider the new output itself if not yet in list? already added
        animManager->setMaxFps(maxFps);
        wlr_log(WLR_INFO, "Animation max FPS updated to %d (outputs %zu)", maxFps, outputs.size());
    }
}

void Engine::onToplevelAdded(struct wlr_xdg_toplevel *xtoplevel)
{
    wlr_log(WLR_INFO, "onToplevelAdded: xdg_toplevel %p title %s", (void*)xtoplevel, xtoplevel->title ? xtoplevel->title : "(null)");
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
    wlr_log(WLR_INFO, "  created Toplevel %p id %lu", (void*)toplevel, (unsigned long)toplevel->id);
    toplevels.append(toplevel);
    if (decorManager) decorManager->createFor(toplevel);

    connect(toplevel, &Toplevel::mapped, this, [this, toplevel]() {
        onToplevelMapped(toplevel);
    });
    connect(toplevel, &Toplevel::unmapped, this, [this, toplevel]() {
        onToplevelUnmapped(toplevel);
    });
    connect(toplevel, &Toplevel::moveRequested, this, [this, toplevel]() {
        if (layout->isFullscreen(toplevel) || layout->isMaximized(toplevel)) return;
        cursorMgrObj->beginInteractive(toplevel, CURSOR_MOVE, 0);
        int ws = layout->getWindowWorkspace(toplevel);
        if (ws > 0 && layout->getWorkspaceLayoutMode(ws) == LayoutManager::Mode::Tiling) {
            // Floating windows in tiling are moved freely, not detached from BSP
            if (layout->isFloating(toplevel)) {
                // No detachment; CursorManager will handle free move and update geometry
                return;
            }
            detachedWindow = toplevel;
            detachedFromWorkspace = ws;
            detachedRatio = layout->getParentRatio(toplevel);
            layout->removeWindow(toplevel);
            rearrangeTiled();
        }
    });
    connect(toplevel, &Toplevel::resizeRequested, this, [this, toplevel](uint32_t edges) {
        if (layout->isFullscreen(toplevel) || layout->isMaximized(toplevel)) return;
        // Guard: tiling/monowindow with single window must not resize/drag
        int ws = layout->getWindowWorkspace(toplevel);
        if (ws >= 0) {
            // Floating windows can always be resized even with single tiled window
            if (layout->isFloating(toplevel)) {
                cursorMgrObj->beginInteractive(toplevel, CURSOR_RESIZE, edges);
                rearrangeTiled();
                return;
            }
            auto mode = layout->getWorkspaceLayoutMode(ws);
            if ((mode == LayoutManager::Mode::Tiling || mode == LayoutManager::Mode::MonoWindow)
                && layout->tiledCount(ws) <= 1) {
                return;
            }
        }
        cursorMgrObj->beginInteractive(toplevel, CURSOR_RESIZE, edges);
        rearrangeTiled();
    });
    connect(toplevel, &Toplevel::maximizeRequested, this, [this, toplevel]() {
        bool want = toplevel->get()->requested.maximized;
        setMaximized(toplevel, want);
    });
    connect(toplevel, &Toplevel::fullscreenRequested, this, [this, toplevel]() {
        bool want = toplevel->get()->requested.fullscreen;
        struct wlr_output *reqOut = toplevel->get()->requested.fullscreen_output;
        if (want && reqOut) {
            for (Output *out : outputs) if (out->get() == reqOut) {
                struct wlr_box full = fullAreaForOutput(out->get());
                layout->setFullscreen(toplevel, true, full);
                wlr_xdg_toplevel_set_fullscreen(toplevel->get(), true);
                if (layout->isMaximized(toplevel)) {
                    layout->setMaximized(toplevel, false);
                    wlr_xdg_toplevel_set_maximized(toplevel->get(), false);
                }
                for (Output *o : outputs) if (o->getWorkspace() == layout->getWindowWorkspace(toplevel)) arrangeForOutput(o);
                focusToplevel(toplevel);
                wlr_xdg_surface_schedule_configure(toplevel->get()->base);
                return;
            }
        }
        setFullscreen(toplevel, want);
    });
    connect(toplevel, &Toplevel::destroyed, this, [this, toplevel]() {
        if (toplevel->closeAnimationRunning) {
            // Defer actual removal until close animation finishes (keeps buffer visible)
            return;
        }
        if (decorManager) decorManager->removeFor(toplevel);
        int ws = layout->getWindowWorkspace(toplevel);
        toplevels.removeOne(toplevel);
        layout->removeWindow(toplevel);
        if (ws > 0) for (Output *out : outputs) if (out->getWorkspace() == ws) arrangeForOutput(out);
        else rearrangeTiled();
    });
}

void Engine::onPopupAdded(struct wlr_xdg_popup *xpopup)
{
    Popup *popup = new Popup(this, xpopup);
    popups.append(popup);
    connect(popup, &Popup::destroyed, this, [this, popup]() {
        popups.removeOne(popup);
    });
    rearrangeTiled();
    // Popup open animation (full variants like windows)
    animatePopupOpen(popup);
}

void Engine::onLayerAdded(struct wlr_layer_surface_v1 *lsurface)
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

void Engine::onSetSelection(struct wlr_seat_request_set_selection_event *event)
{
    wlr_seat_set_selection(seat, event->source, event->serial);
}

void Engine::onInputAdded(struct wlr_input_device *device)
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

void Engine::onToplevelMapped(Toplevel *toplevel)
{
    wlr_log(WLR_INFO, "onToplevelMapped: tl %p id %lu title %s", (void*)toplevel, (unsigned long)toplevel->id, toplevel->get()->title ? toplevel->get()->title : "(null)");
    // Capture focused window before new window becomes focused, for BSP split
    Toplevel *focusedBefore = nullptr;
    if (seat && seat->keyboard_state.focused_surface) {
        struct wlr_xdg_toplevel *prev = wlr_xdg_toplevel_try_from_wlr_surface(seat->keyboard_state.focused_surface);
        if (prev) {
            for (Toplevel *t : toplevels) {
                if (t->get() == prev) { focusedBefore = t; break; }
            }
        }
    }
    focusToplevel(toplevel);
    int ws = outputs.isEmpty() ? 1 : outputs.first()->getWorkspace();
    struct wlr_box usable = {0,0,1920,1080};
    Output *outForWs = nullptr;
    for (Output *o : outputs) if (o->getWorkspace() == ws) { outForWs = o; break; }
    if (!outForWs && !outputs.isEmpty()) outForWs = outputs.first();
    if (outForWs) usable = usableAreaForOutput(outForWs->get());
    wlr_log(WLR_INFO, "  ws %d usable %d,%d %dx%d", ws, usable.x, usable.y, usable.width, usable.height);
    double cx = cursor ? cursor->x : usable.x + usable.width/2.0;
    double cy = cursor ? cursor->y : usable.y + usable.height/2.0;
    auto wsMode = layout->getWorkspaceLayoutMode(ws);
    wlr_log(WLR_INFO, "  wsMode %d", (int)wsMode);
    // Capture before snapshot for tiling animation before adding new window
    std::unordered_map<Toplevel*, struct wlr_box> beforeTiling;
    if (wsMode == LayoutManager::Mode::Tiling && config && config->animations.enabled) {
        beforeTiling = layout->snapshotGeometries(ws, usable);
        wlr_log(WLR_INFO, "  beforeTiling size %zu", beforeTiling.size());
        for(auto &kv : beforeTiling) wlr_log(WLR_INFO, "    before tl %p id %lu %d,%d %dx%d", (void*)kv.first, (unsigned long)kv.first->id, kv.second.x, kv.second.y, kv.second.width, kv.second.height);
    }
    if (wsMode == LayoutManager::Mode::Tiling) {
        layout->addWindow(toplevel, ws, focusedBefore, usable, cx, cy);
    } else {
        layout->addWindow(toplevel, ws);
    }
    // Capture after and animate existing windows tiling (sets targets) - don't call rearrangeTiled which would overwrite animation start
    bool didTilingAnim = false;
    std::unordered_map<Toplevel*, struct wlr_box> afterTiling;
    if (wsMode == LayoutManager::Mode::Tiling && config && config->animations.enabled) {
        afterTiling = layout->snapshotGeometries(ws, usable);
        wlr_log(WLR_INFO, "  afterTiling size %zu", afterTiling.size());
        for(auto &kv : afterTiling) wlr_log(WLR_INFO, "    after tl %p id %lu %d,%d %dx%d", (void*)kv.first, (unsigned long)kv.first->id, kv.second.x, kv.second.y, kv.second.width, kv.second.height);
        auto it = config->animations.pairs.find("tilingMove");
        if (it != config->animations.pairs.end() && it->second.start.enabled && it->second.start.duration>0) {
            for(auto &kv : afterTiling) {
                Toplevel *tl = kv.first;
                if (tl == toplevel) continue;
                auto bit = beforeTiling.find(tl);
                if (bit == beforeTiling.end()) continue;
                struct wlr_box from = bit->second;
                struct wlr_box to = kv.second;
                if (from.x==to.x && from.y==to.y && from.width==to.width && from.height==to.height) {
                    wlr_log(WLR_INFO, "    tl %p no change skip", (void*)tl);
                    continue;
                }
                wlr_log(WLR_INFO, "    animating tiling tl %p from %d,%d %dx%d to %d,%d %dx%d", (void*)tl, from.x,from.y,from.width,from.height, to.x,to.y,to.width,to.height);
                animateBoxForToplevel(tl, from, to, it->second.start, "tilingMove:");
                didTilingAnim = true;
            }
        }
        // Update layout's stored geometries to after without touching scene (animation drives scene)
        layout->applyGeometries(afterTiling);
        // Ensure decorations and output layout are updated without re-tiling
        if (outForWs) {
            // Update decorations for new window
            if (decorManager) {
                for (Toplevel *tl : toplevels) {
                    if (layout->getWindowWorkspace(tl) != ws) continue;
                    struct wlr_box box;
                    bool has = layout->getWindowGeometry(tl, ws, usable, box);
                    if (!has) has = layout->getWindowGeometry(tl, box);
                    if (!has) continue;
                    if (auto *dec = decorManager->decorationFor(tl)) {
                        dec->updateGeometry(box.x, box.y, box.width, box.height);
                    }
                }
            }
        }
    }
    if (!didTilingAnim) {
        rearrangeTiled();
    } else {
        // For the new window, ensure its decoration is updated (it was not yet rearranged)
        if (outForWs && decorManager) {
            struct wlr_box box;
            if (layout->getWindowGeometry(toplevel, ws, usable, box)) {
                if (auto *dec = decorManager->decorationFor(toplevel)) dec->updateGeometry(box.x, box.y, box.width, box.height);
            }
        }
        // Schedule frame for animation
        if (animPool) animPool->onFrame();
    }
    // Animate window open (paired start)
    {
        struct wlr_box finalBox;
        bool has1 = layout->getWindowGeometry(toplevel, ws, usable, finalBox);
        bool has2 = false;
        if (!has1) has2 = layout->getWindowGeometry(toplevel, finalBox);
        wlr_log(WLR_INFO, "  has1 %d has2 %d finalBox %d,%d %dx%d hasBox %d", has1, has2, finalBox.x, finalBox.y, finalBox.width, finalBox.height, has1||has2);
        if (has1 || has2) {
            animateWindowOpen(toplevel, finalBox);
        } else {
            wlr_log(WLR_INFO, "  no geometry for toplevel, skipping open anim");
        }
    }
    emit toplevelMapped(toplevel);
}

void Engine::onToplevelUnmapped(Toplevel *toplevel)
{
    wlr_log(WLR_INFO, "Engine::onToplevelUnmapped tl %p id %lu", (void*)toplevel, toplevel ? (unsigned long)toplevel->id : 0);
    if (toplevel == cursorMgrObj->getGrabbed()) {
        cursorMgrObj->resetMode();
    }
    if (toplevel == detachedWindow) {
        detachedWindow = nullptr;
        detachedFromWorkspace = -1;
        detachedRatio = -1;
    }
    // Defer layout removal until destroy completes so close animation can obtain geometry.
    // Immediate layout cleanup for unmap without destroy is now handled in startCloseAnimation/fullCleanup.
    emit toplevelUnmapped(toplevel);
}

void Engine::startCloseAnimation(Toplevel *toplevel)
{
    if (!toplevel) return;
    wlr_log(WLR_INFO, "Engine::startCloseAnimation tl %p id %lu", (void*)toplevel, (unsigned long)toplevel->id);
    dumpDebugState();
    // Cancel any existing animation for this window
    if (animPool) animPool->removeAnimation(toplevel->id);
    int wsBefore = layout->getWindowWorkspace(toplevel);
    struct wlr_box curBox;
    bool hasBox = layout->getWindowGeometry(toplevel, curBox);
    wlr_log(WLR_INFO, "startCloseAnimation hasBox %d curBox %d,%d %dx%d", hasBox, curBox.x, curBox.y, curBox.width, curBox.height);
    if (!hasBox) {
        // fallback immediate destroy
        toplevels.removeOne(toplevel);
        layout->removeWindow(toplevel);
        rearrangeTiled();
        emit toplevelUnmapped(toplevel);
        if (toplevel->getSceneTree()) toplevel->getSceneTree()->node.data = nullptr;
        emit toplevel->destroyed();
        toplevel->destroyCloseSnapshot();
        delete toplevel;
        return;
    }
    const AnimDef *closeDef = nullptr;
    bool shouldAnimate = false;
    if (config && config->animations.enabled && animPool) {
        auto it = config->animations.pairs.find("window");
        if (it != config->animations.pairs.end()) {
            wlr_log(WLR_INFO, "Close anim check: hasEnd %d", it->second.hasEnd());
            if (it->second.hasEnd()) {
                wlr_log(WLR_INFO, "Close anim end def enabled %d duration %d style %d", it->second.end->enabled, it->second.end->duration, (int)it->second.end->style);
            }
        }
        if (it != config->animations.pairs.end() && it->second.hasEnd() && it->second.end->enabled && it->second.end->duration > 0) {
            shouldAnimate = true;
            closeDef = &*it->second.end;
        }
    }
    if (!shouldAnimate || !closeDef) {
        // Immediate removal
        toplevels.removeOne(toplevel);
        layout->removeWindow(toplevel);
        rearrangeTiled();
        if (wsBefore > 0) for (Output *out : outputs) if (out->getWorkspace() == wsBefore) arrangeForOutput(out);
        emit toplevelUnmapped(toplevel);
        if (toplevel->getSceneTree()) toplevel->getSceneTree()->node.data = nullptr;
        emit toplevel->destroyed();
        toplevel->destroyCloseSnapshot();
        delete toplevel;
        return;
    }

    toplevel->closeAnimationRunning = true;
    toplevel->createCloseSnapshot();
    bool isFade = (closeDef->style == AnimationStyle::Fade || closeDef->style == AnimationStyle::FadeWindowLayer);

    // Deinit layout-related state before animation so other windows can move immediately.
    // Keep snapshot/scene tree alive for visual animation only.
    {
        int ws = wsBefore > 0 ? wsBefore : layout->getWindowWorkspace(toplevel);
        toplevels.removeOne(toplevel);
        layout->removeWindow(toplevel);
        rearrangeTiled();
        if (ws > 0) {
            for (Output *out : outputs) if (out->getWorkspace() == ws) arrangeForOutput(out);
        }
        emit toplevelUnmapped(toplevel);
        if (toplevel->getSceneTree()) wlr_scene_node_set_enabled(&toplevel->getSceneTree()->node, false);
    }

    auto finalCleanup = [this, toplevel]() {
        if (!toplevel->closeAnimationRunning) return;
        wlr_log(WLR_INFO, "Close animation finished, final cleanup tl %p", (void*)toplevel);
        toplevel->closeAnimationRunning = false;
        toplevel->destroyCloseSnapshot();
        // Clear scene tree data pointer to avoid dangling references in hit testing
        if (toplevel->getSceneTree()) {
            toplevel->getSceneTree()->node.data = nullptr;
        }
        emit toplevel->destroyed();
        delete toplevel;
    };

    if (isFade) {
        toplevel->animOpacity = 1.0f;
        if (toplevel->closeSnapshot) {
            wlr_scene_node_set_position(&toplevel->closeSnapshot->node, curBox.x, curBox.y);
            wlr_scene_buffer_set_dest_size(toplevel->closeSnapshot, curBox.width, curBox.height);
            wlr_scene_buffer_set_opacity(toplevel->closeSnapshot, 1.0f);
        } else {
            // Fallback: animate scene tree opacity directly if snapshot unavailable
            wlr_log(WLR_INFO, "Close snapshot missing for fade, falling back to immediate cleanup");
            finalCleanup();
            return;
        }
        std::array<float*,10> ptrs = {&toplevel->animOpacity, nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};
        std::array<float,10> targets = {0.0f, 0,0,0,0,0,0,0,0,0};
        auto apply = [toplevel](){
            if (toplevel->closeSnapshot) wlr_scene_buffer_set_opacity(toplevel->closeSnapshot, toplevel->animOpacity);
        };
        auto *inst = animPool->addInstanceWithDef<float>(*toplevel, ptrs, targets, 1, *closeDef, apply);
        if (inst) {
            if (animPool) animPool->onFrame();
            connect(inst, &Resource::resourceDestroyed, this, [finalCleanup](Resource*){ finalCleanup(); });
        } else {
            finalCleanup();
        }
        return;
    }

    struct wlr_box endBox = boxForStyle(closeDef->style, curBox, false);
    toplevel->animX = curBox.x;
    toplevel->animY = curBox.y;
    toplevel->animW = curBox.width;
    toplevel->animH = curBox.height;
    toplevel->animOpacity = 1.0f;
    bool useSnapshot = (toplevel->closeSnapshot != nullptr);
    if (useSnapshot) {
        wlr_scene_node_set_position(&toplevel->closeSnapshot->node, curBox.x, curBox.y);
        wlr_scene_buffer_set_dest_size(toplevel->closeSnapshot, curBox.width, curBox.height);
        wlr_scene_buffer_set_opacity(toplevel->closeSnapshot, 1.0f);
    } else {
        wlr_log(WLR_INFO, "Close snapshot missing, animating live scene tree directly");
        wlr_scene_node_set_position(&toplevel->getSceneTree()->node, curBox.x, curBox.y);
        wlr_xdg_toplevel_set_size(toplevel->get(), curBox.width, curBox.height);
    }
    std::array<int*,10> ptrs = {&toplevel->animX, &toplevel->animY, &toplevel->animW, &toplevel->animH, nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};
    std::array<int,10> targets = {endBox.x, endBox.y, endBox.width, endBox.height, 0,0,0,0,0,0};
    auto applyBox = [toplevel](){
        if (toplevel->closeSnapshot) {
            wlr_scene_node_set_position(&toplevel->closeSnapshot->node, toplevel->animX, toplevel->animY);
            wlr_scene_buffer_set_dest_size(toplevel->closeSnapshot, toplevel->animW, toplevel->animH);
            wlr_scene_buffer_set_opacity(toplevel->closeSnapshot, toplevel->animOpacity);
        } else if (toplevel->getSceneTree()) {
            wlr_scene_node_set_position(&toplevel->getSceneTree()->node, toplevel->animX, toplevel->animY);
            wlr_xdg_toplevel_set_size(toplevel->get(), toplevel->animW, toplevel->animH);
        }
    };
    // Opacity target based on endPercent, default 0 = fully transparent
    float endOpacity = closeDef->endPercent >= 0 ? closeDef->endPercent / 100.0f : 0.0f;
    auto applyOpacity = [toplevel](){
        if (toplevel->closeSnapshot) {
            wlr_scene_buffer_set_opacity(toplevel->closeSnapshot, toplevel->animOpacity);
        }
    };
    std::array<float*,10> oPtrs = {&toplevel->animOpacity, nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};
    std::array<float,10> oTargets = {endOpacity, 0,0,0,0,0,0,0,0,0};

    auto *boxInst = animPool->addInstanceWithDef<int>(*toplevel, ptrs, targets, 4, *closeDef, applyBox);
    auto *opaInst = animPool->addInstanceWithDef<float>(*toplevel, oPtrs, oTargets, 1, *closeDef, applyOpacity);
    if (boxInst && opaInst) {
        if (animPool) animPool->onFrame();
        connect(boxInst, &Resource::resourceDestroyed, this, [finalCleanup](Resource*){ finalCleanup(); });
        connect(opaInst, &Resource::resourceDestroyed, this, [finalCleanup](Resource*){ finalCleanup(); });
    } else {
        finalCleanup();
    }
}

// Focus management

void Engine::focusToplevel(Toplevel *toplevel)
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
    if (decorManager) decorManager->setFocusedToplevel(toplevel);
    // Focus animation (paired start, optional end)
    if (config && config->animations.enabled) {
        auto it = config->animations.pairs.find("focus");
        if (it != config->animations.pairs.end() && it->second.start.enabled && it->second.start.duration > 0) {
            struct wlr_box cur;
            if (layout->getWindowGeometry(toplevel, cur)) {
                // quick pop: if style is pop/scale, use it, else just fade via no-op
                struct wlr_box start = boxForStyle(it->second.start.style, cur, true);
                // if fade, start==cur so no visual, but still schedule for consistency
                animateBoxForToplevel(toplevel, start, cur, it->second.start, "focus:");
            }
        }
    }

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

Output *Engine::outputForToplevel(Toplevel *toplevel)
{
    if (outputs.isEmpty()) return nullptr;
    if (toplevel && layout) {
        int ws = layout->getWindowWorkspace(toplevel);
        if (ws > 0) {
            for (Output *out : outputs) if (out->getWorkspace() == ws) return out;
        }
    }
    // Fallback: output containing cursor
    if (cursor && outputLayout) {
        struct wlr_output *wout = wlr_output_layout_output_at(outputLayout, cursor->x, cursor->y);
        if (wout) for (Output *out : outputs) if (out->get() == wout) return out;
    }
    return outputs.first();
}

void Engine::setInitialLayoutMode(const QString &mode)
{
    QString m = mode.toLower().trimmed();
    LayoutManager::Mode lm = LayoutManager::Mode::Tiling;
    if (m == "floating")
        lm = LayoutManager::Mode::Floating;
    else if (m == "monowindow")
        lm = LayoutManager::Mode::MonoWindow;
    layout->setWorkspaceLayoutMode(1, lm);
}

struct wlr_box Engine::usableAreaForOutput(struct wlr_output *wlr_output)
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

struct wlr_box Engine::fullAreaForOutput(struct wlr_output *wlr_output)
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
    return full;
}

void Engine::arrangeForOutput(Output *out)
{
    if (!out) return;
    struct wlr_box usable = usableAreaForOutput(out->get());
    // Apply decoration gaps to usable area (outer_gap shrinks, inner handled by layout spacing if configured)
    if (decorManager) {
        int outer = decorManager->config().outerGap;
        if (outer > 0) { usable.x += outer; usable.y += outer; usable.width -= 2*outer; usable.height -= 2*outer; if (usable.width<0) usable.width=0; if (usable.height<0) usable.height=0; }
    }
    struct wlr_box full = fullAreaForOutput(out->get());
    // Snapshot before for tilingMove animation (generic lerp with window-layer-only)
    std::unordered_map<Toplevel*, struct wlr_box> beforeBoxes;
    bool doTilingAnim = config && config->animations.enabled && layout->getWorkspaceLayoutMode(out->getWorkspace())==LayoutManager::Mode::Tiling;
    int ws = out->getWorkspace();
    if (doTilingAnim) {
        // Capture current scene positions as animation start points
        for (Toplevel *tl : toplevels) {
            if (layout->getWindowWorkspace(tl) != ws) continue;
            struct wlr_box b;
            b.x = tl->getSceneTree()->node.x;
            b.y = tl->getSceneTree()->node.y;
            // Use current surface size as starting size
            struct wlr_surface *surf = tl->get()->base->surface;
            if (surf) {
                b.width = surf->current.width;
                b.height = surf->current.height;
            } else {
                struct wlr_box geo = tl->get()->base->geometry;
                b.width = geo.width;
                b.height = geo.height;
            }
            beforeBoxes[tl] = b;
        }
    }
    layout->arrange(usable, full, out->getWorkspace());
    if (doTilingAnim) {
        auto afterBoxes = layout->snapshotGeometries(out->getWorkspace());
        // windowLayerOnly true means we only animate windows, which we already do
        animateTilingMove(beforeBoxes, afterBoxes);
    }
    // Sync window decorations geometries after arrange
    if (decorManager) {
        for (Toplevel *tl : toplevels) {
            if (layout->getWindowWorkspace(tl) != out->getWorkspace()) continue;
            struct wlr_box box;
            bool has = layout->getWindowGeometry(tl, out->getWorkspace(), usable, box);
            if (!has) has = layout->getWindowGeometry(tl, box);
            if (!has) {
                // fallback to scene node pos + xdg geometry
                auto *node = tl->getSceneTree();
                struct wlr_box *geo = &tl->get()->base->geometry;
                box = {node->node.x + geo->x, node->node.y + geo->y, geo->width, geo->height};
                if (box.width==0) box.width=800;
                if (box.height==0) box.height=600;
            }
            if (auto *dec = decorManager->decorationFor(tl)) {
                dec->updateGeometry(box.x, box.y, box.width, box.height);
                // hide decorations for fullscreen/maximized windows if needed (border stays but titlebar maybe hidden)
                bool isFs = layout->isFullscreen(tl);
                bool isMx = layout->isMaximized(tl);
                // For fullscreen, hide borders/title; for maximized keep border but no outer gap
                dec->setVisible(!(isFs));
                if (isMx && decorManager->config().border.enabled) dec->setVisible(true);
            }
        }
    }
    int ws2 = out->getWorkspace();
    // Handle layer visibility for fullscreen (hide shell) and ensure stacking respects popupTree
    bool isFs = layout->getFullscreenWindow(ws2) != nullptr;
    for (LayerSurface *ls : layers) {
        if (ls->get()->output != out->get()) continue;
        struct wlr_scene_layer_surface_v1 *sl = ls->getSceneLayer();
        if (!sl) continue;
        if (isFs) {
            wlr_scene_node_set_enabled(&sl->tree->node, false);
        } else {
            // Restore visibility based on mapped state
            bool shouldEnable = ls->get()->surface->mapped;
            wlr_scene_node_set_enabled(&sl->tree->node, shouldEnable);
        }
    }
    // Fix stacking: keep windows below popupTree (as per popup fix 42765e7)
    if (Toplevel *fs = layout->getFullscreenWindow(ws)) {
        if (fs->getSceneTree() && popupTree) {
            wlr_scene_node_place_below(&fs->getSceneTree()->node, &popupTree->node);
        }
    } else if (Toplevel *mx = layout->getMaximizedWindow(ws)) {
        if (mx->getSceneTree() && popupTree) {
            wlr_scene_node_place_below(&mx->getSceneTree()->node, &popupTree->node);
        }
    } else {
        // Tiling with floating windows: floating above tiled but below popup
        auto floating = layout->getFloatingWindows(ws);
        for (Toplevel *fw : floating) {
            if (fw->getSceneTree() && popupTree) {
                wlr_scene_node_place_below(&fw->getSceneTree()->node, &popupTree->node);
            }
        }
    }
}

void Engine::rearrangeTiled()
{
    for (Output *out : outputs) {
        int ws = out->getWorkspace();
        if (layout->getWorkspaceLayoutMode(ws) == LayoutManager::Mode::Tiling
            || layout->getFullscreenWindow(ws)
            || layout->getMaximizedWindow(ws))
            arrangeForOutput(out);
    }
}

bool Engine::setFullscreen(Toplevel *toplevel, bool fullscreen)
{
    if (!toplevel || !layout) return false;
    int ws = layout->getWindowWorkspace(toplevel);
    if (ws < 0) return false;
    Output *out = outputForToplevel(toplevel);
    struct wlr_box full = out ? fullAreaForOutput(out->get()) : (struct wlr_box){0,0,1920,1080};
    struct wlr_box beforeBox; bool hasBefore = layout->getWindowGeometry(toplevel, beforeBox);
    bool changed = layout->setFullscreen(toplevel, fullscreen, full);
    if (!changed) {
        wlr_xdg_toplevel_set_fullscreen(toplevel->get(), fullscreen);
        if (out) arrangeForOutput(out);
        wlr_xdg_surface_schedule_configure(toplevel->get()->base);
        return false;
    }
    wlr_xdg_toplevel_set_fullscreen(toplevel->get(), fullscreen);
    if (fullscreen) {
        if (layout->isMaximized(toplevel)) layout->setMaximized(toplevel, false);
        wlr_xdg_toplevel_set_maximized(toplevel->get(), false);
    }
    for (Output *o : outputs) if (o->getWorkspace() == ws) arrangeForOutput(o);
    wlr_xdg_surface_schedule_configure(toplevel->get()->base);
    if (fullscreen) focusToplevel(toplevel);
    // animate fullscreen enter/exit if pair exists
    if (hasBefore && config && config->animations.enabled) {
        auto it = config->animations.pairs.find("fullscreen");
        if (it != config->animations.pairs.end()) {
            const AnimDef *def = nullptr;
            if (fullscreen) def = &it->second.start;
            else if (it->second.hasEnd()) def = &*it->second.end;
            if (def && def->enabled && def->duration>0) {
                struct wlr_box afterBox = fullscreen ? full : beforeBox;
                if (!fullscreen) {
                    // for exit, try to get tiled geometry after arrange
                    struct wlr_box tmp;
                    if (layout->getWindowGeometry(toplevel, ws, usableAreaForOutput(out->get()), tmp)) afterBox = tmp;
                }
                animateBoxForToplevel(toplevel, beforeBox, afterBox, *def, fullscreen ? "fullscreenEnter:" : "fullscreenExit:");
            } else if (fullscreen && !it->second.hasEnd()) {
                // non-reversible case not needed
            }
        }
    }
    return true;
}

bool Engine::setMaximized(Toplevel *toplevel, bool maximized)
{
    if (!toplevel || !layout) return false;
    int ws = layout->getWindowWorkspace(toplevel);
    if (ws < 0) return false;
    if (maximized && layout->isFullscreen(toplevel)) return false;
    if (maximized && layout->getFullscreenWindow(ws)) return false;
    struct wlr_box beforeBoxMax; bool hasBeforeMax = layout->getWindowGeometry(toplevel, beforeBoxMax);
    bool changed = layout->setMaximized(toplevel, maximized);
    if (!changed) {
        wlr_xdg_toplevel_set_maximized(toplevel->get(), maximized);
        Output *out = outputForToplevel(toplevel);
        if (out && layout->getWindowWorkspace(toplevel) == ws) arrangeForOutput(out);
        wlr_xdg_surface_schedule_configure(toplevel->get()->base);
        return false;
    }
    wlr_xdg_toplevel_set_maximized(toplevel->get(), maximized);
    for (Output *o : outputs) if (o->getWorkspace() == ws) arrangeForOutput(o);
    wlr_xdg_surface_schedule_configure(toplevel->get()->base);
    if (maximized) focusToplevel(toplevel);
    if (hasBeforeMax && config && config->animations.enabled) {
        auto it = config->animations.pairs.find("maximize");
        if (it != config->animations.pairs.end()) {
            const AnimDef *def = maximized ? &it->second.start : (it->second.hasEnd()? &*it->second.end : nullptr);
            if (def && def->enabled && def->duration>0) {
                struct wlr_box afterBox = beforeBoxMax;
                Output *outM = outputForToplevel(toplevel);
                if (maximized) afterBox = usableAreaForOutput(outM ? outM->get() : nullptr);
                else {
                    struct wlr_box tmp;
                    if (layout->getWindowGeometry(toplevel, ws, usableAreaForOutput(outM?outM->get():nullptr), tmp)) afterBox = tmp;
                }
                animateBoxForToplevel(toplevel, beforeBoxMax, afterBox, *def, maximized ? "maximizeEnter:" : "maximizeExit:");
            }
        }
    }
    return true;
}

// Input device helpers

void Engine::addKeyboard(struct wlr_input_device *device)
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
                    } else if (act == "swap_orientation" || act == "toggle_split" || act == "toggle_orientation") {
                        Toplevel *focused = nullptr;
                        if (seat->keyboard_state.focused_surface) {
                            struct wlr_xdg_toplevel *tl = wlr_xdg_toplevel_try_from_wlr_surface(seat->keyboard_state.focused_surface);
                            if (tl) {
                                for (Toplevel *t : toplevels) if (t->get() == tl) { focused = t; break; }
                            }
                        }
                        if (focused) {
                            int ws = layout->getWindowWorkspace(focused);
                            if (ws > 0 && layout->getWorkspaceLayoutMode(ws) == LayoutManager::Mode::Tiling) {
                                bool ok = layout->toggleSplitOrientation(ws, focused);
                                if (ok) {
                                    for (Output *o : outputs) if (o->getWorkspace() == ws) arrangeForOutput(o);
                                } else {
                                    wlr_log(WLR_INFO, "swap_orientation: no branch to toggle (single leaf)");
                                }
                            }
                        } else {
                            wlr_log(WLR_INFO, "swap_orientation: no focused window");
                        }
                    } else if (act == "toggle_floating" || act == "toggle_float" || act == "floating_toggle" || act == "set_floating") {
                        Toplevel *focused = nullptr;
                        if (seat->keyboard_state.focused_surface) {
                            struct wlr_xdg_toplevel *tl = wlr_xdg_toplevel_try_from_wlr_surface(seat->keyboard_state.focused_surface);
                            if (tl) {
                                for (Toplevel *t : toplevels) if (t->get() == tl) { focused = t; break; }
                            }
                        }
                        if (focused) {
                            int ws = layout->getWindowWorkspace(focused);
                            if (ws > 0) {
                                struct wlr_box usable = {0,0,1920,1080};
                                for (Output *o : outputs) if (o->getWorkspace() == ws) { usable = usableAreaForOutput(o->get()); break; }
                                if (usable.width==0) {
                                    for (Output *o : outputs) if (!outputs.isEmpty()) { usable = usableAreaForOutput(outputs.first()->get()); break; }
                                }
                                // Determine target state: toggle unless arg forces
                                bool makeFloating = !layout->isFloating(focused);
                                if (kbnd->arg == "on" || kbnd->arg == "true" || kbnd->arg == "1") makeFloating = true;
                                else if (kbnd->arg == "off" || kbnd->arg == "false" || kbnd->arg == "0") makeFloating = false;
                                // Allow floating in any mode but most useful in tiling; still handle.
                                auto mode = layout->getWorkspaceLayoutMode(ws);
                                if (mode == LayoutManager::Mode::Tiling || makeFloating || kbnd->arg == "toggle") {
                                    struct wlr_box beforeFloat; bool hasBeforeFloat = layout->getWindowGeometry(focused, beforeFloat);
                                    bool ok = layout->setFloating(focused, makeFloating, usable);
                                    if (ok) {
                                        for (Output *o : outputs) if (o->getWorkspace() == ws) arrangeForOutput(o);
                                        // Floating toggle animation (paired start/end)
                                        if (hasBeforeFloat && config && config->animations.enabled) {
                                            auto itf = config->animations.pairs.find("floating");
                                            if (itf != config->animations.pairs.end()) {
                                                const AnimDef *def = makeFloating ? &itf->second.start : (itf->second.hasEnd() ? &*itf->second.end : nullptr);
                                                if (def && def->enabled && def->duration>0) {
                                                    struct wlr_box afterFloat = beforeFloat;
                                                    struct wlr_box tmp; if (layout->getWindowGeometry(focused, ws, usable, tmp)) afterFloat = tmp; else if (layout->getWindowGeometry(focused, tmp)) afterFloat = tmp;
                                                    struct wlr_box s = boxForStyle(def->style, afterFloat, true);
                                                    if (makeFloating) animateBoxForToplevel(focused, s, afterFloat, *def, "floatingEnter:");
                                                    else {
                                                        struct wlr_box e = boxForStyle(def->style, beforeFloat, false);
                                                        animateBoxForToplevel(focused, beforeFloat, e, *def, "floatingExit:");
                                                    }
                                                }
                                            }
                                        }
                                        // Raise floating window to top and refocus
                                        focusToplevel(focused);
                                    } else {
                                        // Fallback to toggle if setFloating failed due to same state (e.g. no workspace)
                                        if (layout->toggleFloating(focused, usable)) {
                                            for (Output *o : outputs) if (o->getWorkspace() == ws) arrangeForOutput(o);
                                            focusToplevel(focused);
                                        }
                                    }
                                } else {
                                    // In floating/monowindow mode, unfloating means tiling
                                    bool ok = layout->setFloating(focused, makeFloating, usable);
                                    if (ok) for (Output *o : outputs) if (o->getWorkspace() == ws) arrangeForOutput(o);
                                }
                                wlr_log(WLR_INFO, "toggle_floating: %s ws %d floating=%d", focused->get()->title ? focused->get()->title : "window", ws, layout->isFloating(focused));
                            }
                        } else {
                            wlr_log(WLR_INFO, "toggle_floating: no focused window");
                        }
                    } else if (act == "toggle_fullscreen" || act == "fullscreen" || act == "toggle_fullscreened" || act == "set_fullscreen") {
                        Toplevel *focused = nullptr;
                        if (seat->keyboard_state.focused_surface) {
                            struct wlr_xdg_toplevel *tl = wlr_xdg_toplevel_try_from_wlr_surface(seat->keyboard_state.focused_surface);
                            if (tl) for (Toplevel *t : toplevels) if (t->get() == tl) { focused = t; break; }
                        }
                        if (focused) {
                            bool isFs = layout->isFullscreen(focused);
                            bool want = !isFs;
                            if (kbnd->arg == "on" || kbnd->arg == "true" || kbnd->arg == "1") want = true;
                            else if (kbnd->arg == "off" || kbnd->arg == "false" || kbnd->arg == "0") want = false;
                            setFullscreen(focused, want);
                            wlr_log(WLR_INFO, "toggle_fullscreen: %s fullscreen=%d", focused->get()->title ? focused->get()->title : "window", want);
                        } else {
                            wlr_log(WLR_INFO, "toggle_fullscreen: no focused window");
                        }
                    } else if (act == "toggle_maximize" || act == "maximize" || act == "toggle_maximized" || act == "set_maximize" || act == "set_maximized") {
                        Toplevel *focused = nullptr;
                        if (seat->keyboard_state.focused_surface) {
                            struct wlr_xdg_toplevel *tl = wlr_xdg_toplevel_try_from_wlr_surface(seat->keyboard_state.focused_surface);
                            if (tl) for (Toplevel *t : toplevels) if (t->get() == tl) { focused = t; break; }
                        }
                        if (focused) {
                            bool isMx = layout->isMaximized(focused);
                            bool want = !isMx;
                            if (kbnd->arg == "on" || kbnd->arg == "true" || kbnd->arg == "1") want = true;
                            else if (kbnd->arg == "off" || kbnd->arg == "false" || kbnd->arg == "0") want = false;
                            // If fullscreen active, refuse maximize
                            if (want && layout->isFullscreen(focused)) {
                                wlr_log(WLR_INFO, "toggle_maximize: window is fullscreen, ignoring");
                            } else {
                                setMaximized(focused, want);
                                wlr_log(WLR_INFO, "toggle_maximize: %s maximized=%d", focused->get()->title ? focused->get()->title : "window", want);
                            }
                        } else {
                            wlr_log(WLR_INFO, "toggle_maximize: no focused window");
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

void Engine::addMouse(struct wlr_input_device *device)
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

Engine::Engine(const Astick &app, Config *cfg)
    : config(cfg)
{
    connect(&app, &Astick::aboutToRun, this, &Engine::run);

    connect(this, &Engine::outputAdded, this, &Engine::onOutputAdded);
    connect(this, &Engine::toplevelAdded, this, &Engine::onToplevelAdded);
    connect(this, &Engine::popupAdded, this, &Engine::onPopupAdded);
    connect(this, &Engine::layerAdded, this, &Engine::onLayerAdded);
    connect(this, &Engine::setSelection, this, &Engine::onSetSelection);
    connect(this, &Engine::inputAdded, this, &Engine::onInputAdded);

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
    if (config) {
        layout->setDefaultSplitRatio(config->bsp.split_ratio);
        layout->setOppositeOrientation(config->bsp.opposite_orientation);
        layout->setKeepRatioOnDrop(config->bsp.keep_ratio_on_drop);
        layout->setMinRatio(config->bsp.min_ratio);
        layout->setMaxRatio(config->bsp.max_ratio);
    }
    animManager = new AnimationManager(this);
    animPool = new AnimationPool(config, this);
    // Apply animations config immediately (global speed + per-anim toggles)
    if (config) {
        animManager->setEnabled(config->animations.enabled);
        animManager->setGlobalSpeed(config->animations.speed);
        for (auto &kv : config->animations.presets) {
            animManager->setAnimationEnabled(QString::fromStdString(kv.first), kv.second.enabled);
        }
        if (config->animations.presets.count("border")) {
            animManager->setAnimationEnabled("border", config->animations.presets.at("border").enabled);
        }
        animPool->setConfig(config);
    }
    connect(animPool, &AnimationPool::frameRequested, this, &Engine::scheduleAllOutputs);
    // Decoration manager: convert Config decoration data to runtime QColor-based config
    auto makeDecorationConfig = [&](const DecorationConfigData &d) -> DecorationConfig {
        DecorationConfig dc;
        dc.border.enabled = d.border.enabled;
        dc.border.width = d.border.width;
        dc.border.radius = d.border.radius;
        dc.border.activeColor = QColor(QString::fromStdString(d.border.active_color));
        if (!dc.border.activeColor.isValid()) dc.border.activeColor = QColor("#ff5500");
        dc.border.inactiveColor = QColor(QString::fromStdString(d.border.inactive_color));
        if (!dc.border.inactiveColor.isValid()) dc.border.inactiveColor = QColor("#3a3a3a");
        dc.border.gradient.enabled = d.border.gradient.enabled;
        dc.border.gradient.angle = d.border.gradient.angle;
        dc.border.gradient.animate = d.border.gradient.animate;
        dc.border.gradient.colors.clear();
        for (auto &cstr : d.border.gradient.colors) {
            QColor c(QString::fromStdString(cstr));
            if (c.isValid()) dc.border.gradient.colors.append(c);
        }
        dc.border.animate = d.border.animate;
        dc.border.animationDuration = d.border.animation_duration;
        dc.border.animationEasing = QString::fromStdString(d.border.animation_easing);
        dc.titlebar.enabled = d.titlebar.enabled;
        dc.titlebar.height = d.titlebar.height;
        dc.titlebar.color = QColor(QString::fromStdString(d.titlebar.color));
        dc.titlebar.textColor = QColor(QString::fromStdString(d.titlebar.text_color));
        dc.titlebar.fontSize = d.titlebar.font_size;
        dc.titlebar.showTitle = d.titlebar.show_title;
        dc.titlebar.showButtons = d.titlebar.show_buttons;
        dc.outerGap = d.outer_gap;
        dc.innerGap = d.inner_gap;
        return dc;
    };
    DecorationConfig initDec = config ? makeDecorationConfig(config->decorations) : DecorationConfig{};
    decorManager = new DecorationManager(initDec, animManager, this);
    // Animation drives max FPS: request frame on all outputs each tick
    connect(animManager, &AnimationManager::frameRequested, this, &Engine::scheduleAllOutputs);

    connect(cursorMgrObj, &CursorManager::interactiveEnded, this, [this](Toplevel *toplevel, CursorMode mode) {
        if (mode == CURSOR_MOVE && toplevel == detachedWindow && detachedFromWorkspace > 0) {
            Output *out = outputForToplevel(toplevel);
            if (!out && !outputs.isEmpty()) out = outputs.first();
            if (out) {
                struct wlr_box usable = usableAreaForOutput(out->get());
                double cx = cursor ? cursor->x : usable.x + usable.width/2.0;
                double cy = cursor ? cursor->y : usable.y + usable.height/2.0;
                double ratioHint = -1;
                if (config && config->bsp.keep_ratio_on_drop && detachedRatio > 0) {
                    ratioHint = detachedRatio;
                } else if (layout) {
                    ratioHint = layout->getDefaultSplitRatio();
                }
                // Use BSP cursor insertion: closest leaf to mouse, preserving ratio
                layout->insertWindowAtCursor(toplevel, detachedFromWorkspace, usable, cx, cy, ratioHint);
            } else {
                layout->addWindow(toplevel, detachedFromWorkspace);
            }
            rearrangeTiled();
            detachedWindow = nullptr;
            detachedFromWorkspace = -1;
            detachedRatio = -1;
        } else if (mode == CURSOR_RESIZE && toplevel) {
            // Hyprland-like: ratio already updated during drag via handleResize, just ensure final sync
            Output *out = outputForToplevel(toplevel);
            if (!out && !outputs.isEmpty()) out = outputs.first();
            if (out) arrangeForOutput(out);
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

Engine::~Engine()
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
        // Animation and decoration managers are QObjects parented to Compositor; delete handled by QObject hierarchy,
        // but clear pointers
        animManager = nullptr;
        decorManager = nullptr;

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

void Engine::run()
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

void Engine::closePopup(Popup *popup)
{
    if (popup == nullptr) return;
    wlr_log(WLR_INFO, "Closing popup");
    wlr_xdg_popup_destroy(popup->get());
}

void Engine::scheduleAllOutputs() {
    wlr_log(WLR_INFO, "scheduleAllOutputs: scheduling %zu outputs", outputs.size());
    for (Output *o : outputs) {
        if (o && o->get()) wlr_output_schedule_frame(o->get());
    }
}

// Animation helper implementations

struct wlr_box Engine::boxForStyle(AnimationStyle style, const struct wlr_box &finalBox, bool isStart) {
    // For start, offset away from final; for end, offset outward from start
    switch(style){
        case AnimationStyle::Fade: return finalBox;
        case AnimationStyle::ScaleIn: {
            int w = int(finalBox.width * 0.94);
            int h = int(finalBox.height * 0.94);
            int dx = (finalBox.width - w)/2;
            int dy = (finalBox.height - h)/2;
            if(isStart) return {finalBox.x+dx, finalBox.y+dy, w, h};
            else { // scaleOut end smaller
                int w2 = int(finalBox.width * 0.75);
                int h2 = int(finalBox.height * 0.75);
                int dx2=(finalBox.width - w2)/2;
                int dy2=(finalBox.height - h2)/2;
                return {finalBox.x+dx2, finalBox.y+dy2, w2, h2};
            }
        }
        case AnimationStyle::ScaleOut: {
            // pop in/out
            if (isStart) {
                int w = finalBox.width;
                int h = finalBox.height;
                int dx=(finalBox.width - w)/2;
                int dy=(finalBox.height - h)/2;
                return {finalBox.x+dx, finalBox.y+dy, w, h};
            } else {
                // shrink to center point for close
                int cx = finalBox.x + finalBox.width/2;
                int cy = finalBox.y + finalBox.height/2;
                return {cx, cy, 1, 1};
            }
        }
        case AnimationStyle::Pop: {
            int w = int(finalBox.width * 0.6);
            int h = int(finalBox.height * 0.6);
            int dx=(finalBox.width - w)/2;
            int dy=(finalBox.height - h)/2;
            return {finalBox.x+dx, finalBox.y+dy, w, h};
        }
        case AnimationStyle::SlideTop: return {finalBox.x, finalBox.y - finalBox.height/3, finalBox.width, finalBox.height};
        case AnimationStyle::SlideBottom: return {finalBox.x, finalBox.y + finalBox.height/3, finalBox.width, finalBox.height};
        case AnimationStyle::SlideLeft: return {finalBox.x - finalBox.width/3, finalBox.y, finalBox.width, finalBox.height};
        case AnimationStyle::SlideRight: return {finalBox.x + finalBox.width/3, finalBox.y, finalBox.width, finalBox.height};
        case AnimationStyle::SlideFade:
        case AnimationStyle::Slide: {
            // generic slide left for start
            return {finalBox.x - finalBox.width/4, finalBox.y, finalBox.width, finalBox.height};
        }
        case AnimationStyle::Cube: {
            // cube approx as slide + scale down
            int w = int(finalBox.width * 0.9);
            int h = int(finalBox.height * 0.9);
            int dx=(finalBox.width - w)/2 + finalBox.width/5;
            int dy=(finalBox.height - h)/2;
            return {finalBox.x+dx, finalBox.y+dy, w, h};
        }
        case AnimationStyle::FadeWindowLayer: return finalBox;
        default: return finalBox;
    }
}

void Engine::animateBoxForToplevel(Toplevel *tl, struct wlr_box from, struct wlr_box to, const AnimDef &def, const QString &id) {
    wlr_log(WLR_INFO, "animateBoxForToplevel tl %p id %lu from %d,%d %dx%d to %d,%d %dx%d def %d %s", (void*)tl, tl ? (unsigned long)tl->id : 0, from.x, from.y, from.width, from.height, to.x, to.y, to.width, to.height, def.duration, def.easing.c_str());
    if (!tl || !tl->getSceneTree()) {
        wlr_log(WLR_INFO, "  no tl or sceneTree");
        return;
    }
    if (tl->closeAnimationRunning) {
        wlr_log(WLR_INFO, "  close animation running, skip");
        return;
    }
    if (!config || !config->animations.enabled || !animPool) {
        wlr_log(WLR_INFO, "  no config or anim disabled or no pool config %p enabled %d pool %p", (void*)config, config?config->animations.enabled:0, (void*)animPool);
        wlr_scene_node_set_position(&tl->getSceneTree()->node, to.x, to.y);
        wlr_xdg_toplevel_set_size(tl->get(), to.width, to.height);
        return;
    }
    if (!def.enabled || def.duration <= 0) {
        wlr_log(WLR_INFO, "  def disabled");
        wlr_scene_node_set_position(&tl->getSceneTree()->node, to.x, to.y);
        wlr_xdg_toplevel_set_size(tl->get(), to.width, to.height);
        return;
    }
    // Initialize anim fields to from (so pool captures start correctly)
    tl->animX = from.x;
    tl->animY = from.y;
    tl->animW = from.width;
    tl->animH = from.height;
    wlr_scene_node_set_position(&tl->getSceneTree()->node, from.x, from.y);
    wlr_xdg_toplevel_set_size(tl->get(), from.width, from.height);
    std::array<int*,10> ptrs = {&tl->animX, &tl->animY, &tl->animW, &tl->animH, nullptr,nullptr,nullptr,nullptr,nullptr,nullptr};
    std::array<int,10> targets = {to.x, to.y, to.width, to.height, 0,0,0,0,0,0};
    auto apply = [tl](){
        if(tl && tl->getSceneTree()){
            wlr_scene_node_set_position(&tl->getSceneTree()->node, tl->animX, tl->animY);
            wlr_xdg_toplevel_set_size(tl->get(), tl->animW, tl->animH);
        }
    };
    // Use new pool with explicit def
    AnimationInstance<int> *inst = animPool->addInstanceWithDef<int>(*tl, ptrs, targets, 4, def, apply);
    Q_UNUSED(inst);
    Q_UNUSED(id);
}

void Engine::animateTilingMove(const std::unordered_map<Toplevel*, struct wlr_box> &before, const std::unordered_map<Toplevel*, struct wlr_box> &after) {
    wlr_log(WLR_INFO, "animateTilingMove: before %zu after %zu", before.size(), after.size());
    if (!config || !config->animations.enabled) {
        wlr_log(WLR_INFO, "  no config or disabled");
        return;
    }
    if (cursorMgrObj && cursorMgrObj->getMode() == CURSOR_RESIZE) {
        wlr_log(WLR_INFO, "  cursor resizing, skip tiling animation");
        return;
    }
    auto it = config->animations.pairs.find("tilingMove");
    if (it == config->animations.pairs.end()) {
        wlr_log(WLR_INFO, "  no tilingMove pair");
        return;
    }
    const AnimDef &def = it->second.start;
    wlr_log(WLR_INFO, "  def enabled %d duration %d", def.enabled, def.duration);
    if (!def.enabled || def.duration<=0) return;
    for (auto &p : after) {
        Toplevel *tl = p.first;
        auto bit = before.find(tl);
        if (bit == before.end()) {
            wlr_log(WLR_INFO, "  tl %p id %lu not in before, skip", (void*)tl, (unsigned long)tl->id);
            continue;
        }
        struct wlr_box from = bit->second;
        struct wlr_box to = p.second;
        wlr_log(WLR_INFO, "  tl %p id %lu from %d,%d %dx%d to %d,%d %dx%d", (void*)tl, (unsigned long)tl->id, from.x, from.y, from.width, from.height, to.x, to.y, to.width, to.height);
        if (from.x==to.x && from.y==to.y && from.width==to.width && from.height==to.height) {
            wlr_log(WLR_INFO, "    no change, skip");
            continue;
        }
        if (tl->closeAnimationRunning) {
            wlr_log(WLR_INFO, "    close animation running, skip tiling move");
            continue;
        }
        animateBoxForToplevel(tl, from, to, def, "tilingMove:");
    }
}

void Engine::animateWindowOpen(Toplevel *tl, const struct wlr_box &finalBox) {
    wlr_log(WLR_INFO, "animateWindowOpen: tl %p id %lu final %d,%d %dx%d", (void*)tl, tl ? (unsigned long)tl->id : 0, finalBox.x, finalBox.y, finalBox.width, finalBox.height);
    if (!tl) return;
    if (!config || !config->animations.enabled) {
        wlr_log(WLR_INFO, "  no config or anim disabled");
        return;
    }
    auto it = config->animations.pairs.find("window");
    if (it==config->animations.pairs.end()) {
        wlr_log(WLR_INFO, "  no window pair");
        return;
    }
    const AnimDef &def = it->second.start;
    wlr_log(WLR_INFO, "  def enabled %d duration %d style %d", def.enabled, def.duration, (int)def.style);
    if (!def.enabled || def.duration<=0) {
        wlr_scene_node_set_position(&tl->getSceneTree()->node, finalBox.x, finalBox.y);
        wlr_xdg_toplevel_set_size(tl->get(), finalBox.width, finalBox.height);
        return;
    }
    struct wlr_box start = boxForStyle(def.style, finalBox, true);
    wlr_log(WLR_INFO, "  start %d,%d %dx%d", start.x, start.y, start.width, start.height);
    animateBoxForToplevel(tl, start, finalBox, def, "windowOpen:");
}

void Engine::animateWindowClose(Toplevel *tl, const struct wlr_box &curBox) {
    if (!tl) return;
    if (!config || !config->animations.enabled) return;
    auto it = config->animations.pairs.find("window");
    if (it==config->animations.pairs.end()) return;
    if (!it->second.hasEnd()) {
        wlr_log(WLR_ERROR, "window animation reversible but missing end - skipping close anim");
        return;
    }
    const AnimDef &def = *it->second.end;
    if (!def.enabled || def.duration<=0) return;
    struct wlr_box end = boxForStyle(def.style, curBox, false);
    struct wlr_box startBox = curBox;
    // start at 96% size for scale styles to match open start
    if (def.style == AnimationStyle::ScaleOut || def.style == AnimationStyle::ScaleIn) {
        float f = 0.96f;
        int w = int(curBox.width * f);
        int h = int(curBox.height * f);
        int dx = (curBox.width - w)/2;
        int dy = (curBox.height - h)/2;
        startBox = {curBox.x + dx, curBox.y + dy, w, h};
    }
    // Keep node enabled until anim finishes; caller must defer removal
    animateBoxForToplevel(tl, startBox, end, def, "windowClose:");
    // The finished callback will be overridden by caller to do removal; we set a temporary one that keeps visibility
}

void Engine::animatePopupOpen(Popup *popup) {
    if (!popup || !popup->get() || !popup->get()->base || !popup->get()->base->data) return;
    if (!config || !config->animations.enabled) return;
    auto it = config->animations.pairs.find("popup");
    if (it==config->animations.pairs.end()) return;
    const AnimDef &def = it->second.start;
    if (!def.enabled || def.duration<=0) return;
    auto *tree = (struct wlr_scene_tree*)popup->get()->base->data;
    if (!tree) return;
    struct wlr_box end = {tree->node.x, tree->node.y, 200,200}; // fallback size not accurate
    // Try to get size from popup geometry if available
    if (popup->get()->base->surface && popup->get()->base->surface->current.width>0)
        end = {tree->node.x, tree->node.y, popup->get()->base->surface->current.width, popup->get()->base->surface->current.height};
    struct wlr_box s = boxForStyle(def.style, end, true);
    QString id = QString("popupOpen:%1").arg((quintptr)popup,16);
    animManager->remove(id);
    Animation *anim = animManager->create(id, int(def.duration / config->animations.speed), Animation::easingFromString(QString::fromStdString(def.easing)));
    wlr_scene_node_set_position(&tree->node, s.x, s.y);
    anim->setUpdateCallback([tree,s,end](double e){ struct wlr_box cur=easedLerpBox(s,end,e); wlr_scene_node_set_position(&tree->node, cur.x, cur.y); });
    anim->start();
}

void Engine::animatePopupClose(Popup *popup) {
    // No-op for now (popup destroy is immediate)
    (void)popup;
}

void Engine::animateWorkspaceSwitch(Output *out, int oldWs, int newWs, const struct wlr_box &usable) {
    if (!out || oldWs==newWs) return;
    if (!config || !config->animations.enabled) {
        layout->deactivateWorkspace(oldWs);
        layout->activateWorkspace(newWs);
        arrangeForOutput(out);
        return;
    }
    auto it = config->animations.pairs.find("workspaceSwitch");
    if (it==config->animations.pairs.end()) {
        layout->deactivateWorkspace(oldWs);
        layout->activateWorkspace(newWs);
        arrangeForOutput(out);
        return;
    }
    const AnimPair &pair = it->second;
    bool isForward = newWs > oldWs;
    const AnimDef &def = isForward ? pair.start : (pair.end ? *pair.end : pair.start);
    if (!def.enabled || def.duration<=0) {
        layout->deactivateWorkspace(oldWs);
        layout->activateWorkspace(newWs);
        arrangeForOutput(out);
        return;
    }
    // Ensure both workspaces' windows are visible during animation (window-layer-only)
    layout->activateWorkspace(newWs); // enable new windows (they may have been disabled)
    // Keep old enabled for duration; will deactivate in finished callback
    bool winOnly = config->animations.windowLayerOnly;
    // Collect snapshots for both workspaces using current usable
    auto beforeBoxes = layout->snapshotGeometries(oldWs, usable);
    auto afterBoxes = layout->snapshotGeometries(newWs, usable);
    // For afterBoxes, compute final positions (they will be arranged)
    // We need final boxes for newWs after arrange; but arrange already called? Call snapshot after arrange
    // This method is called from workspaceChanged before rearrange? We'll handle both.
    // For slide/fade/cube, we animate window layer nodes offset
    // Simplified: offset new windows offscreen and slide in, old windows slide out
    struct wlr_box outBox = fullAreaForOutput(out->get());
    // Determine offset direction
    int dx=0, dy=0;
    switch(def.style){
        case AnimationStyle::SlideLeft: dx = -outBox.width; break;
        case AnimationStyle::SlideRight: dx = outBox.width; break;
        case AnimationStyle::SlideTop: dy = -outBox.height; break;
        case AnimationStyle::SlideBottom: dy = outBox.height; break;
        case AnimationStyle::Cube:
        case AnimationStyle::Slide: dx = isForward ? outBox.width : -outBox.width; break;
        case AnimationStyle::Fade:
        case AnimationStyle::FadeWindowLayer: dx=0; dy=0; break;
        default: dx = isForward ? outBox.width : -outBox.width; break;
    }
    auto easing = Animation::easingFromString(QString::fromStdString(def.easing));
    QString baseId = QString("wsSwitch:%1->%2:").arg(oldWs).arg(newWs);
    // For old workspace windows: animate out
    for(auto &kv : beforeBoxes){
        Toplevel* tl = kv.first;
        struct wlr_box from = kv.second;
        struct wlr_box to = from;
        if(def.style==AnimationStyle::Fade || def.style==AnimationStyle::FadeWindowLayer){
            to = from;
        } else {
            to.x += dx;
            to.y += dy;
        }
        QString id = baseId + QString::number((quintptr)tl,16) + ":old";
        animManager->remove(id);
        Animation* anim = animManager->create(id, int(def.duration/2 / config->animations.speed), easing);
        anim->setUpdateCallback([tl, from, to](double e){ struct wlr_box cur=easedLerpBox(from,to,e); if(tl&&tl->getSceneTree()) wlr_scene_node_set_position(&tl->getSceneTree()->node, cur.x, cur.y); });
        anim->start();
    }
    // Need final positions after arrange; if not yet arranged, compute via layout
    for(auto &kv : afterBoxes){
        Toplevel* tl = kv.first;
        struct wlr_box to = kv.second;
        struct wlr_box from = to;
        if(def.style==AnimationStyle::Fade || def.style==AnimationStyle::FadeWindowLayer){
            from = to;
        } else {
            from.x -= dx;
            from.y -= dy;
        }
        if(tl && tl->getSceneTree()) wlr_scene_node_set_position(&tl->getSceneTree()->node, from.x, from.y);
        QString id = baseId + QString::number((quintptr)tl,16) + ":new";
        animManager->remove(id);
        Animation* anim = animManager->create(id, int(def.duration/2 / config->animations.speed), easing);
        anim->setUpdateCallback([tl, from, to](double e){ struct wlr_box cur=easedLerpBox(from,to,e); if(tl&&tl->getSceneTree()) wlr_scene_node_set_position(&tl->getSceneTree()->node, cur.x, cur.y); });
        anim->start();
    }
    if(!winOnly){
        // would animate layer surfaces as well; not yet implemented
    }
    // Schedule final workspace activation/deactivation after anim duration
    QString doneId = baseId + "done";
    animManager->remove(doneId);
    Animation *done = animManager->create(doneId, int(def.duration / config->animations.speed), easing);
    done->setFinishedCallback([this, out, oldWs, newWs, usable](){
        layout->deactivateWorkspace(oldWs);
        arrangeForOutput(out);
    });
    done->start();
}

void Engine::applyConfigDecorations() {
    if (!config || !decorManager || !animManager) return;
    // animations
    animManager->setEnabled(config->animations.enabled);
    animManager->setGlobalSpeed(config->animations.speed);
    for (auto &kv : config->animations.presets) {
        animManager->setAnimationEnabled(QString::fromStdString(kv.first), kv.second.enabled);
    }
    for (auto &kv : config->animations.pairs) {
        animManager->setAnimationEnabled(QString::fromStdString(kv.first + ":start"), kv.second.start.enabled);
        if (kv.second.hasEnd()) animManager->setAnimationEnabled(QString::fromStdString(kv.first + ":end"), kv.second.end->enabled);
        // also enable base id
        animManager->setAnimationEnabled(QString::fromStdString(kv.first), kv.second.start.enabled);
    }
    // decorations
    DecorationConfig dc;
    dc.border.enabled = config->decorations.border.enabled;
    dc.border.width = config->decorations.border.width;
    dc.border.radius = config->decorations.border.radius;
    dc.border.activeColor = QColor(QString::fromStdString(config->decorations.border.active_color));
    dc.border.inactiveColor = QColor(QString::fromStdString(config->decorations.border.inactive_color));
    dc.border.gradient.enabled = config->decorations.border.gradient.enabled;
    dc.border.gradient.angle = config->decorations.border.gradient.angle;
    dc.border.gradient.animate = config->decorations.border.gradient.animate;
    dc.border.gradient.colors.clear();
    for (auto &s : config->decorations.border.gradient.colors) {
        QColor c(QString::fromStdString(s));
        if (c.isValid()) dc.border.gradient.colors.append(c);
    }
    dc.border.animate = config->decorations.border.animate;
    dc.border.animationDuration = config->decorations.border.animation_duration;
    dc.border.animationEasing = QString::fromStdString(config->decorations.border.animation_easing);
    dc.titlebar.enabled = config->decorations.titlebar.enabled;
    dc.titlebar.height = config->decorations.titlebar.height;
    dc.titlebar.color = QColor(QString::fromStdString(config->decorations.titlebar.color));
    dc.titlebar.textColor = QColor(QString::fromStdString(config->decorations.titlebar.text_color));
    dc.titlebar.fontSize = config->decorations.titlebar.font_size;
    dc.titlebar.showTitle = config->decorations.titlebar.show_title;
    dc.titlebar.showButtons = config->decorations.titlebar.show_buttons;
    dc.outerGap = config->decorations.outer_gap;
    dc.innerGap = config->decorations.inner_gap;
    decorManager->setConfig(dc);
    // reflow
    for (Output *o : outputs) arrangeForOutput(o);
}

void Engine::dumpDebugState() {
    QJsonObject root;
    QJsonArray windows;
    for (auto *tl : toplevels) {
        struct wlr_box box;
        bool has = layout->getWindowGeometry(tl, box);
        QJsonObject obj;
        obj["id"] = static_cast<qint64>(tl->id);
        obj["closeAnimationRunning"] = tl->closeAnimationRunning;
        obj["animX"] = tl->animX;
        obj["animY"] = tl->animY;
        obj["animW"] = tl->animW;
        obj["animH"] = tl->animH;
        obj["hasBox"] = has;
        if (has) {
            obj["x"] = box.x;
            obj["y"] = box.y;
            obj["w"] = box.width;
            obj["h"] = box.height;
        }
        windows.append(obj);
    }
    root["toplevels"] = windows;
    root["animPoolExists"] = animPool != nullptr;
    QFile f("/tmp/astick_state.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    }
}


std::vector<astick::VariantEvent> Engine::poll() {
    return {};
}

void Engine::onCommand(const astick::VariantCommand &cmd) {
    // Simple apply for State commands — keep zero-copy visitor
    std::visit(astick::Overloaded{
        [this](const astick::Cmd::SetWindowBox &c){
            auto it = std::find_if(toplevels.begin(), toplevels.end(), [&](Toplevel* tl){ return tl->id == c.id; });
            if (it != toplevels.end() && *it) {
                wlr_scene_node_set_position(&(*it)->getSceneTree()->node, c.box.x, c.box.y);
                wlr_xdg_toplevel_set_size((*it)->get(), c.box.width, c.box.height);
            }
        },
        [](const auto&){},
    }, cmd);
}
