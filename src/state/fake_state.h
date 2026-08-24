#pragma once
#include "state_interface.h"
#include <vector>
#include <algorithm>

namespace astick {

// FakeStateManager — drop-in replacement for State for testing.
// Records every handle/emit so tests can assert without wlroots.
// Zero-copy: stores VariantEvent/Command by move, exposes const& views.
class FakeStateManager : public IStateManager {
public:
    Subscription subscribe(void* owner, SubMask mask, Callback cb) override {
        // Store but never auto-unsubscribe; tests can clear manually
        subs.push_back({owner, mask, std::move(cb)});
        return Subscription(this, owner, {});
    }
    Subscription subscribeWindow(void* owner, WindowId id, Callback cb) override {
        return subscribe(owner, SubMask::WindowOf(id), std::move(cb));
    }
    void addEventSource(EventSource* src) override { sources.push_back(src); }
    void addCommandReceiver(CommandReceiver* r) override { receivers.push_back(r); }

    void handle(const VariantEvent& ev) override {
        handled.push_back(ev);
        // Dispatch to subs like real State (const&)
        auto k = eventKind(ev);
        auto win = eventWindowId(ev);
        for (auto &e : subs) {
            if ((e.mask.kinds & uint32_t(k)
                ) == 0) continue;
            if (e.mask.window) {
                if (k != EventKind::Window)
                    continue;
                if (!win || *win != *e.mask.window)
                    continue;
            }
            e.cb(ev);
        }
    }
    void emitCommand(VariantCommand cmd) override { emitted.push_back(cmd); }
    std::vector<VariantCommand> drainCommands() override {
        auto out = std::move(emitted);
        emitted.clear();
        return out;
    }
    const std::vector<VariantCommand>& pendingCommands() const noexcept override { return emitted; }
    int nextWakeupMs() const override { return -1; }
    bool needsFrame() const noexcept override { return false; }
    void setNeedsFrame(bool) noexcept override {}
    void setNextDeadlineMs(std::optional<int>) override {}
    void unsubscribeOwner(void* owner) override {
        subs.erase(std::remove_if(subs.begin(), subs.end(), [&](auto &e){
            return e.owner==owner;
        }), subs.end());
    }

    void clear() {
        handled.clear();
        emitted.clear();
        subs.clear();
    }

    std::vector<VariantEvent> handled;
    std::vector<VariantCommand> emitted;

private:
    struct Entry {
        void* owner;
        SubMask mask;
        Callback cb;
    };
    std::vector<Entry> subs;
    std::vector<EventSource*> sources;
    std::vector<CommandReceiver*> receivers;
};

class FakeEngineState : public IEngineState {
public:
    void handle(const VariantEvent& ev) override { handled.push_back(ev); }
    void addEventSource(EventSource* s) override { sources.push_back(s); }
    void addCommandReceiver(CommandReceiver* r) override { receivers.push_back(r); }
    std::vector<VariantCommand> drainCommands() override {
        auto out = std::move(cmds);
        cmds.clear();
        return out;
    }
    int nextWakeupMs() const override { return -1; }
    bool needsFrame() const noexcept override { return false; }
    void setNeedsFrame(bool) noexcept override {}
    void emitCommand(VariantCommand c) {
        cmds.push_back(c);
    }

    std::vector<VariantEvent> handled;
    std::vector<VariantCommand> cmds;
    std::vector<EventSource*> sources;
    std::vector<CommandReceiver*> receivers;
};

} // namespace astick
