#pragma once
// Sole wlroots include — no other file in src/state/* includes this.
#include "../wlroots.h"
#include "../state/state.h"
#include "../state/event.h"
#include "../state/command.h"
#include "../config.h"
#include <poll.h>
#include <sys/timerfd.h>
#include <unistd.h>
#include <unordered_map>

// Engine owns all wlr_* memory and is stale until hookState().
// Zero-copy: holds live event instances (kb/ptr/win/anim/out) and
// fills their public fields directly from wl_listeners — no new/delete
// per event. poll() moves them into VariantEvent via emplace.

class Engine : public EventSource, public CommandReceiver {
public:
    Engine(State& state, Config& cfg);
    ~Engine() override;

    // Create wl_display, backend, renderer, allocator, scene, seat, etc.
    // Installs wl_listeners that enqueue by filling public fields (no alloc).
    void init();

    void hookState(); // stale -> ready, after State wiring
    int run(); // custom loop — replaces wl_event_loop dispatch

    // EventSource impl — drains internal wlEnqueued queue filled by wl_listeners
    std::vector<VariantEvent> poll() override;
    std::optional<int> nextWakeupMs() const override { return std::nullopt; }

    // CommandReceiver impl — applies to wlr_scene / wlr_xdg_toplevel
    void onCommand(const VariantCommand& cmd) override;

    // Accessors for listeners
    State& getState() noexcept { return state; }
    struct wl_display* display() const noexcept { return display_; }
    struct wlr_backend* backend() const noexcept { return backend_; }

private:
    State& state;
    Config& cfg;
    bool hooked = false;
    bool shouldTerminate = false;

    // wlroots owned — unique ownership, freed in ~Engine
    struct wl_display* display_ = nullptr;
    struct wl_event_loop* loop_ = nullptr; // kept for compatibility but not driven via wl_display_run
    struct wlr_backend* backend_ = nullptr;
    struct wlr_renderer* renderer_ = nullptr;
    struct wlr_allocator* allocator_ = nullptr;
    struct wlr_scene* scene_ = nullptr;
    struct wlr_scene_output_layout* sceneLayout_ = nullptr;
    struct wlr_output_layout* outputLayout_ = nullptr;
    struct wlr_xdg_shell* xdgShell_ = nullptr;
    struct wlr_layer_shell_v1* layerShell_ = nullptr;
    struct wlr_cursor* cursor_ = nullptr;
    struct wlr_seat* seat_ = nullptr;

    // Live per-frame event classes — Engine is sole writer, resets each loop top
    Keyboard kb_;
    Pointer ptr_;
    Window win_;
    AnimTick animTick_;
    OutputEv outEv_;
    // Per-frame queue of Window events (multiple commits in one frame)
    std::vector<VariantEvent> wlEnqueued_;
    std::unordered_map<struct wlr_output*, uint32_t> outputSerials_;

    int nextWindowId_ = 1;
    int nextAnimId_ = 1;
    int timerFd_ = -1;

    // wl listeners — fill public fields instead of allocating
    struct wl_listener newOutputListener{};
    struct wl_listener newXdgToplevelListener{};
    struct wl_listener newXdgPopupListener{};
    struct wl_listener newLayerListener{};
    struct wl_listener newInputListener{};
    struct wl_listener requestSetSelectionListener{};

    // Helpers called by C listeners
    void handleNewOutput(void* data);
    void handleNewToplevel(void* data);
    void handleNewPopup(void* data);
    void handleNewLayer(void* data);
    void handleNewInput(void* data);
    void handleRequestSetSelection(void* data);

    // Command apply helpers — inline to avoid extra copies
    void applySetWindowBox(const Cmd::SetWindowBox& c);
    void applySetWindowActivated(const Cmd::SetWindowActivated& c);
    void applySetWindowOpacity(const Cmd::SetWindowOpacity& c);
    void applyRequestFrame(const Cmd::RequestFrame& c);

    friend void engine_handle_newOutput(wl_listener*, void*);
    friend void engine_handle_newToplevel(wl_listener*, void*);
    friend void engine_handle_newPopup(wl_listener*, void*);
    friend void engine_handle_newLayer(wl_listener*, void*);
    friend void engine_handle_newInput(wl_listener*, void*);
    friend void engine_handle_setSelection(wl_listener*, void*);
};
