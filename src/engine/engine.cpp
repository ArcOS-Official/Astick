#include "engine.h"
#include "../util.h"
#include <chrono>
#include <poll.h>
#include <sys/timerfd.h>
#include <cstring>

namespace astick {

// C listener shims — thin, zero-copy: just forward data ptr, no allocation.
void engine_handle_newOutput(wl_listener* l, void* d){ Engine* e = wl_container_of(l, e, newOutputListener); e->handleNewOutput(d); }
void engine_handle_newToplevel(wl_listener* l, void* d){ Engine* e = wl_container_of(l, e, newXdgToplevelListener); e->handleNewToplevel(d); }
void engine_handle_newPopup(wl_listener* l, void* d){ Engine* e = wl_container_of(l, e, newXdgPopupListener); e->handleNewPopup(d); }
void engine_handle_newLayer(wl_listener* l, void* d){ Engine* e = wl_container_of(l, e, newLayerListener); e->handleNewLayer(d); }
void engine_handle_newInput(wl_listener* l, void* d){ Engine* e = wl_container_of(l, e, newInputListener); e->handleNewInput(d); }
void engine_handle_setSelection(wl_listener* l, void* d){ Engine* e = wl_container_of(l, e, requestSetSelectionListener); e->handleRequestSetSelection(d); }

Engine::Engine(State& state_, Config& cfg_) : state(state_), cfg(cfg_) {}

Engine::~Engine() {
    if (timerFd_ >= 0) close(timerFd_);
    // Destruction order: scene, allocator, renderer, backend, display (reverse of init)
    if (scene_) wlr_scene_node_destroy(&scene_->tree.node);
    if (cursor_) wlr_cursor_destroy(cursor_);
    if (allocator_) wlr_allocator_destroy(allocator_);
    if (renderer_) wlr_renderer_destroy(renderer_);
    if (backend_) wlr_backend_destroy(backend_);
    if (display_) wl_display_destroy(display_);
}

void Engine::init() {
    display_ = wl_display_create();
    loop_ = wl_display_get_event_loop(display_);
    backend_ = wlr_backend_autocreate(loop_, nullptr);
    if (!backend_) { wlr_log(WLR_ERROR, "Engine: wlr_backend_autocreate failed"); return; }

    renderer_ = wlr_renderer_autocreate(backend_);
    if (!renderer_) { wlr_log(WLR_ERROR, "Engine: wlr_renderer_autocreate failed"); return; }
    wlr_renderer_init_wl_display(renderer_, display_);

    allocator_ = wlr_allocator_autocreate(backend_, renderer_);
    wlr_compositor_create(display_, 5, renderer_);
    wlr_subcompositor_create(display_);
    wlr_data_device_manager_create(display_);

    outputLayout_ = wlr_output_layout_create(display_);
    scene_ = wlr_scene_create();
    if (scene_ && outputLayout_) sceneLayout_ = wlr_scene_attach_output_layout(scene_, outputLayout_);

    xdgShell_ = wlr_xdg_shell_create(display_, 3);
    layerShell_ = wlr_layer_shell_v1_create(display_, 5);
    cursor_ = wlr_cursor_create();
    if (cursor_ && outputLayout_) wlr_cursor_attach_output_layout(cursor_, outputLayout_);
    seat_ = wlr_seat_create(display_, "seat0");

    // Install listeners — they only enqueue via public field fills (no handling)
    signal(newOutputListener, &backend_->events.new_output, engine_handle_newOutput);
    signal(newXdgToplevelListener, &xdgShell_->events.new_toplevel, engine_handle_newToplevel);
    signal(newXdgPopupListener, &xdgShell_->events.new_popup, engine_handle_newPopup);
    signal(newLayerListener, &layerShell_->events.new_surface, engine_handle_newLayer);
    signal(requestSetSelectionListener, &seat_->events.request_set_selection, engine_handle_setSelection);
    signal(newInputListener, &backend_->events.new_input, engine_handle_newInput);

    timerFd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    // wlEnqueued reserve to avoid per-frame alloc
    wlEnqueued_.reserve(32);
    wlr_log(WLR_INFO, "Engine::init stale until hookState()");
}

void Engine::hookState() {
    hooked = true;
    // Now state.handle will be called from run()
    state.addCommandReceiver(this);
    state.addEventSource(this);
    wlr_log(WLR_INFO, "Engine::hookState ready");
}

std::vector<VariantEvent> Engine::poll() {
    std::vector<VariantEvent> out;
    out.reserve(wlEnqueued_.size() + 4);
    // Move wlEnqueued without copying extra: wlEnqueued already holds VariantEvents
    // which were emplaced via move of live instances.
    for (auto& ev : wlEnqueued_) out.emplace_back(std::move(ev));
    wlEnqueued_.clear();

    // Also emit live Keyboard/Pointer if they haveEvent — move via emplace
    if (kb_.hasEvent) { out.emplace_back(kb_); }
    if (ptr_.hasEvent) { out.emplace_back(ptr_); }
    if (win_.hasEvent) { out.emplace_back(win_); }
    if (animTick_.hasEvent) { out.emplace_back(animTick_); }
    if (outEv_.hasEvent) { out.emplace_back(outEv_); }
    return out; // NRVO
}

void Engine::onCommand(const VariantCommand& cmd) {
    // Visitor takes const& — no copy of variant. Dispatch without heap.
    // Zero-copy: each lambda takes const& and covers exactly one variant alternative.
    std::visit(Overloaded{
        [this](const Cmd::SetWindowBox& c){ applySetWindowBox(c); },
        [this](const Cmd::SetWindowActivated& c){ applySetWindowActivated(c); },
        [this](const Cmd::SetWindowOpacity& c){ applySetWindowOpacity(c); },
        [this](const Cmd::RequestFrame& c){ applyRequestFrame(c); },
        [](const Cmd::CreateSnapshot&){},
        [](const Cmd::DestroySnapshot&){},
        [](const Cmd::SetWorkspace&){},
        [](const Cmd::ConfigureToplevel&){}
    }, cmd);
}

void Engine::applySetWindowBox(const Cmd::SetWindowBox& c) {
    // Find wlr_scene node by WindowId -> scene tree. For now stub: log.
    // Real impl looks up wlr_scene_tree from WindowId map and does set_position/set_size.
    // Zero-copy: Box passed as const& (trivial) and applied directly.
    (void)c;
    // Example: wlr_scene_node_set_position(&tree->node, c.box.x, c.box.y);
    // wlr_xdg_toplevel_set_size(toplevel, c.box.width, c.box.height);
}

void Engine::applySetWindowActivated(const Cmd::SetWindowActivated& c) {
    (void)c;
    // wlr_xdg_toplevel_set_activated(tl, c.active);
}

void Engine::applySetWindowOpacity(const Cmd::SetWindowOpacity& c) {
    (void)c;
    // wlr_scene_buffer_set_opacity(buffer, c.opacity);
}

void Engine::applyRequestFrame(const Cmd::RequestFrame& c) {
    (void)c;
    // Iterate outputs and schedule frame for matching serial
    // wlr_output_schedule_frame(output);
}

int Engine::run() {
    if (!hooked) { wlr_log(WLR_ERROR, "Engine::run called before hookState"); return 1; }
    if (!wlr_backend_start(backend_)) { wlr_log(WLR_ERROR, "Engine: wlr_backend_start failed"); return 1; }

    int wlFd = wl_display_get_fd(display_);
    // libinput fd via backend (if available)
    int libinputFd = -1;
#ifdef WLR_HAS_LIBINPUT_BACKEND
    // Not all backends expose libinput fd; use -1 if unavailable
#endif

    // Arm timerFd for initial tick
    // Compute timeout = next anim wakeup or -1
    while (!shouldTerminate) {
        // 0. Reset all per-frame public fields — guarantee stale never leaks
        kb_.reset(); ptr_.reset(); win_.reset(); animTick_.reset(); outEv_.reset();
        wlEnqueued_.clear();

        int timeoutMs = state.nextWakeupMs(); // may be -1
        pollfd fds[3];
        int nfds = 0;
        fds[nfds++] = pollfd{wlFd, POLLIN, 0};
        if (libinputFd >= 0) fds[nfds++] = pollfd{libinputFd, POLLIN, 0};
        if (timerFd_ >= 0) fds[nfds++] = pollfd{timerFd_, POLLIN, 0};

        int ret = ::poll(fds, nfds, timeoutMs);
        if (ret < 0 && errno != EINTR) { wlr_log(WLR_ERROR, "Engine poll failed %s", strerror(errno)); break; }

        // Dispatch clients if wlFd ready
        for (int i=0;i<nfds;i++) if (fds[i].revents & POLLIN) {
            if (fds[i].fd == wlFd) {
                wl_display_flush_clients(display_);
                // dispatch pending wayland events — listeners will fill kb/ptr/win fields
                // In real wlroots, wlr_backend_dispatch handles libinput; we call both
                // For minimal stub we just dispatch clients; backend events drive via libinputFd
            } else if (fds[i].fd == timerFd_) {
                uint64_t v; (void)read(timerFd_, &v, sizeof(v));
            }
        }
        // Even if no fd, we still need to drain backend/libinput
        // wlr_backend_dispatch(backend_); // would fill kb/ptr via listeners

        // 3. Drain Engine's own queue + EventSources
        auto evs = this->poll();
        for (auto* src : std::vector<EventSource*>{}) { (void)src; } // placeholder: state.sources polled via State
        // Actually Engine is also an EventSource; AnimationManager polls via State sources
        // For correctness, ask State for sources
        // But to avoid copy, we iterate state's sources directly (friend or accessor needed)
        // Simplified: poll AnimationManager via state (we've already hooked)
        // So we collect from state sources by calling state.handle loop below after polling them
        // For this stub, we just handle evs from Engine itself
        for (auto& srcEv : evs) state.handle(srcEv);
        // Poll other sources (e.g., AnimationManager) — they push commands directly, but also produce ticks
        // In full impl we would: for (auto* s : state.sources) { auto more = s->poll(); for (auto& e: more) state.handle(e); }

        auto cmds = state.drainCommands(); // move, no copy
        for (const auto& c : cmds) this->onCommand(c);

        if (state.needsFrame()) {
            if (timerFd_ >= 0) {
                itimerspec ts{};
                ts.it_value.tv_nsec = 1000000 * (1000 / (state.nextWakeupMs() >0 ? state.nextWakeupMs() : 16));
                timerfd_settime(timerFd_, 0, &ts, nullptr);
            }
            wl_display_flush_clients(display_);
            state.setNeedsFrame(false);
        }

        // Check for termination request via command or signal
        if (wl_display_get_event_loop(display_) == nullptr) break; // stub
    }
    return 0;
}

void Engine::handleNewOutput(void* data) {
    (void)data;
    // Fill OutputEv live instance directly (zero-copy field write)
    outEv_.outputSerial = (uint32_t)nextWindowId_++; // reuse counter for serial
    outEv_.usable = Box{0,0,1920,1080};
    outEv_.full = outEv_.usable;
    outEv_.hasEvent = true;
    wlEnqueued_.emplace_back(outEv_); // copy of small struct (32 bytes) — acceptable; could emplace via move
    outEv_.reset(); // reset after emplace to avoid double deliver? But we keep hasEvent for poll() — alternative: clear after
    // Actually we already emplaced copy, so we can keep live cleared; but we set hasEvent false via reset, and poll will not duplicate
    // So we should not rely on poll() duplicating; we already enqueued.
    // To avoid double, we cleared live.
}

void Engine::handleNewToplevel(void* data) {
    (void)data;
    Window w;
    w.id = (WindowId)nextWindowId_++;
    w.kind = Window::Kind::Mapped;
    w.hasEvent = true;
    // Stable key: hash app_id via string_view — zero copy
    // const char* app_id = xdg_toplevel->app_id; if (app_id) stable = stableKeyFromAppId(app_id);
    wlEnqueued_.emplace_back(std::move(w));
}

void Engine::handleNewPopup(void* ) {}
void Engine::handleNewLayer(void* ) {}
void Engine::handleNewInput(void* ) {}
void Engine::handleRequestSetSelection(void* ) {}
} // namespace astick
