---
name: astick-test
description: Use when working on Astick compositor to build or run in-binary tests, stress/fuzz the compositor, read fuzz.log.json, or extend the test suite
---

# Astick Test

In-binary test + stress/fuzz system compiled into `build/Astick`. No external harness, no CI.

## Commands

### Build

```sh
cmake -B build -DASTICK_TESTS=ON
cmake --build build -j$(nproc)   # binary: build/Astick
```

`ASTICK_TESTS=ON` defines `ASTICK_ENABLE_TESTS` and compiles `src/test_runner.cpp` + `src/*_test.cpp` + `src/stress.cpp` + `src/state_dump.cpp` into the same binary. `CMAKE_CXX_STANDARD` is 17 (`CMakeLists.txt:4`).

Without the flag the binary still builds but `--test` / `-st` are no-ops.

### Unit tests

```sh
./build/Astick --test
```

Output protocol (parsed by humans and scripts):

```
[TEST] RUNNING resource.id.monotonic
[TEST] PASS   resource.id.monotonic (0 ms)
[TEST] RUNNING config.outputId.sanitize
[TEST] FAIL   config.outputId.sanitize (1 ms) — got "a b" expected "a_b"
[TEST] SUMMARY 42 ran, 41 passed, 1 failed
```

- Runner is `src/test_runner.h:6` / `src/test_runner.cpp:23` — `TestRunner::runAll()` continues after failure, collects `TestResult`.
- Exit code 0 if all pass, 1 if any fail.
- Registration via `registerAllTests()` (`src/test_runner.cpp:79`) which calls weak `register*Tests()` symbols. Each `src/*_test.cpp` defines one.

List tests without running: `grep -rn 'TestCase{' src/*_test.cpp` or `grep -rn 'register.*Tests' src/`.

### Stress / fuzz

```sh
# Normal compositor + concurrent stress engine (needs Wayland backend / nested)
./build/Astick -st --stress-seed 1 --stress-duration 30 --stress-rate 200 --stress-dump ./fuzz.log.json

# Short deterministic headless run (no DRM) — dumps to fuzz log + /tmp/astick_state.json
./build/Astick --headless -st --stress-seed 42 --stress-duration 5 --stress-rate 100 --stress-dump ./fuzz.log.json

# Trace every tick/anomaly verbosely
./build/Astick -st --trace-state --stress-dump ./fuzz.log.json
```

Flags (`src/main.cpp:69-84`):

| Flag | Default | Meaning |
|---|---|---|
| `--test` | off | Run unit tests and exit |
| `-st`, `--stress-test` | off | Spawn `StressEngine` alongside compositor |
| `--stress-seed <n>` | 1 | Deterministic `QRandomGenerator` seed |
| `--stress-duration <s>` | 30 | Seconds; 0 = forever |
| `--stress-rate <hz>` | 200 | `QTimer` ticks / fake events per second |
| `--stress-dump <path>` | `./fuzz.log.json` | Append-only JSONL fuzz log |
| `--headless`, `--dry-run` | off | Virtual 1280x720, one `arrangeForOutput` cycle, dump `StateDumper::snapshot()` and exit |
| `--trace-state` | off | Write heartbeat snapshot every 1 s + print anomalies to stderr |

Headless + stress is synchronous (`src/main.cpp:150-265`): caps at 2000 ticks, writes `kind: periodic` / `kind: final` snapshots without needing `wl_display_run`.

### Fuzz log reading

`fuzz.log.json` is JSON Lines — one compact JSON object per line, append-only via `QFile` + `flush()` (`src/stress.cpp:40`, `src/main.cpp:333`).

```sh
# Pretty-print (needs jq)
jq -s . fuzz.log.json | less
jq 'select(.kind=="invariant")' fuzz.log.json
jq 'select(.kind=="crash")' fuzz.log.json
jq -s 'group_by(.kind) | map({kind: .[0].kind, count: length})' fuzz.log.json
jq .state.workspaces fuzz.log.json | head -n 80
jq -s 'map(select(.seed==1)) | length' fuzz.log.json

# Count anomalies vs periodic heartbeats
grep -c '"kind":"invariant"' fuzz.log.json
grep -c '"kind":"final"' fuzz.log.json
```

Also `fuzz.log.jsonl` symlink is not auto-created — treat `fuzz.log.json` as the canonical JSONL file even though the extension is `.json`.

### Live mutate

While `-st` is running:

```sh
# Hot-patch tick rate (polled every 1s via QTimer + QFileSystemWatcher on /tmp)
echo '{"rate":500}' > /tmp/astick.stress.cmd
# or bare integer
echo 1000 > /tmp/astick.stress.cmd

# Dump counters to stderr without stopping
kill -USR1 $(pidof Astick)
# prints: [STRESS] SIGUSR1 counters ticks=... fakeClients=... moves=... resizes=... wsSwitches=... anomalies=...
```

Poller is `src/main.cpp:286-319`; signal handler is `src/main.cpp:38`. The file is removed after being consumed.

### State dump

`src/state_dump.h:10` / `src/state_dump.cpp`:

- `StateDumper::snapshot(Compositor*)` builds the canonical JSON object (outputs, workspaces, BSP nodes, windows, animations, config modkey).
- `StateDumper::hashState(obj)` hashes the snapshot for determinism checks.
- In headless mode the snapshot is also written to `/tmp/astick_state.json` (indented) for golden-file diffing.

## Invariants to check after any change

1. **IDs deterministic** — `Resource::allocateId()` per-kind monotonic counters (`src/resource.h:37-47`), not title/size hashed. `Resource::resetForTests()` must zero counters; same `--stress-seed` must hash to identical `StateDumper::snapshot()` (`StateDumper::hashState`).
2. **Layout helpers log-and-continue** — `wlr_box` splits / `ratio` clamping via `logAndContinue` (`src/util.h` / `src/debug.h`), never crash on NaN/negative/ out-of-range. `ratio` must stay in `[minRatio, maxRatio]`.
3. **No crash on empty/degenerate BSP** — zero/one window per workspace, empty trees, extreme cursor coords must not segfault (caught as `kind: crash` in fuzz log).
4. **Geometry invariant** — every leaf `box` within `usable` + gaps, no negative `w`/`h`, `id` unique and in kind range (`ResourceKind::WindowBase` etc.).
5. **No new heap per frame in hot path** — stress `onTick` must not `new`/`malloc`. Check with `heaptrack`/`massif` if suspicious.

## How to extend tests

Tests are plain structs (`src/test_runner.h:6`):

```cpp
struct TestCase {
    QString name;                                   // e.g. "layout.bsp.splitBoxHorizontally"
    std::function<bool(QString *outDetail)> run;    // return true=pass; write reason to *outDetail on fail
    bool enabled = true;
};
```

1. Create or edit `src/<area>_test.cpp` (e.g. `src/layout_test.cpp`):
   ```cpp
   #include "test_runner.h"
   #include "layout.h"

   void registerLayoutTests() {
       TestRunner::instance().add(TestCase{
           QStringLiteral("layout.bsp.splitBoxHorizontally"),
           [](QString *out) -> bool {
               wlr_box usable{0,0,1920,1080};
               // ... exercise helper, return false + *out on mismatch
               return true;
           }
       });
   }
   ```
2. Declare the registrar in `src/test_runner.cpp:72` if new area (weak symbol, add `__attribute__((weak)) void registerMyTests() {}` and call it in `registerAllTests()`).
3. Under `ASTICK_ENABLE_TESTS`, you may expose anonymous-namespace helpers via a `detail` namespace or `src/detail/compositor_helpers.h` include guarded by `#ifdef ASTICK_ENABLE_TESTS` — never in normal builds.
4. Build and run: `cmake -B build -DASTICK_TESTS=ON && cmake --build build -j$(nproc) && ./build/Astick --test`
5. Add a stress invariant if the bug is state-dependent: extend `StressEngine::checkInvariantsAndLog()` (`src/stress.cpp:39`) and verify it emits `kind: invariant` to `fuzz.log.json`.

Naming convention: `area.subsystem.case` (e.g. `resource.id.no_title_hash`, `config.outputId.sanitize`, `bsp.ratio.out_of_range`).

## Heap / cache discipline

Owner constraint: decrease heap allocations/accesses, stay cache-friendly, no regressions.

- **Arena for fakes**: `StressEngine::fakeClients` is `QVector<FakeClient> fakeClients` with `reserve(256)` upfront (`src/stress.h:51`), reused via indices — no `new Toplevel` per tick. If real `Toplevel` stubs are needed, use `QVarLengthArray` or `std::array<FakeToplevel,256>` free-list.
- **No QString in hot tick**: `onTick` uses `int`/`wlr_box`/`uint64_t`; `QString`/`QJsonObject detail` only on anomaly.
- **Single QTimer**: one `QTimer` at `rateHz`; `now = QDateTime::currentMSecsSinceEpoch()` once per tick.
- **File I/O off hot path**: `QFile` + `QTextStream` buffered, `flush()` only on anomaly (or every 5 s heartbeat / `--trace-state`).
- **Helpers take `const wlr_box&` by ref, return `wlr_box` by value** — no heap, inline-friendly.
- **Test `TestCase::run` is `std::function`** — okay outside frame loop; stress engine avoids it.

## Fuzz log schema

Each line is a self-contained JSON object. Fields:

```json
{
  "t": 1714051200.123,
  "tick": 421,
  "seed": 1,
  "kind": "invariant | crash | periodic | final | stress_start",
  "name": "bsp.ratio.out_of_range",
  "detail": {"node": 42, "ratio": 1.7, "expected": "[0.1,0.9]"},
  "signal": "SIGSEGV",
  "stack": "...",
  "state": {
    "t": 12345, "frame": 42, "seed": 1, "headless": false,
    "compositor": {"outputs": 1, "toplevels": 5, "popups": 0, "layers": 2},
    "outputs": [{"id": 2001, "name": "HDMI-A-1", "box": {"x":0,"y":0,"w":1920,"h":1080}, "scale": 1.0, "workspace": 1}],
    "workspaces": [{"id": 1, "mode": "Tiling", "bsp": [{"id": 101, "type": "leaf", "box": {...}, "toplevel": 1}], "floating": [], "fullscreen": null}],
    "windows": [{"id": 1, "title": "foot", "box": {"x":0,"y":0,"w":960,"h":1080}, "mapped": true, "floating": false}],
    "animations": [{"target": 1, "kind": "tilingMove", "progress": 0.3}],
    "config": {"modkey": "Alt", "bsp": {"split_ratio": 0.5}}
  },
  "counters": {"ticks": 421, "fakeClients": 3, "moves": 10, "resizes": 8, "workspaceSwitches": 2, "anomalies": 1}
}
```

- `t` — wall time seconds (double); `tick` — `StressEngine` tick counter; `seed` — `--stress-seed`.
- `kind: invariant` — invariant violation; `kind: crash` — SIGSEGV/abort via `qInstallMessageHandler` + `std::set_terminate`; `kind: periodic`/`final`/`stress_start` — heartbeats.
- `state` — `StateDumper::snapshot()` reuse; ring buffer of last 100 states kept in memory so crash can still dump.
- `counters` — `StressEngine::Counters`.

## Determinism

Same `--stress-seed` must produce identical `state` hashes. `StressEngine` seeds `QRandomGenerator` explicitly (`src/stress.h:45`, `src/stress.cpp`), never `securelySeeded()` unless requested. Verify:

```sh
./build/Astick --headless -st --stress-seed 1 --stress-duration 2 --stress-dump /tmp/a.json
cp /tmp/a.json /tmp/a1.json
./build/Astick --headless -st --stress-seed 1 --stress-duration 2 --stress-dump /tmp/b.json
diff /tmp/a1.json /tmp/b.json && echo "deterministic" || echo "BUG: non-deterministic"
# or: jq -s 'map(.state | tojson) | unique | length' -> 1 per unique state slice
```

If hashes diverge, it is a bug (often `QHash` iteration order or unseeded RNG).

## Unified header layout (post-Phase 4)

`16 -> 7-8` headers, decl-only (PLAN 4.1):

- `src/resource.h` — `Resource` + `ResourceKind` decls only; bodies in `resource.cpp`
- `src/surfaces.h` — merges `toplevel.h` + `popup.h` + `layersurface.h` + `output.h` + `cursor.h`
- `src/input.h` — merges `input/keyboard.h` + `input/mouse.h`
- `src/layout.h` — `LayoutManager` + `BspNode` decls; `makeLeaf`/`makeBranch`/`opposite()` moved to `layout.cpp`
- `src/config.h` — structs + `Config`; `AnimationsConfig::pairFor` etc. moved to `config.cpp`
- `src/animation.h` — merges `animation.h` + `animation_pool.h`; template `addInstance<T>` bodies in `animation_pool.cpp` with explicit instantiations
- `src/util.h` + `src/debug.h` + `src/decoration.h` — decl-only; `signal` macro still in `util.h`

Rule: `grep -n "{" src/*.h | grep -v "class\|struct\|enum"` should find almost nothing except `Q_OBJECT`.

## Common mistakes

- Forgetting `-DASTICK_TESTS=ON` — tests and stress still compile but `--test` prints `SUMMARY 0 ran`.
- Using `std::unordered_map`/`std::filesystem` in new code — prefer `QHash`/`QDir`/`QStandardPaths` (Qt-leaning, keeps `CMAKE_CXX_STANDARD 17`).
- Adding `new` in `StressEngine::onTick` or layout `arrange` — breaks heap discipline; use `reserve` + arena.
- Writing fuzz log as single giant JSON — must be JSONL (one object per line) so crash does not corrupt prior lines.
- Hashing title/size into `Resource` id — forbidden; use per-kind counters only.
