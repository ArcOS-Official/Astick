#pragma once
#include <QObject>
#include <QList>
#include <QString>
#include "../wlroots.h"
#include "../config.h"
#include "../layout.h"
#include "../output.h"
#include "../toplevel.h"
#include "../popup.h"
#include "../layersurface.h"
#include "../cursor.h"
#include "../input/keyboard.h"
#include "../input/mouse.h"
#include "../animation.h"
#include "../animation_pool.h"
#include "../decoration.h"
#include "../state/state.h"
#include "../state/event.h"
#include "../state/command.h"

namespace astick {

class Engine : public QObject, public CommandReceiver, public EventSource {
    Q_OBJECT
public:
    explicit Engine(State &state, Config &config, QObject *parent = nullptr);
    ~Engine() override;

    void init();
    void hookState();
    int run();

    std::vector<VariantEvent> poll() override;
    std::optional<int> nextWakeupMs() const override { return std::nullopt; }
    void onCommand(const VariantCommand &cmd) override;

    wlr_scene *scene() const { return scene_; }
    wlr_output_layout *outputLayout() const { return outputLayout_; }
    wlr_seat *seat() const { return seat_; }
    wlr_cursor *cursor() const { return cursor_; }
    wl_display *display() const { return display_; }
    LayoutManager *layout() const { return layout_; }

    // Compatibility aliases for old Compositor API used by Toplevel/Popup/LayerSurface/Cursor
    wlr_seat *getSeat() const { return seat_; }
    wlr_scene *getScene() const { return scene_; }
    wlr_output_layout *getOutputLayout() const { return outputLayout_; }
    wlr_scene_tree *getLayerTree(uint32_t l) const { return layerTrees[l]; }
    wlr_scene_tree *getPopupTree() const { return popupTree_; }
    QList<::Output*>& getOutputs() { return outputs_; }
    QList<::Toplevel*>& getToplevels() { return toplevels_; }
    wlr_cursor *getCursor() const { return cursor_; }
    wlr_xcursor_manager *getCursorMgr() const { return cursorMgr_; }
    Config *getConfig() { return &config_; }
    LayoutManager *getLayout() { return layout_; }
    ::AnimationManager *getAnimationManager() { return animManager_; }
    ::DecorationManager *getDecorationManager() { return decorManager_; }
    void startCloseAnimation(::Toplevel*) {}
    void setInitialLayoutMode(const QString &mode);

    void focusToplevel(::Toplevel *t);
    ::Output *outputForToplevel(::Toplevel *t);
    struct wlr_box usableAreaForOutput(wlr_output *output);
    struct wlr_box fullAreaForOutput(wlr_output *output);
    void rearrangeTiled();
    void arrangeForOutput(::Output *out);

signals:
    void ready();

private slots:
    void onOutputAdded(struct wlr_output *output);
    void onToplevelAdded(struct wlr_xdg_toplevel *toplevel);
    void onPopupAdded(struct wlr_xdg_popup *popup);
    void onLayerAdded(struct wlr_layer_surface_v1 *surface);
    void onSetSelection(wlr_seat_request_set_selection_event *event);
    void onInputAdded(wlr_input_device *device);

private:
    void addKeyboard(struct wlr_input_device *device);
    void addMouse(struct wlr_input_device *device);
    void scheduleAllOutputs();

    struct wlr_box boxForStyle(AnimationStyle style, const wlr_box &finalBox, bool isStart);

    State &state_;
    Config &config_;
    bool hooked_ = false;

    wl_display *display_ = nullptr;
    wl_event_loop *loop_ = nullptr;
    wlr_backend *backend_ = nullptr;
    wlr_renderer *renderer_ = nullptr;
    wlr_allocator *allocator_ = nullptr;
    wlr_scene *scene_ = nullptr;
    wlr_scene_output_layout *sceneLayout_ = nullptr;
    wlr_output_layout *outputLayout_ = nullptr;
    wlr_xdg_shell *xdgShell_ = nullptr;
    wlr_layer_shell_v1 *layerShell_ = nullptr;
    wlr_scene_tree *layerTrees[4] = {};
    wlr_scene_tree *popupTree_ = nullptr;
    wlr_cursor *cursor_ = nullptr;
    wlr_xcursor_manager *cursorMgr_ = nullptr;
    wlr_seat *seat_ = nullptr;
    QString socket_;

    QList<::Output*> outputs_;
    QList<::Keyboard*> keyboards_;
    QList<::Mouse*> mice_;
    QList<::Toplevel*> toplevels_;
    QList<::Popup*> popups_;
    QList<::LayerSurface*> layers_;

    ::CursorManager *cursorMgrObj_ = nullptr;
    ::LayoutManager *layout_ = nullptr;
    ::AnimationManager *animManager_ = nullptr;
    ::AnimationPool *animPool_ = nullptr;
    ::DecorationManager *decorManager_ = nullptr;

    ::Toplevel *detachedWindow_ = nullptr;
    int detachedFromWorkspace_ = -1;
    double detachedRatio_ = -1;

    WindowId nextWindowId_ = 1;
    std::unordered_map<WindowId, ::Toplevel*> windowMap_;
    std::vector<VariantEvent> pendingEvents_;

    wl_listener newOutputListener_{};
    wl_listener newToplevelListener_{};
    wl_listener newPopupListener_{};
    wl_listener newLayerListener_{};
    wl_listener newInputListener_{};
    wl_listener setSelectionListener_{};

    void handleNewOutput(wl_listener *listener, void *data);
    void handleNewToplevel(wl_listener *listener, void *data);
    void handleNewPopup(wl_listener *listener, void *data);
    void handleNewLayer(wl_listener *listener, void *data);
    void handleNewInput(wl_listener *listener, void *data);
    void handleSetSelection(wl_listener *listener, void *data);

    friend void engine_handle_newOutput(wl_listener*, void*);
    friend void engine_handle_newToplevel(wl_listener*, void*);
    friend void engine_handle_newPopup(wl_listener*, void*);
    friend void engine_handle_newLayer(wl_listener*, void*);
    friend void engine_handle_newInput(wl_listener*, void*);
    friend void engine_handle_setSelection(wl_listener*, void*);

    void applySetWindowBox(const Cmd::SetWindowBox &c);
    void applySetWindowActivated(const Cmd::SetWindowActivated &c);
};

} // namespace astick
