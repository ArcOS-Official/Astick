#pragma once

#include <QObject>
#include <qobject.h>
#include "wlroots.h"

class Compositor;
class Toplevel;
struct wlr_cursor;
struct wlr_xcursor_manager;

enum CursorMode {
    CURSOR_PASSTHROUGH,
    CURSOR_MOVE,
    CURSOR_RESIZE,
};

class CursorManager : public QObject
{
    Q_OBJECT

public:
    CursorManager(Compositor *comp);
    ~CursorManager();

    struct wl_listener motionListener;
    struct wl_listener motionAbsoluteListener;
    struct wl_listener buttonListener;
    struct wl_listener axisListener;
    struct wl_listener frameListener;
    struct wl_listener requestCursorListener;

    friend void cursor_handle_motion(wl_listener *listener, void *data);
    friend void cursor_handle_motion_absolute(wl_listener *listener, void *data);
    friend void cursor_handle_button(wl_listener *listener, void *data);
    friend void cursor_handle_axis(wl_listener *listener, void *data);
    friend void cursor_handle_frame(wl_listener *listener, void *data);
    friend void cursor_handle_request_cursor(wl_listener *listener, void *data);

    void beginInteractive(Toplevel *toplevel, CursorMode mode, uint32_t edges);
    void resetMode();
    CursorMode getMode() const { return cursorMode; }
    Toplevel *getGrabbed() const { return grabbedToplevel; }

private:
    Compositor *compositor;

    CursorMode cursorMode = CURSOR_PASSTHROUGH;
    Toplevel *grabbedToplevel = nullptr;
    double grabX = 0, grabY = 0;
    struct wlr_box grabGeobox = {};
    uint32_t resizeEdges = 0;

    Toplevel *toplevelAt(double lx, double ly,
        struct wlr_surface **surface, double *sx, double *sy);
    void processMotion(uint32_t time);
    void processMove();
    void processResize();
};