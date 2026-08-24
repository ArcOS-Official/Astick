#pragma once
#include "state.h"
#include "event.h"
#include "command.h"
#include <chrono>
#include <unordered_map>
#include <vector>

namespace astick {

// AnimationManager: EventSource + Listener (subscribes to Window/Output).
// Produces AnimTick events via poll() and pushes SetWindowBox/Opacity commands.
// Zero-copy: samples existing anims in place, moves AnimTick into vector via emplace.

struct AnimSpec {
    AnimationId id = 0;
    WindowId windowId = 0;
    Box from{0,0,0,0};
    Box to{0,0,0,0};
    float fromOpacity = 1.0f;
    float toOpacity = 1.0f;
    uint64_t startMs = 0;
    uint64_t durationMs = 250;
    bool running = false;

    double tFor(uint64_t nowMs) const noexcept {
        if (!running || durationMs==0)
            return 1.0;
        if (nowMs <= startMs)
            return 0.0;
        uint64_t elapsed = nowMs - startMs;
        if (elapsed >= durationMs)
            return 1.0;
        return double(elapsed) / double(durationMs);
    }
    Box sample(uint64_t nowMs) const noexcept {
        double t = tFor(nowMs);
        // Linear lerp — caller may ease via progress mapping (no extra copy)
        auto lerp = [&](int a,int b){
            return int(a + (b-a)*t);
        };
        return Box{lerp(from.x,to.x), lerp(from.y,to.y), lerp(from.width,to.width), lerp(from.height,to.height)};
    }
    float opacity(uint64_t nowMs) const noexcept {
        double t = tFor(nowMs);
        return float(fromOpacity + (toOpacity - fromOpacity)*t);
    }
    bool isRunning(uint64_t nowMs) const noexcept { return running && tFor(nowMs) < 1.0; }
};

class AnimationManager : public EventSource {
public:
    explicit AnimationManager(IStateManager& s);
    ~AnimationManager() override = default;

    // EventSource: called from Engine::run() each loop, returns ticks by move.
    std::vector<VariantEvent> poll() override;
    std::optional<int> nextWakeupMs() const override;

    // Listener: called via State subscription (Window/Output)
    void onWindowOrOutput(const VariantEvent& ev);

    // API for LayoutManager/Compositor to start/cancel — moves Box not copies large structs
    void startWindowAnim(WindowId win, Box from, Box to, uint64_t durationMs = 250);
    void cancelForWindow(WindowId win) noexcept;

    void setMaxFps(int fps) noexcept { maxFps = fps; }
    int getMaxFps() const noexcept { return maxFps; }

private:
    IStateManager* state_ = nullptr;
    Subscription sub_;
    std::unordered_map<WindowId, AnimSpec> anims_;
    AnimationId nextId_ = 1;
    int maxFps = 60;
    uint64_t lastPollMs_ = 0;

    static uint64_t nowMs() noexcept;
};
} // namespace astick
