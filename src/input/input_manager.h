#pragma once
#include "../state/state.h"
#include "../state/keyboard_state.h"
#include "../state/pointer_state.h"
#include <memory>

namespace astick {

// InputManager owns KeyboardState + PointerState and subscribes in ctor.
// Zero-copy: no wlroots include, only State refs. Delegates to states which
// already subscribe; this manager just aggregates lifetime and keybind dispatch.

class InputManager {
public:
    explicit InputManager(State& state);
    ~InputManager();

    KeyboardState& keyboards() noexcept { return *kbState; }
    PointerState& pointers() noexcept { return *ptrState; }

    // Keybind handling — const& lookup, no string copy per key (Config already hashed)
    const struct Keybind* findKeybind(uint32_t mods, uint32_t keysym) const;

private:
    State* state_ = nullptr;
    std::unique_ptr<KeyboardState> kbState;
    std::unique_ptr<PointerState> ptrState;
    Subscription subKey_; // optional extra keybind dispatch
    void onKeyboard(const VariantEvent& ev);
};
} // namespace astick
