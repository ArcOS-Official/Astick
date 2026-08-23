#pragma once
#include "state.h"
#include "event.h"
#include <unordered_set>
#include <cstdint>

// KeyboardState: normal consumer subscribed via State, no wlroots include.
// Tracks pressed keys/mods without heap copies per event (just ints).
class KeyboardState {
public:
    explicit KeyboardState(State& s);
    ~KeyboardState();

    // Zero-copy: receives const Keyboard& via variant visitation, no allocation.
    void onEvent(const Keyboard& e);

    uint32_t mods() const noexcept { return mods_; }
    bool isPressed(uint32_t keycode) const noexcept { return pressed_.find(keycode) != pressed_.end(); }
    size_t pressedCount() const noexcept { return pressed_.size(); }

private:
    State* state_ = nullptr;
    Subscription sub_;
    uint32_t mods_ = 0;
    std::unordered_set<uint32_t> pressed_;
};
