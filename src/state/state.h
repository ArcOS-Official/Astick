#pragma once
#include <vector>
#include <functional>
#include <optional>
#include <unordered_map>
#include <chrono>
#include "event.h"
#include "command.h"
#include "ids.h"
#include "window_manager.h"
#include "state_interface.h"

namespace astick {

class State : public IStateManager {
public:
    using Callback = std::function<void(const VariantEvent&)>;

    State() = default;
    ~State() override = default;

    Subscription subscribe(void* owner, SubMask mask, Callback cb) override;
    Subscription subscribeWindow(void* owner, WindowId id, Callback cb) override {
        return subscribe(owner, SubMask::WindowOf(id), std::move(cb));
    }
    template<typename T>
    Subscription subscribeTyped(void* owner, std::function<void(const T&)> typedCb) {
        Callback wrapper = [cb = std::move(typedCb)](const VariantEvent& ev){
            if (auto* v = std::get_if<T>(&ev)) cb(*v);
        };
        EventKind k{};
        if constexpr (std::is_same_v<T, KeyEvent>) k = EventKind::Keyboard;
        else if constexpr (std::is_same_v<T, PointerEvent>) k = EventKind::Pointer;
        else if constexpr (std::is_same_v<T, WindowEvent>) k = EventKind::Window;
        else if constexpr (std::is_same_v<T, AnimTick>) k = EventKind::Anim;
        else if constexpr (std::is_same_v<T, OutputEventent>) k = EventKind::Output;
        return subscribe(owner, SubMask{uint32_t(k), std::nullopt}, std::move(wrapper));
    }

    void addEventSource(EventSource* src) override { sources.push_back(src); }
    void addCommandReceiver(CommandReceiver* r) override { receivers.push_back(r); }

    void handle(const VariantEvent& ev) override;
    void emitCommand(VariantCommand cmd) override { cmdQueue.emplace_back(std::move(cmd)); }
    std::vector<VariantCommand> drainCommands() override {
        std::vector<VariantCommand> out;
        out = std::move(cmdQueue);
        cmdQueue.clear();
        cmdQueue.reserve(out.capacity());
        return out;
    }
    const std::vector<VariantCommand>& pendingCommands() const noexcept override { return cmdQueue; }
    std::vector<VariantCommand>& mutableCommands() noexcept { return cmdQueue; }

    struct WindowHandle {
        WindowId id = 0;
        IStateManager* state = nullptr;
        Subscription subscribe(void* owner, Callback cb) { return state->subscribeWindow(owner, id, std::move(cb)); }
        std::optional<Box> geometry() const;
        void setBox(Box b, bool isFloating = false) { state->emitCommand(Cmd::SetWindowBox{id, b, isFloating}); }
        void setActivated(bool a) { state->emitCommand(Cmd::SetWindowActivated{id, a}); }
        void setOpacity(float o) { state->emitCommand(Cmd::SetWindowOpacity{id, o}); }
    };
    WindowHandle getWindow(WindowId id) { return WindowHandle{id, this}; }
    std::optional<WindowHandle> findWindow(WindowId id) const {
        if (winMgr.shouldDeliver(id)) return WindowHandle{id, const_cast<State*>(this)};
        return std::nullopt;
    }

    int nextWakeupMs() const override;
    bool needsFrame() const noexcept override { return needsFrameFlag; }
    void setNeedsFrame(bool v) noexcept override { needsFrameFlag = v; }
    void setNextDeadlineMs(std::optional<int> ms) override { nextDeadlineMs = ms; }
    void unsubscribeOwner(void* owner) override;

private:
    struct Entry { void* owner = nullptr; SubMask mask{}; Callback cb{}; };
    void dispatchToSubs(const VariantEvent& ev);

    std::vector<Entry> subs;
    std::vector<EventSource*> sources;
    std::vector<CommandReceiver*> receivers;
    std::vector<VariantCommand> cmdQueue;
    WindowEventManager winMgr;
    bool needsFrameFlag = false;
    std::optional<int> nextDeadlineMs;
    std::unordered_map<uint64_t, WindowId> stableToLive;
};

using StateManager = State;

} // namespace astick
