# PLAN.md — Astick Code Quality Plan (Revised per Owner Preferences — v2: no-CI, in-binary test + stress/fuzz)

> Generated 2026-08-23. Read-only plan. **Do not execute** — design to be reviewed before any code changes.
> v1 assumed SRP/clean-code + GitHub Actions; owner rejected both. v2 keeps the god object, no CI, and moves all verification into a single test binary + `-st` stress/fuzz mode.

## 0. Owner Constraints (non-negotiable)

1. **Keep the god object.** `src/compositor.h:40` / `src/compositor.cpp` (1938 LOC) stays central. One place to read top-to-bottom > SRP jumps. Do NOT split Compositor.
2. **Stupidly simple architecture always.** No DDD, DI, service locator, mediator that hides flow.
3. **Do NOT touch:** `LICENSE` boilerplate, friend `handle_*` wl listeners (`src/compositor.h:145-150`, `src/compositor.cpp:33-71`), `compile_commands.json` duplication, `build/` gitignore.
4. **Encapsulation is not a goal.** Public members on `Config`, `Resource`, `Toplevel`, `LayoutManager::Workspace` stay. No getters for purity.
5. **AnimationManager stays singleton** (`src/animation.h:143` `static s_instance`).
6. **C++ version:** Prefer Qt alternatives over newer std. Downgrade from C++23 if not needed.
7. **NEW: No CI.** Do not add `.github/workflows/ci.yml`. Verification must be a local binary that builds in `build/`.
8. **NEW: Heap/cache discipline.** Decrease heap allocations/accesses as much as possible, stay cache-friendly, do not introduce regressions while making things testable/modifiable at runtime.

## 1. What this plan IS trying to fix

- **Tiny-header fatigue:** `src/resource.h` (52 LOC), `src/util.h` (27 LOC), `src/popup.h`, `src/layersurface.h`, `src/output.h`, `src/cursor.h`, `src/debug.h`, `src/input/keyboard.h`/`mouse.h` force constant jumps.
- **Logic in headers:** Templates/inlines in `src/animation_pool.h:36-219`, `src/layout.h:47-66`, `src/config.h:140-192`, `src/resource.h:15-38`. Violates “headers = declarations, .cpp = logic”.
- **Monolithic functions doing too much inline:** `Compositor::onOutputAdded` (~120 LOC), `onToplevelAdded`, `LayoutManager::arrange*`/`removeRecursive` etc. All wlroots/math inlined.
- **Random ID generation:** `src/resource.h:15-38` uses `thread_local mt19937_64` + `randomInRange`. Non-deterministic, breaks session save/restore. Must not hash title/size (owner forbids).
- **No log-and-continue error handling:** wlroots calls fire-and-forget (`wlr_output_commit_state`, `wlr_scene_*`).
- **Qt under-used / std over-used:** `std::unordered_map`/`std::string`/`std::vector`/`std::optional`/`std::filesystem` vs `QHash`/`QString`/`QList`/`QFileInfo`. Pulls in C++23 for no reason.
- **Zero tests / observability:** No `tests/`, no way to run headless or dump state each frame, no stress/fuzz story. Owner wants all of this inside the compositor binary itself.

## 2. Guiding Principles

1. **Read top-to-bottom in one file > jump 10 files.** Unify small headers; keep flow in `compositor.cpp`/`layout.cpp`/`config.cpp`, but decompose *inside the .cpp*.
2. **Pattern-coupled helpers in the .cpp.** Any operation doing “a bunch of calculations and/or low-level calls” becomes free/static functions in that `.cpp` anonymous namespace, grouped by pattern `resolve* → build* → commit* → persist* → wire*`. Public method reads like a recipe.
3. **Headers = pure declarations.** No inline, no template bodies. Templates → explicit instantiations in `.cpp`.
4. **Lean on Qt.** `QString`/`QList`/`QHash`/`QFile`/`QDir`/`QStandardPaths`/`QJsonDocument`/`qWarning`. Drop `std::*` where Qt has direct alternative.
5. **Deterministic IDs without content hashing.** Per-kind monotonic counters, not geometry/title.
6. **Log-and-continue, not crash.** Helper that logs to `wlr_log` + `Debugger` + `qWarning` and returns safe default.
7. **Testability without heap/cache regressions.** Keep hot paths allocation-free; expose state via plain structs/views, not heap-allocated wrappers.

## 3. Non-Goals

- Splitting `Compositor`.
- Fixing `src/wlroots.h:41-68` hack.
- Rewriting `compile_commands.json`.
- Removing license headers or adding encapsulation.
- Introducing DI/plugin system.
- Adding GitHub Actions / external CI (per v2).

## 4. Proposed Target State

### 4.1 Header Unification (fewer, fatter headers, zero logic)

16 headers → 7-8:

| New header | Merges | Notes |
|---|---|---|
| `src/resource.h` | stays | Keep `class Resource` + `namespace ResourceKind` decls only. Move `genId`/`hashCombine`/`randomInRange` bodies to `resource.cpp` (currently 1 LOC). |
| `src/surfaces.h` | `toplevel.h` + `popup.h` + `layersurface.h` + `output.h` + `cursor.h` | One surface header stops jumps. Each class stays `QObject`/`Resource`. Move inline `get()` to `.cpp`. |
| `src/input.h` | `input/keyboard.h` + `input/mouse.h` | Single input include. |
| `src/layout.h` | stays trimmed | Keep `LayoutManager` + `BspNode` decls; move `makeLeaf`/`makeBranch`/`opposite()` (`src/layout.h:47-66`) to `layout.cpp`. Remove duplicate `#include <qobject.h>` (`src/layout.h:10` vs `3`). |
| `src/config.h` | stays trimmed | Keep structs + `class Config`; move `AnimationsConfig::pairFor`/`isEnabled`/`durationFor` (`src/config.h:140-192`) + `Keybind::matches` to `config.cpp`. |
| `src/animation.h` | `animation.h` + `animation_pool.h` merged | Only decls for `Animation`/`AnimationManager`/`AnimationInstanceBase`/`AnimationPool`. Template `addInstance<T>` bodies → `animation_pool.cpp` with explicit instantiations (`int`/`float`/`double`). |
| `src/util.h` + `src/debug.h` + `src/decoration.h` | maybe `src/common.h` | At minimum, headers become decl-only; `signal` macro stays (owner forbids fixing friends). |

Rule: `grep -n "{" src/*.h | grep -v "class\|struct\|enum"` finds almost nothing except `Q_OBJECT`.

CMake: `CMakeLists.txt:44-63` keeps `.cpp` split for parallelism even if headers unified. Unified header is for reading, not for merging object files.

### 4.2 Implementation-file Decomposition by Pattern

Every top-level operation → thin orchestrator calling 4-8 `static` helpers in anonymous namespace.

Example `Compositor::onOutputAdded` (`src/compositor.cpp:75-237`):

```
void Compositor::onOutputAdded(wlr_output *output) {
    auto oid = resolveOutputId(output);
    auto [entry, hasEntry, def] = resolveOutputConfig(oid);
    double scale = resolveScale(output, entry, hasEntry, def);
    auto state = buildOutputState(output, entry, hasEntry, def, scale);
    bool ok = commitOutputStateWithFallback(output, state);
    if (!hasEntry) persistNewOutput(oid, output, scale, def);
    auto *out = createAndWireOutput(output, oid);
    updateAnimationMaxFps();
}
```

Same for `onToplevelAdded` (`createSceneTreeBelowPopup` → `createToplevelResource` → `wireToplevelSignals` → `handleMove/Resize/Maximize/Fullscreen/DestroyDeferred`), `arrangeForOutput` (`resolveUsableArea` → `applyOuterGap` → `snapshotGeometries` → `animateOrArrange` → `finalizeArrange`), `LayoutManager::arrangeNode` (isolate `splitBoxHorizontally`/`splitBoxVertically`/`boxClosestToPoint`).

Result: `compositor.cpp` still one file to `less` top-to-bottom, but helpers are individually testable/loggable without heap.

### 4.3 Deterministic ID Generation (replaces `src/resource.h:15-38`)

Requirement: deterministic across restarts for session save, not derived from `width/height/title`.

- Per-kind monotonic counters: `static QHash<int, uint64_t> s_nextId` keyed by `ResourceKind` base (keep ranges `WindowBase=1`, `WorkspaceBase=1001`, `OutputBase=2001`, etc. `src/resource.h:42-51`).
- `generateId() { id = base + (s_nextId[base]++ % CountPerKind); }` Wrap logs via log-and-continue (4.5).
- Persist `nextId` map to `~/.config/Astick/session.json` (or `QStandardPaths::AppStateLocation`, open question). Loaded before any `Resource` created; persisted batched.
- Do not hash `make/model/serial` (`Config::outputId` `src/config.cpp:172-190`) into numeric id; keep `outputId` string as secondary stable key only.
- Remove `mt19937_64`/`random_device`; if randomness needed use `QRandomGenerator`.
- Add `Resource::resetForTests()` to zero counters for deterministic test runs.

`src/resource.h` keeps only decl; body in `resource.cpp`.

### 4.4 Lean onto Qt / Downgrade C++

| std | Qt | Action |
|---|---|---|
| `std::string` | `QString`/`QByteArray` | Prefer `QString` at API; keep `std::string` only at wlroots/xkbcommon boundary (`toUtf8().constData()`). |
| `std::unordered_map<std::string, OutputEntry>` | `QHash<QString, OutputEntry>` | Drops `<unordered_map>`. |
| `std::vector<Keybind>` | `QList<Keybind>` | Already used in `Compositor`. |
| `std::optional<double>` | keep or sentinel `-1` | No C++23 needed. |
| `std::filesystem` (`Config::defaultPath`) | `QFileInfo`/`QDir`/`QStandardPaths` | Drops C++23 requirement. |
| `std::filesystem::path` param | `QString` | Add overload for compat. |

`CMakeLists.txt:4-6`: `CMAKE_CXX_STANDARD 23` → `17` (or `20` if needed), `CMAKE_CXX_EXTENSIONS ON` → `OFF`. wlroots 0.19 is C11, builds fine with 17.

### 4.5 Error handling — log and move on

New helper in `src/util.h`/`src/debug.h` → `src/util.cpp`:

```cpp
void logAndContinue(const QString &ctx, const QString &detail);
bool logIf(bool ok, const char *ctx, const char *fmt, ...);
template<typename T> T valueOrLog(T v, T fallback, const char *ctx);
```

- Writes `wlr_log(WLR_ERROR)`, appends `Debugger::errors` (`src/debug.h:28`, add `QMutex`), emits `qWarning().noquote() << "[Astick]" << ctx << detail`.
- Never throws/aborts; returns safe fallback (null/0/empty `wlr_box`).
- One `qInstallMessageHandler` in `src/main.cpp:14`.

### 4.6 Tests — in-binary, no CI, struct-based runner (REVISED v2)

**No `.github/workflows/`, no `ctest`, no external harness.** Per owner: tests compile into a binary that lives in `build/` and runs itself.

#### 4.6.1 Build artifact

- New `src/test_runner.h` + `src/test_runner.cpp` (~300 LOC, decl-only header).
- Top-level `CMakeLists.txt` gains `option(ASTICK_TESTS OFF)` — when `ON`, defines `ASTICK_ENABLE_TESTS` and compiles `test_runner.cpp` + all `src/*_test.cpp` (or guards with `#ifdef`) into the same `Astick` executable plus a convenience alias target `AstickTests`. Alternatively produce two binaries from same sources: `build/Astick` (normal) and `build/AstickTests` (same objects + `test_runner.cpp` main wrapper). Simpler: single binary that in `--test` / default test mode runs tests then exits.
- Owner spec says “compile a test binary in the build dir which runs tests” → implement as `build/Astick --test` (or `build/AstickTest`) as separate CMake target `add_executable(AstickTest EXCLUDE_FROM_ALL ...)` that reuses `SOURCES` + `test_runner.cpp`. Both approaches satisfy “in build dir, no CI”.

#### 4.6.2 Test model — struct + callback + name

```cpp
// src/test_runner.h — decl only
struct TestCase {
    QString name;                                   // e.g. "layout.bsp.splitBoxHorizontally"
    std::function<bool(QString *outDetail)> run;    // return true=pass, false=fail; writes detail on fail
    bool enabled = true;
};

struct TestResult {
    QString name;
    bool passed;
    QString detail;   // failure reason or extra info
    qint64 elapsedMs;
};

class TestRunner : public QObject { // QObject only for qDebug integration if needed; not required
    Q_OBJECT
public:
    static TestRunner &instance();
    void add(TestCase tc);
    QList<TestResult> runAll(); // continues after failure
    void printResults(const QList<TestResult> &); // prints RUNNING/PASS/FAIL per test
};
```

- Registry is a plain `QList<TestCase>` (or `std::vector` but prefer `QList` per Qt-leaning). No heap churn per test beyond `QString` detail.
- Each test file registers via static initializer: `static bool _reg = (TestRunner::instance().add({"layout.bsp...",&func}), true);` — or explicit `registerAllTests()` called from runner to stay cache-friendly and avoid global ctors if preferred. Owner says make it modifiable while it runs, so `registerAllTests()` that can be called after `QCoreApplication` is up is cleaner.
- Runner prints exactly as requested:

```
[TEST] RUNNING layout.bsp.splitBoxHorizontally
[TEST] PASS   layout.bsp.splitBoxHorizontally (0.2 ms)
[TEST] RUNNING config.outputId.sanitize
[TEST] FAIL   config.outputId.sanitize (1.1 ms) — got "a b" expected "a_b"
...
[TEST] SUMMARY 42 ran, 41 passed, 1 failed
```

- **Continue after failure**: loop does not break; collects all `TestResult`.
- **Heap/cache discipline**:
  - `TestCase::run` is `std::function` — small functor, but hot path is not in compositor frame loop; okay. For truly cache-friendly stress mode (4.7) we avoid `std::function` there.
  - Avoid per-test `new`: `TestResult` stored in `QList` pre-reserved (`reserve(n)`).
  - `detail` is `QString` which is implicitly shared (copy-on-write) — no deep copy unless mutated.
  - Compositor hot paths that tests touch (layout math, `wlr_box` splits) are extracted to inline-friendly helpers that take `const wlr_box &` by ref and return `wlr_box` by value — no heap.

Invocation:

```sh
cmake -B build -DASTICK_TESTS=ON
cmake --build build -j$(nproc)
./build/Astick --test            # or ./build/AstickTest
# or: ./build/AstickTests --test
```

Exit code 0 if all pass, 1 if any fail. No extra tooling.

#### 4.6.3 Where tests live

- Co-located helpers: `src/layout_test.cpp`, `src/config_test.cpp`, `src/resource_test.cpp`, `src/animation_test.cpp`, `src/state_dump_test.cpp` — each includes the relevant header + calls the anonymous-namespace helpers via a `detail` namespace exposed only under `ASTICK_ENABLE_TESTS` (`src/detail/compositor_helpers.h` trick, but header remains decl-only in normal builds). Keeps helpers testable without polluting public API.
- Or keep tests inline at bottom of `src/layout.cpp` under `#ifdef ASTICK_ENABLE_TESTS` guarded `registerLayoutTests()` — even fewer files to jump, matches “stupidly simple”. Preferred per owner’s “unify into headers/.cpp as much as possible”.

### 4.7 Stress / fuzz mode — `-st` (simulated clients, weird scenarios, state mutation, `fuzz.log.json`) (REVISED v2)

Owner: “it will then start going crazy making fake clients and alot of stuff like that and stress testing it into weird scenarios then if anything unusual or non-determenistic/incrorrect is found or maybe just a crash it adds it into some fuzz.log.json file” + “manipulate the internal state of the compositor directly while it's running” + “very testable and modifiable while it runs”.

#### 4.7.1 CLI

```
Astick --help:
  --test                 Run struct-based unit tests (4.6) and exit
  -st, --stress-test     Run compositor normally but also spawn stress/fuzz engine
  --stress-seed <n>      Deterministic seed (default = 1)
  --stress-duration <s>  Seconds to run (default 30, 0 = forever)
  --stress-rate <hz>     Fake events per second (default 200)
  --stress-dump <path>   fuzz log path (default ./fuzz.log.json, also fuzz.log.jsonl if jsonl preferred)
  --headless --dry-run   No DRM, virtual output 1280x720, one arrange cycle, dump and exit (useful with -st for CI-like local check without CI)
```

`-st` does NOT replace normal `app.exec()` — it starts the normal compositor loop and concurrently drives a `StressEngine` that pokes it. This keeps wlroots backend, scene, outputs, inputs alive.

#### 4.7.2 StressEngine design (lives in `src/stress.h`/`src/stress.cpp`, ~500-700 LOC, decl-only header)

```cpp
// src/stress.h
class StressEngine : public QObject {
    Q_OBJECT
public:
    explicit StressEngine(Compositor *comp, const StressConfig &cfg, QObject *parent=nullptr);
    void start(); // connects to compositor signals + starts QTimer
    void stop();
    struct Counters { uint64_t ticks=0, fakeClients=0, moves=0, resizes=0, workspaceSwitches=0, anomalies=0; };
    Counters counters() const;
signals:
    void anomalyDetected(const QJsonObject &entry);
private slots:
    void onTick(); // QTimer at stress-rate
private:
    Compositor *comp;
    StressConfig cfg;
    QTimer timer;
    QRandomGenerator rng; // seeded, deterministic — QRandomGenerator::securelySeeded() for non-deterministic only if requested
    QList<FakeClient> fakeClients; // lightweight structs, not wlroots objects where possible
    // per-tick actions:
    void maybeCreateFakeClient();
    void maybeDestroyFakeClient();
    void randomMoveOrResize();
    void randomWorkspaceSwitch();
    void directlyMutateState(); // the “manipulate internal state directly” part
    void checkInvariantsAndLog(); // after each mutation
};
```

- **Fake clients**: not full `wlr_xdg_toplevel` (too heavy). Two tiers:
  - Tier 1 (cheap, heap-free): `FakeClient { uint64_t id; wlr_box box; bool mapped; }` inserted into a shadow list that `StressEngine` uses to call `Compositor`/`LayoutManager` directly: `layout->addWindow(fakePtr, ws, focused, usable)`, `layout->removeWindow(...)`, `layout->setFullscreen(...)`, etc. The `fakePtr` can be a lightweight `Toplevel` stub allocated from a pool (see 4.8 arena) — heap allocation minimized, not per-tick `new Toplevel`.
  - Tier 2 (real wlroots): optionally spawn real `wlr_xdg_shell` clients via `wl_client` loopback if `WLR_USE_UNSTABLE` allows — but v1 can be Tier 1 only to stay simple and not require nested Wayland display. Real clients add coverage later.
- **“Going crazy” scenarios** (randomized, seeded):
  - Rapid `addWindow`/`removeWindow` interleaved with `toggleFloating`, `setFullscreen`, `toggleSplitOrientation`, `insertWindowAtCursor`, `arrangeForOutput`.
  - Zero/one/many windows per workspace, empty BSP trees, `ratio` at `minRatio`/`maxRatio` edges (0.1/0.9 from `bsp` config).
  - Outputs added/removed (simulate `onOutputAdded` with `wlr_headless_output`), scale/DPI edge values.
  - Cursor at extreme coordinates, resize edges combos.
- **Direct internal state mutation** (the most powerful part):
  - `directlyMutateState()` picks a random field in `Compositor`/`LayoutManager` that is *safe to corrupt in a test* and flips it, then checks invariants. Examples: set `detachedWindow` to nullptr mid-drag, set `BspNode::ratio` to NaN/out-of-range, set `Workspace::mode` to invalid enum, set `Output::workspace` to non-existent id, corrupt `AnimationPool` entry duration to negative.
  - All mutations go through a whitelist table so we don’t corrupt memory unsafely: `struct MutableField { const char *path; std::function<QJsonValue()> get; std::function<void(QJsonValue)> set; }`. The engine picks one, saves old value, sets new, then `checkInvariantsAndLog()` validates.
  - **Modifiable while it runs**: expose `StressEngine` via `Compositor::getStressEngine()` and a tiny REPL over stdin or `SIGUSR1`: `kill -USR1 $(pidof Astick)` prints counters; `echo '{"rate":500}' > /tmp/astick.stress.cmd` hot-patches `cfg.rate`. No restart needed.
- **Invariant checks** (what counts as “unusual / non-deterministic / incorrect”):
  - Geometry invariant: every `BspNode` leaf box within `usable` + gaps; no negative width/height; `ratio` in `[minRatio, maxRatio]`; `id` unique and in kind range.
  - Determinism invariant: re-running `arrange(usable, ws)` with same inputs produces identical `snapshotGeometries` (hash comparison). If not, log determinism bug.
  - No-crash invariant: any `SIGSEGV`/`abort` caught via `qInstallMessageHandler` + `std::set_terminate` that writes to `fuzz.log.json` before re-raising.
  - Error invariant: any `logAndContinue` call counts as anomaly if it fires during stress (unexpected path).

#### 4.7.3 `fuzz.log.json` format (expressive + `jq`-friendly, chosen after re-evaluating “better idea”)

You asked for “expressive enough, probably some dump of all current state in json or something each frame (find out a better idea)” — re-evaluated:

| Candidate | Why not |
|---|---|
| CBOR / binary | Not human-readable, needs extra tool, bad for `fuzz.log.json` sharing |
| SQLite / journal | Overkill, not line-oriented |
| Single giant JSON | Unparseable if crash mid-write |

**Chosen: `fuzz.log.json` as JSON Lines (one JSON object per anomaly) + periodic full state snapshots.** Each line is self-contained:

```json
{"t": 1714051200.123, "tick": 421, "seed": 1, "kind": "invariant", "name": "bsp.ratio.out_of_range", "detail": {"node": 42, "ratio": 1.7, "expected": "[0.1,0.9]"}, "state": {"outputs": [...], "workspaces": [...], "windows": [...], "counters": {...}}}
{"t": 1714051201.001, "tick": 600, "kind": "crash", "signal": "SIGSEGV", "stack": "...", "state": {...}}
```

- File default `./fuzz.log.json` (also written as `./fuzz.log.jsonl` symlink for tooling). Append-only, `QFile` + `QTextStream` + `QJsonDocument::toJson(Compact)` + `\n`, `flush()` per anomaly.
- Full state snapshot (`state`) is same schema as 4.7.4 below — reuse `StateDumper`.
- Also keep an in-memory ring buffer of last 100 states so that even if crash loses last write, next run can dump it.

#### 4.7.4 State dump reused for fuzz (the “better idea” for each-frame expressiveness)

Instead of dumping every frame by default (expensive), dump **only on anomaly** + periodic heartbeat (every 5 s) + on `--trace-state` opt-in. Schema:

```json
{
  "t": 12345, "frame": 42, "seed": 1,
  "compositor": {"outputs": 1, "toplevels": 5, "popups": 0, "layers": 2},
  "outputs": [{"id": 2001, "name": "HDMI-A-1", "box": {"x":0,"y":0,"w":1920,"h":1080}, "scale": 1.0, "workspace": 1}],
  "workspaces": [{"id": 1, "mode": "Tiling", "bsp": [{"id": 101, "type": "leaf", "box": {...}, "toplevel": 1}, ...], "floating": [], "fullscreen": null}],
  "windows": [{"id": 1, "title": "foot", "box": {"x":0,"y":0,"w":960,"h":1080}, "mapped": true, "floating": false}],
  "animations": [{"target": 1, "kind": "tilingMove", "progress": 0.3}],
  "config": {"modkey": "Alt", "bsp": {"split_ratio": 0.5}}
}
```

This is already designed to be built from existing `Compositor`/`LayoutManager` state without new tracking. Implementation in `src/state_dump.h`/`state_dump.cpp` (decl-only header, ~150 LOC). `StressEngine::checkInvariantsAndLog()` calls `StateDumper::snapshot()` and writes it into `fuzz.log.json` on anomaly.

#### 4.7.5 Heap/cache discipline for the stress engine

Owner explicitly wants “decrease heap allocation and access as much as possible, staying cache friendly without introducing regressions.”

- **Arena / pool for fake clients**: `FakeClient` structs stored in `QVector<FakeClient> fakeClients` with `reserve(256)` up front; never `new` per tick, just `append` + index reuse. `Toplevel` stubs (if needed) come from a `QVarLengthArray` or a `std::array<FakeToplevel, 256>` arena with free-list — no `malloc` in hot `onTick`.
- **Struct-of-arrays for hot geometry**: `LayoutManager::BspNode` boxes already `int x,y,w,h` — keep them tightly packed; `StressEngine` iterates via `QVector<BspNode*>` collected once per tick via `collectLeaves` (already exists `src/layout.h:192-193`), not via `QList<QPointer>`.
- **No `QString` per tick in hot path**: anomaly `detail` built only on failure; `onTick` uses `int`/`wlr_box`/`uint64_t` math.
- **QTimer not QElapsedTimer per tick**: single `QTimer` at `rate` Hz; `onTick` does `now = QDateTime::currentMSecsSinceEpoch()` once.
- **File I/O off hot path**: `fuzz.log.json` writes via buffered `QFile` + `QTextStream`; flushed only on anomaly, not every tick.

### 4.8 Skill for models — how to read/use the test + fuzz system (NEW)

Owner: “make a skill in here for models to know how to read and understand this, make it very testable and modifiable while it runs”.

Plan to add `.agents/skills/astick-test/SKILL.md` (or `.opencode/skills/astick-test/SKILL.md` — auto-detect repo’s skill location; `AGENTS.md` says `ai/superpowers/plans/` is used, so check existing `default.skill` layout at runtime and create in the first existing skill dir, fallback `.agents/skills/astick-test/`). Skill teaches an LLM how to:

- Build and run tests: `cmake -B build -DASTICK_TESTS=ON && cmake --build build -j$(nproc) && ./build/Astick --test` (or `build/AstickTest`), interpret `[TEST] RUNNING/PASS/FAIL` lines.
- Run stress: `./build/Astick -st --stress-seed 42 --stress-duration 30 --stress-rate 500 --stress-dump ./fuzz.log.json` (nested is fine; headless if no DRM).
- Read `fuzz.log.json` (JSONL): `jq -s 'group_by(.kind) | map({kind: .[0].kind, count: length})' fuzz.log.json`, `jq 'select(.kind=="invariant")' fuzz.log.json`, `jq .state.workspaces fuzz.log.json | head`.
- Mutate while running: `echo '{"rate":1000}' > /tmp/astick.stress.cmd` or `kill -USR1` to dump counters; edit `StressConfig` hot values.
- Understand determinism: same `--stress-seed` must produce identical `state` hashes; if not, it’s a bug.
- Know heap/cache rules: don’t add `new`/`malloc` in hot helpers; prefer `QVector::reserve`, `QVarLengthArray`, `wlr_box` by value.
- Extend tests: add a new `TestCase{ "my.feature", []{ ... } }` in `src/my_test.cpp` and register in `registerAllTests()`.

Skill file will include:

```
---
name: astick-test
description: Run Astick's in-binary test/stress system, read fuzz.log.json, mutate StressEngine live.
---

# Commands
- build: cmake -B build -DASTICK_TESTS=ON && cmake --build build
- unit: ./build/Astick --test  # or ./build/AstickTest --test
- stress: ./build/Astick -st --stress-seed 1 --stress-duration 30 --stress-dump ./fuzz.log.json
- fuzz log: jq -s . fuzz.log.json | less; jq 'select(.kind=="crash")' fuzz.log.json
- live mutate: echo '{"rate":500}' > /tmp/astick.stress.cmd

# Invariants to check after any change
- IDs deterministic, not title/size derived
- layout math helpers still logAndContinue, not crash
- no new heap per frame (use heaptrack/massif if怀疑)
...
```

The skill is intentionally small and points at the same `PLAN.md` invariants.

## 5. Phased Execution (order matters; each phase shippable, no CI phase)

**Phase 0 — Scaffolding (no behavior change, ~0.5 day)**
- Add/update `PLAN.md` (this file). Add `src/test_runner.h` stub, `src/stress.h` stub, `src/state_dump.h` stub — headers decl-only.
- Add `option(ASTICK_TESTS OFF)` in `CMakeLists.txt`; when ON, add `test_runner.cpp`/`stress.cpp`/`state_dump.cpp` to `SOURCES` (or new target `AstickTest`). Verify `cmake -B build -DASTICK_TESTS=ON && cmake --build build` still builds.

**Phase 1 — Deterministic IDs (isolated, ~0.5 day)**
- Move `Resource` bodies to `resource.cpp`, replace RNG with per-kind counters, add `resetForTests()`, persist `nextId` map.
- Add `src/resource_test.cpp` with `TestCase` structs (`resource.id.monotonic`, `resource.id.no_title_hash`) registered via `registerAllTests()`. Run via `./build/Astick --test`.

**Phase 2 — Log-and-continue helper (cross-cutting, ~0.5 day)**
- Add `logAndContinue` in `src/util.h`/`src/debug.h` decl + `src/util.cpp` impl. Wire `qInstallMessageHandler` in `src/main.cpp:14`.
- Replace ~10 bare `wlr_log(WLR_ERROR)` sites; covered by `TestCase` that asserts error path returns fallback.

**Phase 3 — Lean onto Qt + downgrade (mechanical, ~1 day)**
- Swap `std::filesystem` → `QStandardPaths`/`QDir` in `Config::defaultPath`/`load`/`save` (`src/config.cpp`).
- Swap `std::unordered_map` → `QHash`, `std::vector` → `QList` (keep `std::string` only at xkbcommon boundary).
- `CMakeLists.txt:4` `23` → `17`, `CMAKE_CXX_EXTENSIONS OFF`.

**Phase 4 — Header unification + no-logic-in-header (largest diff, low risk, ~2 days)**
- Create `src/surfaces.h`, `src/input.h`; merge `animation.h`+`animation_pool.h`; move `BspNode::makeLeaf/makeBranch`, `AnimationsConfig::*` helpers to `.cpp`.
- Keep `src/compositor.h` god header but now includes 3-4 unified headers.

**Phase 5 — .cpp helper decomposition (pattern-coupled, ~2 days)**
- Extract 4-8 `static` helpers per heavy function in anonymous namespaces (`resolve*` → `build*` → `commit*` → `persist*` → `wire*`).
- No API change. Add `src/detail/compositor_helpers.h` trick only under `ASTICK_ENABLE_TESTS` so helpers are reachable from `TestRunner` without polluting normal build.

**Phase 6 — State dump + StressEngine + fuzz.log.json + `--test` / `-st` wiring (~1.5 days)**
- Implement `src/state_dump.h`/`state_dump.cpp` (snapshot JSON).
- Implement `src/stress.h`/`src/stress.cpp` arena, `onTick`, `directlyMutateState`, `checkInvariantsAndLog`, `fuzz.log.json` JSONL writer, hot-mutate via `/tmp/astick.stress.cmd` + `SIGUSR1`.
- Wire CLI in `src/main.cpp:39-50`: `--test` runs `TestRunner::runAll()` then exit; `-st`/`--stress-test` constructs `StressEngine` after `Compositor comp(app,&config)` and starts it before `app.exec()`.
- Add `src/*_test.cpp` that exercise helpers + one `state_dump` golden test: `Astick --headless --dump-state - | jq`.
- Verify `./build/Astick --test` prints `RUNNING/PASS/FAIL` per test, continues after fail, exit code correct; `./build/Astick -st --stress-seed 1 --stress-duration 5 --stress-dump ./fuzz.log.json` creates `./fuzz.log.json` and does not crash on invariants.

**Phase 7 — Skill + docs (~0.5 day)**
- Create `.agents/skills/astick-test/SKILL.md` (or `.opencode/skills/...` depending on repo skill dir) with commands above, plus notes on heap/cache discipline.
- Update `AGENTS.md` to document `--test` / `-st` flags, `TestCase` struct, `fuzz.log.json` schema, unified header layout, C++17.

Estimated total: ~8.5 days solo, each phase revertible.

## 6. Risks & How to Keep It Stupidly Simple / Cache-Friendly

- **Header unification → longer recompiles.** Mitigated by keeping `.cpp` split; unity header only changes include graph.
- **Template bodies in .cpp → link errors.** Explicit instantiations (`template class AnimationInstance<int>;`) at bottom of `animation_pool.cpp`.
- **Deterministic IDs wrapping at 1000.** Log at `s_nextId % 1000 == 0`; session file stores high-water mark.
- **JSON dump volume.** Only on anomaly + heartbeat (every 5 s) or `--trace-state`; default stress does not spam.
- **StressEngine heap in hot path.** Use `QVector::reserve`/`QVarLengthArray` arena, no `new` per tick, `QString` only on failure, buffered `QFile` writes.
- **Fuzz determinism.** Same `--stress-seed` must hash to identical state dumps; `QRandomGenerator` seeded explicitly, not `securelySeeded` unless requested.
- **Skill location drift.** Probe `~/.agents/skills/`, `.agents/skills/`, `.opencode/skills/` at implementation time; create in first that exists, otherwise `.agents/skills/astick-test/`.

## 7. Open Questions (answer before Phase 1)

1. Persist `nextId` counters — `~/.config/Astick/session.json` next to `config.json` (`Config::defaultPath()` parent) or `~/.local/state/Astick/` (`QStandardPaths::AppStateLocation`)? Latter more XDG-correct.
2. `fuzz.log.json` default path — `./fuzz.log.json` in cwd or `~/.local/state/Astick/fuzz.log.json`?
3. Single binary `Astick --test` vs separate `AstickTest` target — single is simpler per owner’s “one binary in build/”, but separate keeps normal binary smaller. Preference?
4. Do we want `--trace-state` to also dump `rawJson` config or redacted? Redacted safer for sharing.

---
*This plan respects all owner constraints (god object, stupidly simple, no CI, log-and-continue, Qt-leaning, heap/cache discipline) and is intentionally not executed. Next step after approval: pick Phase 0 or Phase 1, or adjust this plan.*
