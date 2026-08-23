#include "engine.h"
// This file is the engine's sole place for wl_listener glue that
// translates wlr_* events into State's public field structs.
// All listeners fill public fields directly — no allocation per event.
// The Engine::run() loop resets fields at top, so stale never leaks.

 // Example: keyboard key listener (would be connected to wlr_keyboard key events)
 // void engine_keyboard_key(void* data, Keyboard& kb) {
 //     auto* ev = (wlr_keyboard_key_event*)data;
 //     kb.keycode = ev->keycode + 8;
 //     kb.pressed = ev->state == WL_KEYBOARD_KEY_STATE_PRESSED;
 //     kb.hasEvent = true;
 // }

// For now this file holds only documentation; actual listener bodies
// are in engine.cpp to keep single translation unit simple.
// Keeping this file ensures src/wlroots.h:41 is only included via engine/*
// as required by the plan file isolation rule.
