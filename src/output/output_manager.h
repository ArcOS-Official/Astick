#pragma once
#include "../state/state.h"
#include "../state/event.h"
#include <unordered_map>
#include <cstdint>

namespace astick {

// OutputManager: translates OutputEvent, handles layer exclusive zone.
// Zero-copy: receives const OutputEvent&, computes usable/full via Box math
// without allocating, pushes commands only when needed.

struct OutputInfo {
    uint32_t serial = 0;
    Box usable{0,0,0,0};
    Box full{0,0,0,0};
};

class OutputManager {
public:
    explicit OutputManager(State& s);
    ~OutputManager();

    void onOutput(const VariantEvent& ev);
    OutputInfo infoFor(uint32_t serial) const noexcept;
    size_t count() const noexcept { return outputs.size(); }

private:
    IStateManager* state_ = nullptr;
    Subscription sub_;
    std::unordered_map<uint32_t, OutputInfo> outputs;

    // Layer exclusive zone calc — mirrors compositor usableAreaForOutput but
    // works on Box values only (no wlr_output copy). Takes const& and returns Box.
    static Box applyExclusive(Box full, uint32_t anchor, int32_t zone, Box margin) noexcept;
};
} // namespace astick
