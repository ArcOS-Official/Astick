#pragma once

#include <QtCore>
#include <QObject>
#include <qobject.h>
#include "wlroots.h"
#include "output.h"
#include "input/keyboard.h"
#include "input/mouse.h"
#include "application.h"

class Compositor;

enum CursorMode {
    CURSOR_PASSTHROUGH,
    CURSOR_MOVE,
    CURSOR_RESIZE,
};

class Toplevel : public QObject
{
    Q_OBJECT

public:
    Toplevel(
        Compositor *server,
        struct wlr_xdg_toplevel *toplevel,
        struct wlr_scene_tree *sceneTree
    );
    struct wlr_xdg_toplevel *get() const { return toplevel; }
    struct wlr_scene_tree *getSceneTree() const { return sceneTree; }
    Compositor *getServer() const { return server; }

    friend void handle_map(wl_listener *listener, void *data);
    friend void handle_unmap(wl_listener *listener, void *data);
    friend void handle_commit(wl_listener *listener, void *data);
    friend void handle_destroy(wl_listener *listener, void *data);
    friend void handle_request_move(wl_listener *listener, void *data);
    friend void handle_request_resize(wl_listener *listener, void *data);
    friend void handle_request_maximize(wl_listener *listener, void *data);
    friend void handle_request_fullscreen(wl_listener *listener, void *data);

signals:
    void mapped();
    void unmapped();
    void committed();
    void destroyed();
    void moveRequested();
    void resizeRequested(uint32_t edges);
    void maximizeRequested();
    void fullscreenRequested();

private:
    struct wlr_xdg_toplevel *toplevel;
    struct wlr_scene_tree *sceneTree;
    Compositor *server;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
};

class Popup : public QObject
{
    Q_OBJECT

public:
    Popup(
        Compositor *server,
        struct wlr_xdg_popup *popup_
    );
    friend void handle_popup_commit(wl_listener *listener, void *data);
    friend void handle_popup_destroy(wl_listener *listener, void *data);

signals:
    void committed();
    void destroyed();
    void closed();

private:
    Compositor *server;
    struct wlr_xdg_popup *popup;
    struct wl_listener commit;
    struct wl_listener destroy;
};

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

public slots:
    void run();
    void closePopup();

signals:
    void outputAdded(struct wlr_output *output);
    void outputRemoved(struct wlr_output *output);
    void toplevelAdded(struct wlr_xdg_toplevel *toplevel);
    void toplevelMapped(Toplevel *toplevel);
    void toplevelUnmapped(Toplevel *toplevel);
    void popupAdded(struct wlr_xdg_popup *popup);
    void cursorMoved(struct wlr_pointer_motion_event *event);
    void cursorMovedAbsolute(struct wlr_pointer_motion_absolute_event *event);
    void cursorButton(struct wlr_pointer_button_event *event);
    void cursorAxis(struct wlr_pointer_axis_event *event);
    void cursorFrame();
    void requestSetCursor(struct wlr_seat_pointer_request_set_cursor_event *event);
    void requestSetSelection(struct wlr_seat_request_set_selection_event *event);
    void inputAdded(struct wlr_input_device *device);

private slots:
    void onOutputAdded(struct wlr_output *output);
    void onToplevelAdded(struct wlr_xdg_toplevel *xtoplevel);
    void onPopupAdded(struct wlr_xdg_popup *xpopup);
    void onCursorMoved(struct wlr_pointer_motion_event *event);
    void onCursorMovedAbsolute(struct wlr_pointer_motion_absolute_event *event);
    void onCursorButton(struct wlr_pointer_button_event *event);
    void onCursorAxis(struct wlr_pointer_axis_event *event);
    void onCursorFrame();
    void onRequestSetCursor(struct wlr_seat_pointer_request_set_cursor_event *event);
    void onRequestSetSelection(struct wlr_seat_request_set_selection_event *event);
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

    CursorMode cursorMode = CURSOR_PASSTHROUGH;
    Toplevel *grabbedToplevel = nullptr;
    double grabX = 0, grabY = 0;
    struct wlr_box grabGeobox = {};
    uint32_t resizeEdges = 0;

    struct wl_listener newOutputListener;
    struct wl_listener newXdgToplevelNotifyListener;
    struct wl_listener newXdgPopupNotifyListener;
    struct wl_listener cursorMotionListener;
    struct wl_listener cursorMotionAbsoluteListener;
    struct wl_listener cursorButtonListener;
    struct wl_listener cursorAxisListener;
    struct wl_listener cursorFrameListener;
    struct wl_listener requestCursorListener;
    struct wl_listener setSelectionListener;
    struct wl_listener newInputListener;

    friend void handle_newOutput(wl_listener *listener, void *data);
    friend void handle_newXdgToplevelNotify(wl_listener *listener, void *data);
    friend void handle_newXdgPopupNotify(wl_listener *listener, void *data);
    friend void handle_cursorMotion(wl_listener *listener, void *data);
    friend void handle_cursorMotionAbsolute(wl_listener *listener, void *data);
    friend void handle_cursorButton(wl_listener *listener, void *data);
    friend void handle_cursorAxis(wl_listener *listener, void *data);
    friend void handle_cursorFrame(wl_listener *listener, void *data);
    friend void handle_requestCursor(wl_listener *listener, void *data);
    friend void handle_setSelection(wl_listener *listener, void *data);
    friend void handle_newInput(wl_listener *listener, void *data);
    friend class Toplevel;
    friend class Popup;

    void focusToplevel(Toplevel *toplevel);
    Toplevel *toplevelAt(double lx, double ly, struct wlr_surface **surface, double *sx, double *sy);
    void processCursorMotion(uint32_t time);
    void processCursorMove();
    void processCursorResize();
    void resetCursorMode();
    void beginInteractive(Toplevel *toplevel, CursorMode mode, uint32_t edges);
    void arrangeToplevels();
    void addKeyboard(struct wlr_input_device *device);
    void addMouse(struct wlr_input_device *device);
};