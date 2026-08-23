#include "input_manager.h"
#include "../config.h"

namespace astick {

InputManager::InputManager(State& s) : state_(&s) {
    kbState = std::make_unique<KeyboardState>(s);
    ptrState = std::make_unique<PointerState>(s);
    // Extra subscription for keybind actions — zero-copy: const& variant
    subKey_ = s.subscribe(this, SubMask{uint32_t(EventKind::Keyboard), std::nullopt},
        [this](const VariantEvent& ev){ onKeyboard(ev); });
}
InputManager::~InputManager() = default;

void InputManager::onKeyboard(const VariantEvent& ev) {
    auto* k = std::get_if<KeyEvent>(&ev);
    if (!k || !k->hasEvent || !k->pressed) return;
    // Config lookup would happen here — delegate to Config via Engine or State command
    // Example: auto* bind = findKeybind(k->mods, k->keysym);
    // if (bind) state_->emitCommand(...);
    (void)k;
}

const struct Keybind* InputManager::findKeybind(uint32_t, uint32_t) const {
    return nullptr; // stub until Config is wired
}
} // namespace astick
