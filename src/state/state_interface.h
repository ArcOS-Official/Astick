#pragma once
#include <vector>
#include <functional>
#include <optional>
#include "event.h"
#include "command.h"
#include "ids.h"

namespace astick {

class EventSource {
public:
    virtual ~EventSource() = default;
    virtual std::vector<VariantEvent> poll() = 0;
    virtual std::optional<int> nextWakeupMs() const { return std::nullopt; }
};
class CommandReceiver {
public:
    virtual ~CommandReceiver() = default;
    virtual void onCommand(const VariantCommand& cmd) = 0;
};

class IStateManager;

struct Subscription {
    struct Handle { void* owner = nullptr; size_t index = 0; };
    std::vector<Handle> handles;
    IStateManager* state = nullptr;
    void* owner = nullptr;
    Subscription() = default;
    Subscription(IStateManager* s, void* o, std::vector<Handle> h) : handles(std::move(h)), state(s), owner(o) {}
    Subscription(Subscription&& other) noexcept : handles(std::move(other.handles)), state(other.state), owner(other.owner) { other.state=nullptr; other.owner=nullptr; }
    Subscription& operator=(Subscription&& other) noexcept;
    ~Subscription();
    void unsubscribe();
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
};

class IEngineState {
public:
    virtual ~IEngineState() = default;
    virtual void handle(const VariantEvent& ev) = 0;
    virtual void addEventSource(EventSource* src) = 0;
    virtual void addCommandReceiver(CommandReceiver* r) = 0;
    virtual std::vector<VariantCommand> drainCommands() = 0;
    virtual int nextWakeupMs() const = 0;
    virtual bool needsFrame() const noexcept = 0;
    virtual void setNeedsFrame(bool v) noexcept = 0;
};

class IStateManager : public IEngineState {
public:
    using Callback = std::function<void(const VariantEvent&)>;
    virtual Subscription subscribe(void* owner, SubMask mask, Callback cb) = 0;
    virtual Subscription subscribeWindow(void* owner, WindowId id, Callback cb) = 0;
    virtual void emitCommand(VariantCommand cmd) = 0;
    virtual const std::vector<VariantCommand>& pendingCommands() const noexcept = 0;
    virtual void setNextDeadlineMs(std::optional<int> ms) = 0;
    virtual void unsubscribeOwner(void* owner) = 0;
};

using EngineState = IEngineState;

} // namespace astick
