#pragma once
#include <cstdint>
#include <variant>
#include <functional>
#include "ids.h"
#include "event.h"

namespace astick {

// Commands: State -> Engine. All by-value, moved not copied.
// Engine drains with std::move — zero extra copies.
// Boxes are plain ints, no heap.

namespace Cmd {
    struct SetWindowBox {
        WindowId id = 0;
        Box box{};
        bool isFloating = false;
    };
    struct SetWindowActivated { WindowId id = 0; bool active = false; };
    struct SetWindowOpacity { WindowId id = 0; float opacity = 1.0f; };
    struct SetWorkspace { int workspace = 1; };
    struct RequestFrame { uint32_t outputSerial = 0; };
    struct CreateSnapshot { WindowId id = 0; };
    struct DestroySnapshot { WindowId id = 0; };
    struct ConfigureToplevel { WindowId id = 0; int width = 0; int height = 0; };
}

using VariantCommand = std::variant<
    Cmd::SetWindowBox,
    Cmd::SetWindowActivated,
    Cmd::SetWindowOpacity,
    Cmd::SetWorkspace,
    Cmd::RequestFrame,
    Cmd::CreateSnapshot,
    Cmd::DestroySnapshot,
    Cmd::ConfigureToplevel
>;

// Zero-copy dispatch: visitor takes const& not by value.
template<class... Ts> struct Overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> Overloaded(Ts...) -> Overloaded<Ts...>;
} // namespace astick
