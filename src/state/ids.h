#pragma once
#include <cstdint>
#include <functional>
#include <string_view>
#include <limits>

// Zero-copy IDs: only Window & Animation have live IDs.
// StableKey is a hash of app_id string_view — no string copy.
using WindowId = uint64_t;
using AnimationId = uint64_t;

struct WindowStableKey {
    uint64_t hash = 0;
    constexpr bool operator==(const WindowStableKey& o) const noexcept { return hash == o.hash; }
    constexpr bool operator!=(const WindowStableKey& o) const noexcept { return hash != o.hash; }
};

// Monotonic counters owned by Engine (single writer, no atomic).
// IDs are never fabricated outside Engine; State only routes them.

namespace detail {
// FNV-1a 64 — zero-copy over string_view, no allocation.
constexpr uint64_t fnv1a64(std::string_view s) noexcept {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) { h ^= c; h *= 1099511628211ull; }
    return h;
}
constexpr uint64_t hashCombine64(uint64_t a, uint64_t b) noexcept {
    a ^= b + 0x9e3779b97f4a7c15ULL + (a<<6) + (a>>2);
    return a;
}
}

inline WindowStableKey stableKeyFromAppId(std::string_view appId) noexcept {
    // Zero-copy: appId is string_view into wlroots memory (no copy).
    // Null/empty appId hashes to pointer-derived stable fallback set by Engine.
    if (appId.empty()) return WindowStableKey{0};
    return WindowStableKey{detail::fnv1a64(appId)};
}

inline WindowStableKey stableKeyFromHash(uint64_t h) noexcept {
    return WindowStableKey{h};
}
