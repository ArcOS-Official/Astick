#pragma once

#include <QtCore>
#include <QObject>
#include "wlroots.h"
#include "output.h"
#include "input/keyboard.h"
#include "input/mouse.h"
#include "application.h"

class Compositor : public QObject
{
    Q_OBJECT

public:

    explicit Compositor(const Astick &app);
    ~Compositor();

public slots:
    void run();

private:
    bool initialized = false;
    struct wl_display *display;
    struct wlr_backend *backend;
    struct wl_event_loop *loop;
    struct wlr_session *session;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_output_layout *outputLayout;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *sceneLayout;
    struct wlr_xdg_shell *xdgShell;
    struct wlr_cursor *cursor;
    struct wlr_cursor_manager *cursorManager;
    struct wlr_seat *seat;
    QString socket;
    QList<Output *> outputs;
    QList<Keyboard *> keyboards;
    QList<Mouse *> mice;
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

    void addKeyboard(struct wlr_input_device *device);
    void addMouse(struct wlr_input_device *device);
};
