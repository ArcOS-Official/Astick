#include "output_manager.h"

namespace astick {

OutputManager::OutputManager(State& s) : state_(&s) {
    sub_ = s.subscribe(this, SubMask{uint32_t(EventKind::Output), std::nullopt},
        [this](const VariantEvent& ev){ onOutput(ev); });
}
OutputManager::~OutputManager() = default;

void OutputManager::onOutput(const VariantEvent& ev) {
    auto* o = std::get_if<OutputEventent>(&ev);
    if (!o || !o->hasEvent) return;
    OutputInfo info;
    info.serial = o->outputSerial;
    info.usable = o->usable;
    info.full = o->full;
    // emplace without copy of Boxes (trivial), move semantics for map insert
    outputs.insert_or_assign(info.serial, std::move(info));
}

OutputInfo OutputManager::infoFor(uint32_t serial) const noexcept {
    auto it = outputs.find(serial);
    if (it != outputs.end()) return it->second; // copy small struct (32 bytes) — acceptable
    return OutputInfo{};
}

Box OutputManager::applyExclusive(Box full, uint32_t anchor, int32_t zone, Box margin) noexcept {
    Box usable = full;
    // Simplified per-edge logic — mirrors compositor but on Box only
    constexpr uint32_t TOP = 1, BOTTOM = 4, LEFT = 2, RIGHT = 8;
    if (anchor & TOP) { usable.y += zone + margin.y; usable.height -= zone + margin.y; }
    else if (anchor & BOTTOM) { usable.height -= zone + margin.height; }
    if (anchor & LEFT) { usable.x += zone + margin.x; usable.width -= zone + margin.x; }
    else if (anchor & RIGHT) { usable.width -= zone + margin.width; }
    if (usable.width < 0) usable.width = 0;
    if (usable.height < 0) usable.height = 0;
    return usable;
}
} // namespace astick
