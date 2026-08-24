#include "animation_manager.h"
#include <chrono>

namespace astick {

AnimationManager::AnimationManager(IStateManager& s) : state_(&s) {
    // Subscribe to Window + Output kind — one callback with variant
    SubMask m{uint32_t(EventKind::Window) | uint32_t(EventKind::Output), std::nullopt};
    sub_ = s.subscribe(this, m, [this](const VariantEvent& ev){
        onWindowOrOutput(ev);
    });
}

uint64_t AnimationManager::nowMs() noexcept {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now().time_since_epoch()).count();
}

void AnimationManager::onWindowOrOutput(const VariantEvent& ev) {
    if (auto* w = std::get_if<WindowEvent>(&ev)) {
        if (!w->hasEvent)
            return;
        if (w->kind == WindowEvent::Kind::Mapped) {
            // Could start window open anim here; currently driven by LayoutManager startWindowAnim
        } else if (w->kind == WindowEvent::Kind::Destroy) {
            cancelForWindow(w->id);
        }
    } else if (auto* o = std::get_if<OutputEvent>(&ev)) {
        (void)o;
        // Output change may affect animation frame scheduling
    }
}

void AnimationManager::startWindowAnim(WindowId win, Box from, Box to, uint64_t durationMs) {
    AnimSpec spec;
    spec.id = nextId_++;
    spec.windowId = win;
    spec.from = from;
    spec.to = to;
    spec.startMs = nowMs();
    spec.durationMs = durationMs;
    spec.running = true;
    // emplace without copy of Boxes (trivial)
    anims_.insert_or_assign(win, spec);
    if (state_)
        state_->setNeedsFrame(true);
}

void AnimationManager::cancelForWindow(WindowId win) noexcept {
    anims_.erase(win);
}

std::vector<VariantEvent> AnimationManager::poll() {
    std::vector<VariantEvent> out;
    if (anims_.empty()
        ) return out;
    uint64_t now = nowMs();
    lastPollMs_ = now;
    out.reserve(anims_.size()); // one allocation per frame, reused next frame by Engine clear

    std::vector<WindowId> toErase;
    toErase.reserve(anims_.size());

    for (auto& kv : anims_) {
        AnimSpec& anim = kv.second;
        if (!anim.running)
            continue;

        double t = anim.tFor(now);
        Box cur = anim.sample(now);
        float op = anim.opacity(now);

        // Emit command to Engine without copying large payload (Box trivial)
        if (state_) {
            state_->emitCommand(Cmd::SetWindowBox{anim.windowId, cur, false});
            state_->emitCommand(Cmd::SetWindowOpacity{anim.windowId, op});
            state_->emitCommand(Cmd::RequestFrame{0});
            state_->setNeedsFrame(true);
        }

        // Produce tick event by emplace (no heap)
        AnimTick tick;
        tick.id = anim.id;
        tick.t = float(t);
        tick.box = cur;
        tick.opacity = op;
        tick.hasEvent = true;
        out.emplace_back(tick); // trivial copy

        if (t >= 1.0) {
            anim.running = false;
            toErase.push_back(kv.first);
        }
    }
    for (WindowId id : toErase) anims_.erase(id);
    if (!anims_.empty()
        && state_) state_->setNeedsFrame(true);
    return out; // NRVO/move
}

std::optional<int> AnimationManager::nextWakeupMs() const {
    if (anims_.empty())
        return std::nullopt;
    // Wake up at ~1/maxFps if any running
    if (maxFps <= 0) return std::optional<int>{16};
    return std::optional<int>{1000 / maxFps};
}
} // namespace astick
