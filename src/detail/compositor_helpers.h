#pragma once
// Detail helpers exposed only for tests — not included in normal build.
// Helpers are stack-only, no heap, take const wlr_box& by ref, return wlr_box by value.
#ifdef ASTICK_ENABLE_TESTS

#include <QString>
#include "../wlroots.h"
#include "../config.h"

struct wlr_box;

// Compositor — onOutputAdded decomposition (pure box/state helpers are testable)
namespace astick::detail {

// --- onOutputAdded helpers (resolve → build → commit → persist → wire) ---
QString resolveOutputId(struct wlr_output *output);
struct ResolvedOutput {
    QString oid;
    OutputEntry entry{};
    bool hasEntry = false;
    DefaultOutput def{};
};
ResolvedOutput resolveOutputConfig(Config *config, const QString &oid);
double resolveScale(Config *config, struct wlr_output *output, const ResolvedOutput &ro);
void buildOutputState(struct wlr_output_state *state, struct wlr_output *output,
                      const ResolvedOutput &ro, double scale);
// commit with fallback retries; returns true if any commit succeeded
bool commitOutputStateWithFallback(struct wlr_output *output,
                                   struct wlr_output_state *state,
                                   const ResolvedOutput &ro);
void persistNewOutput(Config *config, const QString &oid,
                      struct wlr_output *output, double scale,
                      const DefaultOutput &def);
int resolveAnimationMaxFps(const QList<struct wlr_output*> &outputs);

// --- arrangeForOutput helpers (resolve → build → commit → persist → wire) ---
struct wlr_box resolveUsableArea(struct wlr_output *output, struct wlr_output_layout *layout);
struct wlr_box applyOuterGap(const struct wlr_box &usable, int outerGap);
struct wlr_box resolveUsableAreaWithGap(struct wlr_output *output,
                                        struct wlr_output_layout *layout,
                                        int outerGap);

// --- layout box helpers (isolated from LayoutManager::arrangeNode) ---
struct BoxPair {
    struct wlr_box first{};
    struct wlr_box second{};
};
BoxPair splitBoxHorizontally(const struct wlr_box &box, double ratio);
BoxPair splitBoxVertically(const struct wlr_box &box, double ratio);
double distanceSquaredToBoxCenter(const struct wlr_box &box, double cx, double cy);
struct wlr_box boxClosestToPoint(const struct wlr_box &a, const struct wlr_box &b,
                                 double cx, double cy);
// choose closest among two by center distance
int indexClosestToPoint(const struct wlr_box *boxes, int count, double cx, double cy);

} // namespace astick::detail

#endif
