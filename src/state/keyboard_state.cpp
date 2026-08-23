#include "keyboard_state.h"

KeyboardState::KeyboardState(State& s) : state_(&s) {
    // Single subscribe call with mask — one callback, no virtual.
    // Callback captures this by raw ptr (lifetime tied to owner), no shared_ptr copy.
    sub_ = s.subscribe(this, SubMask{uint32_t(EventKind::Keyboard), std::nullopt},
        [this](const VariantEvent& ev){
            if (auto* k = std::get_if<Keyboard>(&ev)) onEvent(*k);
        });
}
KeyboardState::~KeyboardState() = default;

void KeyboardState::onEvent(const Keyboard& e) {
    if (!e.hasEvent) return;
    mods_ = e.mods;
    if (e.pressed) pressed_.insert(e.keycode);
    else pressed_.erase(e.keycode);
    // Repeat handling without extra copy: isRepeat flag already set by Engine.
}
