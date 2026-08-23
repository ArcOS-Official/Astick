#pragma once
#include <cstdint>
#include <variant>
#include <optional>
#include "ids.h"

namespace astick {

// Lightweight Box — avoids pulling wlroots.h into State domain.
// Engine converts to/from wlr_box at the boundary with a single copy.
struct Box {
    int x = 0, y = 0;
    int width = 0, height = 0;
    constexpr bool operator==(const Box& o) const noexcept {
        return x==o.x && y==o.y && width==o.width && height==o.height;
    }
};

inline Box boxFromWlr(int x,int y,int w,int h) noexcept { return Box{x,y,w,h}; }

// All events are plain classes with public fields and reset().
// Engine owns live instances, fills fields directly (no allocation),
// moves them into VariantEvent via emplace — one move, no heap.
// State reads const& — no copy on dispatch.

class Keyboard {
public:
    uint32_t keycode = 0;
    uint32_t keysym = 0; // xkb_keysym_t
    uint32_t mods = 0;
    bool pressed = false;
    bool isRepeat = false;
    bool hasEvent = false;
    void reset() noexcept { keycode=0; keysym=0; mods=0; pressed=false; isRepeat=false; hasEvent=false; }
};

class Pointer {
public:
    enum class Kind { None, Move, Button, Axis, Frame };
    Kind kind = Kind::None;
    double x = 0, y = 0, dx = 0, dy = 0;
    uint32_t button = 0;
    bool pressed = false;
    uint32_t time = 0;
    double axisDelta = 0;
    bool hasEvent = false;
    void reset() noexcept { kind=Kind::None; x=y=dx=dy=0; button=0; pressed=false; time=0; axisDelta=0; hasEvent=false; }
};

class Window {
public:
    WindowId id = 0;
    enum class Kind { None, Mapped, Unmapped, Commit, Destroy, RequestMove, RequestResize, RequestMaximize, RequestFullscreen };
    Kind kind = Kind::None;
    uint32_t edges = 0;
    bool flag = false;
    int width = 0, height = 0;
    bool hasEvent = false;
    void reset() noexcept { id=0; kind=Kind::None; edges=0; flag=false; width=height=0; hasEvent=false; }
};

class AnimTick {
public:
    AnimationId id = 0;
    float t = 0;
    Box box{0,0,0,0};
    float opacity = 1.0f;
    bool hasEvent = false;
    void reset() noexcept { id=0; t=0; box={0,0,0,0}; opacity=1.0f; hasEvent=false; }
};

class OutputEv {
public:
    uint32_t outputSerial = 0;
    Box usable{0,0,0,0};
    Box full{0,0,0,0};
    bool hasEvent = false;
    void reset() noexcept { outputSerial=0; usable=full={0,0,0,0}; hasEvent=false; }
};

using VariantEvent = std::variant<Keyboard, Pointer, Window, AnimTick, OutputEv>;

enum class EventKind : uint32_t {
    Keyboard = 1u<<0,
    Pointer  = 1u<<1,
    Window   = 1u<<2,
    Anim     = 1u<<3,
    Output   = 1u<<4
};
constexpr EventKind operator|(EventKind a, EventKind b) noexcept {
    return EventKind(uint32_t(a) | uint32_t(b));
}
constexpr uint32_t kindMask(EventKind k) noexcept { return uint32_t(k); }

struct SubMask {
    uint32_t kinds = 0;
    std::optional<WindowId> window;
    static SubMask WindowOf(WindowId id) { return {uint32_t(EventKind::Window), id}; }
    constexpr bool matches(EventKind k, std::optional<WindowId> evWin) const noexcept {
        if ((kinds & uint32_t(k)) == 0) return false;
        if (window) {
            if (k != EventKind::Window) return false;
            if (!evWin || *evWin != *window) return false;
        }
        return true;
    }
};
inline SubMask operator|(EventKind k, SubMask m) noexcept { m.kinds |= uint32_t(k); return m; }
inline SubMask operator|(SubMask a, EventKind b) noexcept { a.kinds |= uint32_t(b); return a; }
inline SubMask operator|(SubMask a, SubMask b) noexcept {
    a.kinds |= b.kinds;
    if (b.window) a.window = b.window;
    return a;
}

// Zero-copy helpers: get kind + window id without copying variant.
inline EventKind eventKind(const VariantEvent& ev) noexcept {
    if (std::holds_alternative<Keyboard>(ev)) return EventKind::Keyboard;
    if (std::holds_alternative<Pointer>(ev)) return EventKind::Pointer;
    if (std::holds_alternative<Window>(ev)) return EventKind::Window;
    if (std::holds_alternative<AnimTick>(ev)) return EventKind::Anim;
    return EventKind::Output;
}
inline std::optional<WindowId> eventWindowId(const VariantEvent& ev) noexcept {
    if (auto* w = std::get_if<Window>(&ev)) return w->id;
    return std::nullopt;
}
} // namespace astick
