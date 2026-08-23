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

namespace astick {

// Forward deps for zero-copy: State never includes wlroots.
class EventSource {
public:
    virtual ~EventSource() = default;
    // Poll returns events by value moved — caller does std::move + emplace.
    virtual std::vector<VariantEvent> poll() = 0;
    virtual std::optional<int> nextWakeupMs() const { return std::nullopt; }
};
class CommandReceiver {
public:
    virtual ~CommandReceiver() = default;
    virtual void onCommand(const VariantCommand& cmd) = 0;
};

struct Subscription {
    // RAII: when destroyed, auto-unsubscribes via State::unsubscribe.
    // Zero-copy: holds non-owning State pointer + owner identity, no string copy.
    struct Handle {
        void* owner = nullptr;
        size_t index = 0;
    };
    std::vector<Handle> handles;
    class State* state = nullptr;
    void* owner = nullptr;
    Subscription() = default;
    Subscription(class State* s, void* o, std::vector<Handle> h) : handles(std::move(h)), state(s), owner(o) {}
    Subscription(Subscription&& other) noexcept : handles(std::move(other.handles)), state(other.state), owner(other.owner) { other.state=nullptr; other.owner=nullptr; }
    Subscription& operator=(Subscription&& other) noexcept {
        if (this!=&other) { unsubscribe(); handles=std::move(other.handles); state=other.state; owner=other.owner; other.state=nullptr; other.owner=nullptr; }
        return *this;
    }
    ~Subscription(){ unsubscribe(); }
    void unsubscribe();
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
};

class State {
public:
    using Callback = std::function<void(const VariantEvent&)>;

    State() = default;
    ~State() = default;

    // Single callback receiving VariantEvent filtered by SubMask.
    // Zero-copy: mask passed by value (small), cb moved, owner is non-owning raw ptr.
    Subscription subscribe(void* owner, SubMask mask, Callback cb);
    // Convenience for window-only: sugar over subscribe with WindowOf
    Subscription subscribeWindow(void* owner, WindowId id, Callback cb) {
        return subscribe(owner, SubMask::WindowOf(id), std::move(cb));
    }
    // Typed sugar: subscribe<Keyboard>(owner, cb) wraps into variant visitor — no extra copy.
    template<typename T>
    Subscription subscribeTyped(void* owner, std::function<void(const T&)> typedCb) {
        Callback wrapper = [cb = std::move(typedCb)](const VariantEvent& ev){
            if (auto* v = std::get_if<T>(&ev)) cb(*v);
        };
        EventKind k{};
        if constexpr (std::is_same_v<T, Keyboard>) k = EventKind::Keyboard;
        else if constexpr (std::is_same_v<T, Pointer>) k = EventKind::Pointer;
        else if constexpr (std::is_same_v<T, Window>) k = EventKind::Window;
        else if constexpr (std::is_same_v<T, AnimTick>) k = EventKind::Anim;
        else if constexpr (std::is_same_v<T, OutputEv>) k = EventKind::Output;
        return subscribe(owner, SubMask{uint32_t(k), std::nullopt}, std::move(wrapper));
    }

    void addConsumer(void* /*owner*/) {} // tracking hook, no-op for single thread
    void addEventSource(EventSource* src) { sources.push_back(src); }
    void addCommandReceiver(CommandReceiver* r) { receivers.push_back(r); }

    // Called by Engine::run() for each polled event — takes const& (no copy).
    void handle(const VariantEvent& ev);

    // Called by consumers to push commands — rvalue moved into queue (one move).
    void emitCommand(VariantCommand cmd) { cmdQueue.emplace_back(std::move(cmd)); }
    // Drain moves out the queue — no copy of contained variants (vector move).
    std::vector<VariantCommand> drainCommands() {
        std::vector<VariantCommand> out;
        out = std::move(cmdQueue);
        cmdQueue.clear(); // leave in valid empty state after move
        cmdQueue.reserve(out.capacity()); // keep capacity to avoid realloc next frame
        out.reserve(0); // shrink out's spare? no
        return out;
    }
    // Const view without draining — zero-copy inspect.
    const std::vector<VariantCommand>& pendingCommands() const noexcept { return cmdQueue; }
    std::vector<VariantCommand>& mutableCommands() noexcept { return cmdQueue; }

    // Per-window handle for ergonomics: holds State* + WindowId, sugar over subscribeWindow.
    struct WindowHandle {
        WindowId id = 0;
        State* state = nullptr;
        Subscription subscribe(void* owner, Callback cb) { return state->subscribeWindow(owner, id, std::move(cb)); }
        std::optional<Box> geometry() const; // query Handler's layout model (stub)
        void setBox(Box b, bool isFloating = false) { state->emitCommand(Cmd::SetWindowBox{id, b, isFloating}); }
        void setActivated(bool a) { state->emitCommand(Cmd::SetWindowActivated{id, a}); }
        void setOpacity(float o) { state->emitCommand(Cmd::SetWindowOpacity{id, o}); }
    };
    WindowHandle getWindow(WindowId id) { return WindowHandle{id, this}; }
    std::optional<WindowHandle> findWindow(WindowId id) const {
        if (winMgr.shouldDeliver(id)) return WindowHandle{id, const_cast<State*>(this)};
        return std::nullopt;
    }

    // Animation wakeup: minimal poll timeout. Returns -1 if none.
    int nextWakeupMs() const;
    bool needsFrame() const noexcept { return needsFrameFlag; }
    void setNeedsFrame(bool v) noexcept { needsFrameFlag = v; }

    // For Engine to query output deadlines without copy.
    void setNextDeadlineMs(std::optional<int> ms) { nextDeadlineMs = ms; }

    // Unsubscribe helper called by Subscription RAII.
    void unsubscribeOwner(void* owner);

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
    // StableKey -> WindowId restore map (best-effort, hash only, no string copy)
    std::unordered_map<uint64_t, WindowId> stableToLive;
};
} // namespace astick
