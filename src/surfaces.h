#pragma once

#include "resource.h"
#include "wlroots.h"
#include <QObject>
#include <ctime>

class Compositor;
struct wlr_cursor;
struct wlr_xcursor_manager;

// --- Output ---
class Output : public Resource
{
    Q_OBJECT
public:
    uint64_t genId() override;
    Output(struct wlr_output *output_, struct wlr_renderer *renderer, struct wlr_allocator *allocator, struct wlr_scene *scene_);
    struct wlr_output *get() const;
    int getWorkspace() const;
    void setWorkspace(int ws);

    friend void onFrame(struct wl_listener *listener, void *data);
    friend void onRequestState(struct wl_listener *listener, void *data);
    friend void onDestroy(struct wl_listener *listener, void *data);

signals:
    void frameReady();
    void destroyed();
    void workspaceChanged(int oldWorkspace, int newWorkspace);

private:
    struct wlr_output *output;
    int workspace = 1;
    std::timespec lastFrame;
    std::timespec fpsTimer;
    int fpsFrames = 0;
    int fpsRendered = 0;
    struct wlr_scene_rect *background = nullptr;
    void renderFrame();
    struct wlr_scene *scene;
    struct wl_listener frameListener;
    struct wl_listener requestStateListener;
    struct wl_listener destroyListener;
};

// --- Toplevel ---
class Toplevel : public Resource
{
    Q_OBJECT
public:
    Toplevel(Compositor *server, struct wlr_xdg_toplevel *toplevel, struct wlr_scene_tree *sceneTree);
    struct wlr_xdg_toplevel *get() const;
    struct wlr_scene_tree *getSceneTree() const;
    Compositor *getServer() const;
    uint64_t genId() override;

    int animX = 0, animY = 0, animW = 0, animH = 0;
    void setTargetBox(const struct wlr_box &target, const std::string &kind, std::function<void()> apply = nullptr);

    struct wlr_scene_buffer *closeSnapshot = nullptr;
    struct wlr_buffer *closeBuffer = nullptr;
    bool closeAnimationRunning = false;
    bool pendingDestroy = false;
    float animOpacity = 1.0f;
    void createCloseSnapshot();
    void destroyCloseSnapshot();
    void destroy_cb(QObject *);

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

// --- Popup ---
class Popup : public Resource
{
    Q_OBJECT
public:
    uint64_t genId() override;
    Popup(Compositor *server, struct wlr_xdg_popup *popup_);
    struct wlr_xdg_popup *get() const;
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

// --- LayerSurface ---
class LayerSurface : public Resource
{
    Q_OBJECT
public:
    uint64_t genId() override;
    LayerSurface(Compositor *server, struct wlr_layer_surface_v1 *surface);
    struct wlr_layer_surface_v1 *get() const;
    struct wlr_scene_layer_surface_v1 *getSceneLayer() const;
    void configure();
    friend void handle_layer_map(wl_listener *listener, void *data);
    friend void handle_layer_unmap(wl_listener *listener, void *data);
    friend void handle_layer_commit(wl_listener *listener, void *data);
    friend void handle_layer_destroy(wl_listener *listener, void *data);
signals:
    void mapped();
    void unmapped();
    void committed();
    void destroyed();
private:
    Compositor *server;
    struct wlr_layer_surface_v1 *layer;
    struct wlr_scene_layer_surface_v1 *sceneLayer;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
};

// --- CursorManager ---
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
    CursorMode getMode() const;
    Toplevel *getGrabbed() const;
    uint32_t getResizeEdges() const;

signals:
    void interactiveEnded(Toplevel *toplevel, CursorMode mode);

private:
    Compositor *compositor;
    CursorMode cursorMode = CURSOR_PASSTHROUGH;
    Toplevel *grabbedToplevel = nullptr;
    double grabX = 0, grabY = 0;
    struct wlr_box grabGeobox = {};
    uint32_t resizeEdges = 0;

    Toplevel *toplevelAt(double lx, double ly, struct wlr_surface **surface, double *sx, double *sy);
    void processMotion(uint32_t time);
    void processMove();
    void processResize();
};
