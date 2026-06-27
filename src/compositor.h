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
#include "wlroots.h"
#include "output.h"
#include "input/keyboard.h"
#include "input/mouse.h"
#include "toplevel.h"
#include "popup.h"
#include "cursor.h"
#include "layout.h"
#include "application.h"

class Compositor : public QObject
{
    Q_OBJECT

public:
    explicit Compositor(const Astick &app);
    ~Compositor();

    struct wlr_scene *getScene() { return scene; }
    struct wlr_output_layout *getOutputLayout() { return outputLayout; }
    struct wl_display *getDisplay() { return display; }
    struct wlr_seat *getSeat() { return seat; }
    struct wlr_cursor *getCursor() { return cursor; }
    struct wlr_xcursor_manager *getCursorMgr() { return cursorMgr; }
    QList<Toplevel *> &getToplevels() { return toplevels; }
    QList<Output *> &getOutputs() { return outputs; }

    void focusToplevel(Toplevel *toplevel);
    Output *outputForToplevel(Toplevel *toplevel);

public slots:
    void run();
    void closePopup();

signals:
    void outputAdded(struct wlr_output *output);
    void toplevelAdded(struct wlr_xdg_toplevel *toplevel);
    void toplevelMapped(Toplevel *toplevel);
    void toplevelUnmapped(Toplevel *toplevel);
    void popupAdded(struct wlr_xdg_popup *popup);
    void setSelection(struct wlr_seat_request_set_selection_event *event);
    void inputAdded(struct wlr_input_device *device);

private slots:
    void onOutputAdded(struct wlr_output *output);
    void onToplevelAdded(struct wlr_xdg_toplevel *xtoplevel);
    void onPopupAdded(struct wlr_xdg_popup *xpopup);
    void onSetSelection(struct wlr_seat_request_set_selection_event *event);
    void onInputAdded(struct wlr_input_device *device);
    void onToplevelMapped(Toplevel *toplevel);
    void onToplevelUnmapped(Toplevel *toplevel);

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
    struct wlr_cursor *cursor = nullptr;
    struct wlr_xcursor_manager *cursorMgr = nullptr;
    struct wlr_seat *seat = nullptr;
    QString socket;
    QList<Output *> outputs;
    QList<Keyboard *> keyboards;
    QList<Mouse *> mice;
    QList<Toplevel *> toplevels;
    QList<Popup *> popups;

    CursorManager *cursorMgrObj = nullptr;
    LayoutManager *layout = nullptr;

    struct wl_listener newOutputListener;
    struct wl_listener newXdgToplevelNotifyListener;
    struct wl_listener newXdgPopupNotifyListener;
    struct wl_listener setSelectionListener;
    struct wl_listener newInputListener;

    friend void handle_newOutput(wl_listener *listener, void *data);
    friend void handle_newXdgToplevelNotify(wl_listener *listener, void *data);
    friend void handle_newXdgPopupNotify(wl_listener *listener, void *data);
    friend void handle_setSelection(wl_listener *listener, void *data);
    friend void handle_newInput(wl_listener *listener, void *data);

    void addKeyboard(struct wlr_input_device *device);
    void addMouse(struct wlr_input_device *device);
};
