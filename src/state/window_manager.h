#pragma once
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "ids.h"

namespace astick {

// Minimal per-WindowId tracking for O(1) dispatch.
// Zero-copy: keys are uint64_t, values are indices not copies of Window objects.
class WindowEventManager {
public:
    bool shouldDeliver(WindowId id) const noexcept {
        // If no per-window filter registered, deliver to all window subscribers.
        // This manager only tracks existence; actual filtering is in State::dispatchToSubs.
        auto it = liveIds.find(id);
        return it != liveIds.end();
    }
    void onMapped(WindowId id) {
        liveIds.insert(id);
    }
    void onDestroy(WindowId id) {
        liveIds.erase(id);
    }
    void clear() noexcept { liveIds.clear(); }
    size_t liveCount() const noexcept { return liveIds.size(); }
private:
    std::unordered_set<WindowId> liveIds;
};
} // namespace astick
