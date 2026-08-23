#pragma once
#include "state.h"
#include "event.h"
#include <optional>

namespace astick {

// PointerState: holds drag/grab state that was previously in CursorManager.
// Zero-copy: updates via const Pointer& directly, no wlr_cursor copy.
class PointerState {
public:
    explicit PointerState(State& s);
    ~PointerState();

    void onEvent(const Pointer& e);

    // Drag state — public for LayoutManager to query without copy
    struct Drag {
        bool active = false;
        WindowId grabbedWindow = 0;
        double grabX = 0, grabY = 0;
        uint32_t resizeEdges = 0;
    };
    const Drag& drag() const noexcept { return drag_; }
    void beginMove(WindowId win, double x, double y) noexcept { drag_ = {true, win, x, y, 0}; }
    void beginResize(WindowId win, double x, double y, uint32_t edges) noexcept { drag_ = {true, win, x, y, edges}; }
    void reset() noexcept { drag_ = {}; }

    double x() const noexcept { return curX; }
    double y() const noexcept { return curY; }

private:
    State* state_ = nullptr;
    Subscription sub_;
    Drag drag_{};
    double curX = 0, curY = 0;
};
} // namespace astick
