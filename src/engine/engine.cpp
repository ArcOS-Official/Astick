#include "engine.h"
#include "../compositor.h"
#include "../util.h"
#include <QColor>

namespace astick {

void engine_handle_newOutput(wl_listener *l, void *d) { Engine *e = wl_container_of(l, e, newOutputListener_); e->handleNewOutput(l, d); }
void engine_handle_newToplevel(wl_listener *l, void *d) { Engine *e = wl_container_of(l, e, newToplevelListener_); e->handleNewToplevel(l, d); }
void engine_handle_newPopup(wl_listener *l, void *d) { Engine *e = wl_container_of(l, e, newPopupListener_); e->handleNewPopup(l, d); }
void engine_handle_newLayer(wl_listener *l, void *d) { Engine *e = wl_container_of(l, e, newLayerListener_); e->handleNewLayer(l, d); }
void engine_handle_newInput(wl_listener *l, void *d) { Engine *e = wl_container_of(l, e, newInputListener_); e->handleNewInput(l, d); }
void engine_handle_setSelection(wl_listener *l, void *d) { Engine *e = wl_container_of(l, e, setSelectionListener_); e->handleSetSelection(l, d); }

Engine::Engine(State &state, Config &config, QObject *parent)
    : QObject(parent), state_(state), config_(config) {}

Engine::~Engine() {
    wl_display_destroy_clients(display_);
    wl_list_remove(&newOutputListener_.link);
    wl_list_remove(&newToplevelListener_.link);
    wl_list_remove(&newPopupListener_.link);
    wl_list_remove(&newLayerListener_.link);
    wl_list_remove(&newInputListener_.link);
    wl_list_remove(&setSelectionListener_.link);
    delete cursorMgrObj_;
    delete layout_;
    qDeleteAll(outputs_);
    qDeleteAll(toplevels_);
    qDeleteAll(popups_);
    qDeleteAll(layers_);
    qDeleteAll(keyboards_);
    qDeleteAll(mice_);
    if (cursorMgr_) wlr_xcursor_manager_destroy(cursorMgr_);
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
    renderer_ = wlr_renderer_autocreate(backend_);
    wlr_renderer_init_wl_display(renderer_, display_);
    allocator_ = wlr_allocator_autocreate(backend_, renderer_);
    wlr_compositor_create(display_, 5, renderer_);
    wlr_subcompositor_create(display_);
    wlr_data_device_manager_create(display_);
    wlr_presentation_create(display_, backend_, 1);

    outputLayout_ = wlr_output_layout_create(display_);
    scene_ = wlr_scene_create();
    sceneLayout_ = wlr_scene_attach_output_layout(scene_, outputLayout_);

    for (int i = ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND; i <= ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY; ++i)
        layerTrees[i] = wlr_scene_tree_create(&scene_->tree);
    popupTree_ = wlr_scene_tree_create(&scene_->tree);
    wlr_scene_node_place_below(&popupTree_->node, &layerTrees[ZWLR_LAYER_SHELL_V1_LAYER_TOP]->node);

    xdgShell_ = wlr_xdg_shell_create(display_, 3);
    layerShell_ = wlr_layer_shell_v1_create(display_, 5);
    cursor_ = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor_, outputLayout_);
    cursorMgr_ = wlr_xcursor_manager_create(nullptr, 24);
    wlr_xcursor_manager_load(cursorMgr_, 1);
    seat_ = wlr_seat_create(display_, "seat0");

    cursorMgrObj_ = nullptr;

    layout_ = new LayoutManager();
    layout_->setDefaultSplitRatio(config_.bsp.split_ratio);
    layout_->setOppositeOrientation(config_.bsp.opposite_orientation);
    layout_->setKeepRatioOnDrop(config_.bsp.keep_ratio_on_drop);
    layout_->setMinRatio(config_.bsp.min_ratio);
    layout_->setMaxRatio(config_.bsp.max_ratio);

    animManager_ = new ::AnimationManager(this);
    animPool_ = new AnimationPool(&config_, this);
    animManager_->setEnabled(config_.animations.enabled);
    animManager_->setGlobalSpeed(config_.animations.speed);
    animPool_->setConfig(&config_);
    connect(animPool_, &AnimationPool::frameRequested, this, &Engine::scheduleAllOutputs);

    DecorationConfig decCfg;
    decCfg.border.enabled = config_.decorations.border.enabled;
    decCfg.border.width = config_.decorations.border.width;
    decCfg.border.radius = config_.decorations.border.radius;
    decorManager_ = new DecorationManager(decCfg, animManager_, this);

    pendingEvents_.reserve(32);

    signal(newOutputListener_, &backend_->events.new_output, engine_handle_newOutput);
    signal(newToplevelListener_, &xdgShell_->events.new_toplevel, engine_handle_newToplevel);
    signal(newPopupListener_, &xdgShell_->events.new_popup, engine_handle_newPopup);
    signal(newLayerListener_, &layerShell_->events.new_surface, engine_handle_newLayer);
    signal(newInputListener_, &backend_->events.new_input, engine_handle_newInput);
    signal(setSelectionListener_, &seat_->events.request_set_selection, engine_handle_setSelection);

    wlr_log(WLR_INFO, "Engine initialized");
}

void Engine::hookState() {
    hooked_ = true;
    state_.addCommandReceiver(this);
    state_.addEventSource(this);
}

int Engine::run() {
    if (!hooked_) {
        wlr_log(WLR_ERROR, "Engine::run before hookState");
        return 1;
    }
    socket_ = QString::fromUtf8(wl_display_add_socket_auto(display_));
    if (socket_.isEmpty()) {
        wlr_log(WLR_ERROR, "Engine: failed to create socket");
        return 1;
    }
    setenv("WAYLAND_DISPLAY", socket_.toUtf8().constData(), true);
    wlr_log(WLR_INFO, "Engine running on %s", socket_.toUtf8().constData());
    if (!wlr_backend_start(backend_)) {
        wlr_log(WLR_ERROR, "Engine: backend start failed");
        return 1;
    }
    wl_display_run(display_);
    return 0;
}

std::vector<VariantEvent> Engine::poll() {
    std::vector<VariantEvent> out;
    out.reserve(pendingEvents_.size());
    for (auto &ev : pendingEvents_) out.emplace_back(std::move(ev));
    pendingEvents_.clear();
    return out;
}

void Engine::onCommand(const VariantCommand &cmd) {
    std::visit(Overloaded{
        [this](const Cmd::SetWindowBox &c){ applySetWindowBox(c); },
        [this](const Cmd::SetWindowActivated &c){ applySetWindowActivated(c); },
        [this](const Cmd::SetWindowOpacity &){},
        [this](const Cmd::RequestFrame &c){
            for (auto *o : outputs_) if (o->get()) wlr_output_schedule_frame(o->get());
            Q_UNUSED(c);
        },
        [](const Cmd::CreateSnapshot&){},
        [](const Cmd::DestroySnapshot&){},
        [](const Cmd::SetWorkspace&){},
        [](const Cmd::ConfigureToplevel&){}
    }, cmd);
}

void Engine::applySetWindowBox(const Cmd::SetWindowBox &c) {
    auto it = windowMap_.find(c.id);
    if (it == windowMap_.end()) return;
    Toplevel *tl = it->second;
    if (!tl || !tl->getSceneTree()) return;
    wlr_scene_node_set_position(&tl->getSceneTree()->node, c.box.x, c.box.y);
    wlr_xdg_toplevel_set_size(tl->get(), c.box.width, c.box.height);
    if (auto *dec = decorManager_->decorationFor(tl))
        dec->updateGeometry(c.box.x, c.box.y, c.box.width, c.box.height);
}

void Engine::applySetWindowActivated(const Cmd::SetWindowActivated &c) {
    auto it = windowMap_.find(c.id);
    if (it == windowMap_.end()) return;
    wlr_xdg_toplevel_set_activated(it->second->get(), c.active);
}

void Engine::handleNewOutput(wl_listener *, void *data) {
    auto *wout = static_cast<wlr_output*>(data);
    onOutputAdded(wout);
    OutputEv ev;
    ev.outputSerial = (uint32_t)nextWindowId_++;
    ev.usable = Box{0,0,wout->width,wout->height};
    ev.full = ev.usable;
    ev.hasEvent = true;
    pendingEvents_.emplace_back(std::move(ev));
    state_.handle(pendingEvents_.back());
}

void Engine::handleNewToplevel(wl_listener *, void *data) {
    auto *xdg = static_cast<wlr_xdg_toplevel*>(data);
    onToplevelAdded(xdg);
}

void Engine::handleNewPopup(wl_listener *, void *data) {
    auto *popup = static_cast<wlr_xdg_popup*>(data);
    onPopupAdded(popup);
}

void Engine::handleNewLayer(wl_listener *, void *data) {
    auto *layer = static_cast<wlr_layer_surface_v1*>(data);
    onLayerAdded(layer);
}

void Engine::handleNewInput(wl_listener *, void *data) {
    auto *device = static_cast<wlr_input_device*>(data);
    onInputAdded(device);
}

void Engine::handleSetSelection(wl_listener *, void *data) {
    auto *event = static_cast<wlr_seat_request_set_selection_event*>(data);
    onSetSelection(event);
}

void Engine::onOutputAdded(wlr_output *wout) {
    wlr_output_init_render(wout, allocator_, renderer_);
    std::string oid = Config::outputId(wout);
    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    if (auto *mode = wlr_output_preferred_mode(wout)) wlr_output_state_set_mode(&state, mode);
    wlr_output_commit_state(wout, &state);
    wlr_output_state_finish(&state);

    auto *out = new ::Output(wout, renderer_, allocator_, scene_);
    outputs_.append(out);
    connect(out, &Output::frameReady, animPool_, &AnimationPool::onFrame);

    auto *lout = wlr_output_layout_add_auto(outputLayout_, wout);
    auto *sout = wlr_scene_output_create(scene_, wout);
    wlr_scene_output_layout_add_output(sceneLayout_, lout, sout);

    layout_->activateWorkspace(out->getWorkspace());
    rearrangeTiled();
}

void Engine::onToplevelAdded(wlr_xdg_toplevel *xdg) {
    auto *tree = wlr_scene_xdg_surface_create(&scene_->tree, xdg->base);
    wlr_scene_node_place_below(&tree->node, &popupTree_->node);
    auto *tl = new ::Toplevel(this, xdg, tree);
    WindowId wid = nextWindowId_++;
    windowMap_[wid] = tl;
    toplevels_.append(tl);
    if (decorManager_) decorManager_->createFor(tl);

    Window ev;
    ev.id = wid;
    ev.kind = Window::Kind::Mapped;
    ev.hasEvent = true;
    pendingEvents_.emplace_back(std::move(ev));
    state_.handle(pendingEvents_.back());

    connect(tl, &Toplevel::mapped, this, [this, tl, wid]() {
        Window e; e.id = wid; e.kind = Window::Kind::Mapped; e.hasEvent = true;
        pendingEvents_.emplace_back(std::move(e));
        state_.handle(pendingEvents_.back());
        focusToplevel(tl);
        int ws = outputs_.isEmpty() ? 1 : outputs_.first()->getWorkspace();
        if (!outputs_.isEmpty()) {
            wlr_box usable = usableAreaForOutput(outputs_.first()->get());
            layout_->addWindow(tl, ws);
            arrangeForOutput(outputs_.first());
        }
    });
    connect(tl, &Toplevel::unmapped, this, [this, tl, wid]() {
        Window e; e.id = wid; e.kind = Window::Kind::Unmapped; e.hasEvent = true;
        pendingEvents_.emplace_back(std::move(e));
        state_.handle(pendingEvents_.back());
    });
    connect(tl, &Toplevel::destroyed, this, [this, tl, wid]() {
        Window e; e.id = wid; e.kind = Window::Kind::Destroy; e.hasEvent = true;
        pendingEvents_.emplace_back(std::move(e));
        state_.handle(pendingEvents_.back());
        toplevels_.removeOne(tl);
        windowMap_.erase(wid);
        layout_->removeWindow(tl);
        rearrangeTiled();
        if (decorManager_) decorManager_->removeFor(tl);
        delete tl;
    });
}

void Engine::onPopupAdded(wlr_xdg_popup *xdg) {
    auto *popup = new ::Popup(this, xdg);
    popups_.append(popup);
    connect(popup, &Popup::destroyed, this, [this, popup]() { popups_.removeOne(popup); });
}

void Engine::onLayerAdded(wlr_layer_surface_v1 *surface) {
    if (!surface->output && !outputs_.isEmpty()) surface->output = outputs_.first()->get();
    auto *layer = new ::LayerSurface(this, surface);
    layers_.append(layer);
    connect(layer, &LayerSurface::destroyed, this, [this, layer]() { layers_.removeOne(layer); });
}

void Engine::onSetSelection(wlr_seat_request_set_selection_event *event) {
    wlr_seat_set_selection(seat_, event->source, event->serial);
}

void Engine::onInputAdded(wlr_input_device *device) {
    if (device->type == WLR_INPUT_DEVICE_KEYBOARD) addKeyboard(device);
    else if (device->type == WLR_INPUT_DEVICE_POINTER) addMouse(device);
    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!keyboards_.isEmpty()) caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(seat_, caps);
}

void Engine::addKeyboard(wlr_input_device *device) {
    auto *kb = new ::Keyboard(device, seat_);
    if (config_.keyboard.layouts.size()) kb->applyConfig(config_.keyboard);
    keyboards_.append(kb);
    connect(kb, &::Keyboard::keyPressed, this, [this, kb](wlr_keyboard_key_event *event) {
        uint32_t keycode = event->keycode + 8;
        const xkb_keysym_t *syms; int nsyms = xkb_state_key_get_syms(kb->getKeyboard()->xkb_state, keycode, &syms);
        uint32_t mods = wlr_keyboard_get_modifiers(kb->getKeyboard());
        if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED) {
            for (int i=0;i<nsyms;i++) {
                if (auto *bind = config_.findKeybind(mods, syms[i])) {
                    if (bind->action=="quit") wl_display_terminate(display_);
                    else if (bind->action=="focus_prev" && toplevels_.size()>=2) focusToplevel(toplevels_.last());
                    else if (bind->action=="focus_next" && !toplevels_.isEmpty()) focusToplevel(toplevels_.first());
                }
            }
        }
        Keyboard ev;
        ev.keycode = keycode; ev.keysym = nsyms?syms[0]:0; ev.mods = mods;
        ev.pressed = event->state==WL_KEYBOARD_KEY_STATE_PRESSED;
        ev.hasEvent = true;
        pendingEvents_.emplace_back(std::move(ev));
        state_.handle(pendingEvents_.back());
    });
}

void Engine::addMouse(wlr_input_device *device) {
    auto *mouse = new ::Mouse(device);
    mice_.append(mouse);
    wlr_cursor_attach_input_device(cursor_, device);
}

void Engine::focusToplevel(Toplevel *tl) {
    if (!tl) return;
    auto *surface = tl->get()->base->surface;
    if (seat_->keyboard_state.focused_surface == surface) return;
    if (auto *prev = seat_->keyboard_state.focused_surface) {
        if (auto *p = wlr_xdg_toplevel_try_from_wlr_surface(prev)) wlr_xdg_toplevel_set_activated(p, false);
    }
    wlr_scene_node_place_below(&tl->getSceneTree()->node, &popupTree_->node);
    toplevels_.removeOne(tl); toplevels_.prepend(tl);
    wlr_xdg_toplevel_set_activated(tl->get(), true);
    layout_->raiseWindow(tl);
    if (auto *kb = wlr_seat_get_keyboard(seat_)) {
        wlr_seat_keyboard_notify_enter(seat_, surface, kb->keycodes, kb->num_keycodes, &kb->modifiers);
    }
    state_.emitCommand(Cmd::SetWindowActivated{*reinterpret_cast<WindowId*>(&tl), true});
}

Output *Engine::outputForToplevel(Toplevel *tl) {
    if (outputs_.isEmpty()) return nullptr;
    if (tl && layout_) {
        int ws = layout_->getWindowWorkspace(tl);
        for (auto *o: outputs_) if (o->getWorkspace()==ws) return o;
    }
    if (cursor_ && outputLayout_) {
        if (auto *wout = wlr_output_layout_output_at(outputLayout_, cursor_->x, cursor_->y))
            for (auto *o: outputs_) if (o->get()==wout) return o;
    }
    return outputs_.first();
}

wlr_box Engine::usableAreaForOutput(wlr_output *wout) {
    wlr_box full{0,0,0,0};
    if (outputLayout_ && wout) wlr_output_layout_get_box(outputLayout_, wout, &full);
    if (full.width==0) full = {0,0,wout->width,wout->height};
    wlr_box usable = full;
    for (auto *ls: layers_) {
        auto *l = ls->get();
        if (l->output!=wout || !l->surface->mapped || l->current.exclusive_zone<=0) continue;
        uint32_t anchor = l->current.exclusive_edge ? l->current.exclusive_edge : l->current.anchor;
        if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP) { usable.y+=l->current.exclusive_zone; usable.height-=l->current.exclusive_zone; }
        else if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM) usable.height-=l->current.exclusive_zone;
        if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT) { usable.x+=l->current.exclusive_zone; usable.width-=l->current.exclusive_zone; }
        else if (anchor & ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT) usable.width-=l->current.exclusive_zone;
    }
    return usable;
}

wlr_box Engine::fullAreaForOutput(wlr_output *wout) {
    wlr_box full{0,0,0,0};
    if (outputLayout_ && wout) wlr_output_layout_get_box(outputLayout_, wout, &full);
    if (full.width==0) full={0,0,wout->width,wout->height};
    return full;
}

void Engine::arrangeForOutput(Output *out) {
    if (!out) return;
    wlr_box usable = usableAreaForOutput(out->get());
    wlr_box full = fullAreaForOutput(out->get());
    layout_->arrange(usable, full, out->getWorkspace());
}

void Engine::rearrangeTiled() {
    for (auto *o: outputs_) arrangeForOutput(o);
}

void Engine::scheduleAllOutputs() {
    for (auto *o: outputs_) if (o->get()) wlr_output_schedule_frame(o->get());
}

wlr_box Engine::boxForStyle(AnimationStyle, const wlr_box &b, bool) { return b; }

void Engine::setInitialLayoutMode(const QString &mode) {
    QString m = mode.toLower().trimmed();
    LayoutManager::Mode lm = LayoutManager::Mode::Tiling;
    if (m == "floating") lm = LayoutManager::Mode::Floating;
    else if (m == "monowindow") lm = LayoutManager::Mode::MonoWindow;
    if (layout_) layout_->setWorkspaceLayoutMode(1, lm);
}

} // namespace astick
