#include "pointer_state.h"

namespace astick {

PointerState::PointerState(State& s) : state_(&s) {
    sub_ = s.subscribe(this, SubMask{uint32_t(EventKind::Pointer), std::nullopt},
        [this](const VariantEvent& ev){
            if (auto* p = std::get_if<Pointer>(&ev)) onEvent(*p);
        });
}
PointerState::~PointerState() = default;

void PointerState::onEvent(const Pointer& e) {
    if (!e.hasEvent) return;
    curX = e.x; curY = e.y;
    // Move/resize handling is layer above: Engine translates cursor motion
    // into drag_. LayoutManager subscribes to Pointer via State directly for tiling.
    // PointerState only caches last position without copying wlr structs.
    if (e.kind == Pointer::Kind::Button && !e.pressed) {
        // Release ends grab — zero-copy reset
        if (drag_.active) drag_.active = false;
    }
}
} // namespace astick
