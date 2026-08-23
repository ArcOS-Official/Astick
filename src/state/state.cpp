#include "state.h"
#include <algorithm>

namespace astick {

Subscription State::subscribe(void* owner, SubMask mask, Callback cb) {
    // Zero-copy: move cb into storage, no copy of variant.
    Entry e{owner, mask, std::move(cb)};
    subs.emplace_back(std::move(e));
    size_t idx = subs.size() - 1;
    std::vector<Subscription::Handle> hs;
    hs.reserve(1);
    hs.push_back({owner, idx});
    return Subscription(this, owner, std::move(hs));
}

void State::unsubscribeOwner(void* owner) {
    // Erase-remove idiom; swaps to keep vector packed. Order not important.
    // Zero-copy: move last element over erased slot instead of shifting all.
    for (size_t i = 0; i < subs.size(); ) {
        if (subs[i].owner == owner) {
            if (i + 1 != subs.size()) subs[i] = std::move(subs.back());
            subs.pop_back();
        } else {
            ++i;
        }
    }
}

void State::handle(const VariantEvent& ev) {
    // Update per-window tracking without copying event.
    if (auto* w = std::get_if<WindowEvent>(&ev)) {
        if (w->hasEvent) {
            switch (w->kind) {
                case WindowEvent::Kind::Mapped: winMgr.onMapped(w->id); break;
                case WindowEvent::Kind::Destroy: winMgr.onDestroy(w->id); break;
                default: break;
            }
        }
        // Cache stable mapping if needed (no string copy)
        if (w->hasEvent && (w->kind == WindowEvent::Kind::Mapped || w->kind == WindowEvent::Kind::Commit)) {
            // stableToLive is lazily filled by Engine via setWindowStable? keep stub
        }
    }
    dispatchToSubs(ev);
}

void State::dispatchToSubs(const VariantEvent& ev) {
    // Zero-copy dispatch: iterate subs, check mask, invoke const&.
    // No variant copy; callers pass ev by reference.
    const EventKind k = eventKind(ev);
    const auto winId = eventWindowId(ev);
    // Avoid re-entrancy issues by indexing, not iterator (subs may be mutated via unsubscribe)
    size_t n = subs.size();
    for (size_t i = 0; i < n; ++i) {
        // Note: if subs grew during dispatch, we ignore new entries this frame (deterministic)
        if (i >= subs.size()) break;
        const Entry& e = subs[i];
        if (!e.cb) continue;
        if ((e.mask.kinds & uint32_t(k)) == 0) continue;
        if (e.mask.window) {
            if (k != EventKind::Window) continue;
            if (!winId || *winId != *e.mask.window) continue;
        }
        // Extra window filter via manager: if event is Window but manager says not live, skip wildcards?
        // We still deliver Destroy even if manager removed, so skip manager check.
        e.cb(ev);
    }
}

int State::nextWakeupMs() const {
    if (nextDeadlineMs) return *nextDeadlineMs;
    // Ask sources for next wakeup without copying.
    int best = -1;
    for (auto* s : sources) {
        if (auto ms = s->nextWakeupMs()) {
            if (best < 0 || *ms < best) best = *ms;
        }
    }
    return best;
}

std::optional<Box> State::WindowHandle::geometry() const {
    // Stub: real implementation queries LayoutManager's snapshot without copying large maps.
    // Return nullopt until LayoutManager is integrated.
    (void)id; (void)state;
    return std::nullopt;
}
} // namespace astick
