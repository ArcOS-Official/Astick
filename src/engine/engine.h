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

#include <QtCore>
#include <QObject>
#include <qobject.h>
#include <QList>
#include "../wlroots.h"
#include "../output.h"
#include "../input/keyboard.h"
#include "../input/mouse.h"
#include "../toplevel.h"
#include "../popup.h"
#include "../layersurface.h"
#include "../cursor.h"
#include "../layout.h"
#include "../application.h"
#include "../config.h"
#include "../animation.h"
#include "../animation_pool.h"
#include "../decoration.h"
#include "../state/state.h"
#include "../state/event.h"
#include "../state/command.h"

class Engine : public QObject, public astick::CommandReceiver, public astick::EventSource
{
    Q_OBJECT

public:
    explicit Engine(const Astick &app, Config *cfg = nullptr);
    ~Engine();

    struct wlr_scene *getScene() {
        return scene;
    }
    struct wlr_output_layout *getOutputLayout() {
        return outputLayout;
    }
    struct wlr_scene_tree *getLayerTree(uint32_t layer) {
        return layerTrees[layer];
    }
    struct wlr_scene_tree *getPopupTree() {
        return popupTree;
    }
    struct wl_display *getDisplay() {
        return display;
    }
    struct wlr_seat *getSeat() {
        return seat;
    }
    struct wlr_cursor *getCursor() {
        return cursor;
    }
    struct wlr_xcursor_manager *getCursorMgr() {
        return cursorMgr;
    }
    QList<Toplevel *> &getToplevels() {
        return toplevels;
    }
    QList<Output *> &getOutputs() {
        return outputs;
    }

    void focusToplevel(Toplevel *toplevel);
    Output *outputForToplevel(Toplevel *toplevel);
    void setInitialLayoutMode(const QString &mode);
    struct wlr_box usableAreaForOutput(struct wlr_output *output);
    struct wlr_box fullAreaForOutput(struct wlr_output *output);
    Config* getConfig() {
        return config;
    }
    LayoutManager *getLayout() {
        return layout;
    }
    AnimationManager *getAnimationManager() {
        return animManager;
    }
    DecorationManager *getDecorationManager() {
        return decorManager;
    }
    void rearrangeTiled();
    void arrangeForOutput(Output *out);
    bool setFullscreen(Toplevel *toplevel, bool fullscreen);
    bool setMaximized(Toplevel *toplevel, bool maximized);
    void applyConfigDecorations(); // sync Config -> runtime managers
    void scheduleAllOutputs(); // force frame at max fps (used by AnimationManager)

public slots:
    void run();
    void closePopup(Popup *popup);

signals:
    void outputAdded(struct wlr_output *output);
    void toplevelAdded(struct wlr_xdg_toplevel *toplevel);
    void toplevelMapped(Toplevel *toplevel);
    void toplevelUnmapped(Toplevel *toplevel);
    void popupAdded(struct wlr_xdg_popup *popup);
    void layerAdded(struct wlr_layer_surface_v1 *surface);
    void setSelection(struct wlr_seat_request_set_selection_event *event);
    void inputAdded(struct wlr_input_device *device);

private slots:
    void onOutputAdded(struct wlr_output *output);
    void onToplevelAdded(struct wlr_xdg_toplevel *xtoplevel);
    void onPopupAdded(struct wlr_xdg_popup *xpopup);
    void onLayerAdded(struct wlr_layer_surface_v1 *lsurface);
    void onSetSelection(struct wlr_seat_request_set_selection_event *event);
    void onInputAdded(struct wlr_input_device *device);
    void onToplevelMapped(Toplevel *toplevel);
    void onToplevelUnmapped(Toplevel *toplevel);

public:
    void startCloseAnimation(Toplevel *toplevel);

    // State integration
    void setState(astick::IEngineState *s) {
        state_ = s;
    }
    std::vector<astick::VariantEvent> poll() override;
    std::optional<int> nextWakeupMs() const override { return std::nullopt; }
    void onCommand(const astick::VariantCommand &cmd) override;

private:
    bool initialized = false;
    struct wl_display *display = nullptr;
    struct wl_event_loop *loop = nullptr;
    struct wlr_backend *backend = nullptr;
    struct wlr_renderer *renderer = nullptr;
    struct wlr_allocator *allocator = nullptr;
    struct wlr_scene *scene = nullptr;
    struct wlr_scene_output_layout *sceneLayout = nullptr;
    struct wlr_output_layout *outputLayout = nullptr;
    struct wlr_xdg_shell *xdgShell = nullptr;
    struct wlr_layer_shell_v1 *layerShell = nullptr;
    struct wlr_scene_tree *layerTrees[4] = {};
    struct wlr_scene_tree *popupTree = nullptr;
    struct wlr_cursor *cursor = nullptr;
    struct wlr_xcursor_manager *cursorMgr = nullptr;
    struct wlr_seat *seat = nullptr;
    QString socket;
    QList<Output *> outputs;
    QList<Keyboard *> keyboards;
    QList<Mouse *> mice;
    QList<Toplevel *> toplevels;
    QList<Popup *> popups;
    QList<LayerSurface *> layers;

    CursorManager *cursorMgrObj = nullptr;
    LayoutManager *layout = nullptr;
    AnimationManager *animManager = nullptr;
    AnimationPool *animPool = nullptr;
    DecorationManager *decorManager = nullptr;
    Config *config = nullptr;
    astick::IEngineState *state_ = nullptr;

    Toplevel *detachedWindow = nullptr;
    int detachedFromWorkspace = -1;
    double detachedRatio = -1;

    struct wl_listener newOutputListener;
    struct wl_listener newXdgToplevelNotifyListener;
    struct wl_listener newXdgPopupNotifyListener;
    struct wl_listener newLayerNotifyListener;
    struct wl_listener setSelectionListener;
    struct wl_listener newInputListener;

    friend void handle_newOutput(wl_listener *listener, void *data);
    friend void handle_newXdgToplevelNotify(wl_listener *listener, void *data);
    friend void handle_newXdgPopupNotify(wl_listener *listener, void *data);
    friend void handle_newLayerNotify(wl_listener *listener, void *data);
    friend void handle_setSelection(wl_listener *listener, void *data);
    friend void handle_newInput(wl_listener *listener, void *data);

    void addKeyboard(struct wlr_input_device *device);
    void addMouse(struct wlr_input_device *device);

    // Animation helpers (new pool, target-driven)
    struct wlr_box boxForStyle(AnimationStyle style, const struct wlr_box &finalBox, bool isStart);
    void animateBoxForToplevel(Toplevel *tl, struct wlr_box from, struct wlr_box to, const AnimDef &def, const QString &id);
    void animateBoxForToplevelPool(Toplevel *tl, struct wlr_box from, struct wlr_box to, const std::string &kind);
    void animateTilingMove(const std::unordered_map<Toplevel*, struct wlr_box> &before, const std::unordered_map<Toplevel*, struct wlr_box> &after);
    void animateWindowOpen(Toplevel *tl, const struct wlr_box &finalBox);
    void animateWindowClose(Toplevel *tl, const struct wlr_box &curBox);
    void animateWorkspaceSwitch(Output *out, int oldWs, int newWs, const struct wlr_box &usable);
    void animatePopupOpen(Popup *popup);
    void animatePopupClose(Popup *popup);
    void dumpDebugState();
};
