# AGENTS.md — Astick

Wayland compositor for ArcDE. C++17 + Qt6 Core/Gui + wlroots 0.19. Single CMake binary with in-binary test/stress.

## Build

```sh
cmake -B build
cmake --build build          # binary: build/Astick
# or: make -C build -j$(nproc)

# With in-binary tests/stress/fuzz:
cmake -B build -DASTICK_TESTS=ON
cmake --build build -j$(nproc)   # defines ASTICK_ENABLE_TESTS
```

- `CMAKE_BUILD_TYPE` is hardcoded to `Debug` in `CMakeLists.txt:7`; override with `-DCMAKE_BUILD_TYPE=Release` if needed.
- `CMAKE_CXX_STANDARD` is 17 (`CMakeLists.txt:4`, `CMAKE_CXX_EXTENSIONS OFF`). Do not bump to 23 — prefer Qt (`QString`/`QList`/`QHash`/`QDir`/`QStandardPaths`) over `std::filesystem`/`std::unordered_map`/`std::string` (keep `std::string` only at wlroots/xkbcommon boundary).
- Requires: `Qt6::Core`, `Qt6::Gui`, `wlroots-0.19`, `wayland-server`, `wayland-client`, `xkbcommon`, `libinput`, `nlohmann_json` (header-only fallback if CMake package missing), `pixman-1`, `libdrm`.
- Flags set by CMake: `-DWLR_USE_UNSTABLE -Wno-vla`, `CMAKE_AUTOMOC ON`.
- Out-of-source build dir `build/` is gitignored. `compile_commands.json` is duplicated at repo root and `build/` — keep in sync after reconfiguring.
- No lint/format/typecheck config in repo. No CI — verification is local via `--test` / `-st` in `build/Astick`.

## Run

```sh
./build/Astick --help
./build/Astick --mode tiling|floating|monowindow --config /path/to/config.json
```

- Config resolution in `src/main.cpp:39-64`: `--config` arg > `~/.config/Astick/config.json` (`Config::defaultPath()` respects `XDG_CONFIG_HOME`). On first run, embedded `config.template.json` (stripped of device-specific `outputs.monitors`) is written to that path. Bare `off` disables an animation (zero-duration), bare `default` restores code default via pre-tokenizer; invalid configs are renamed to `config.json.old` and regenerated with full error log. No local-dir (`./config.json` / `<appDir>/config.json` / hardcoded dev path) lookup.
- Sample config at `config.json` (root) and stripped template at `config.template.json` (source for embedded `src/embedded_config.h`). `Config::save()` preserves unknown JSON fields via `rawJson`.
- Keybinds use `Mod` placeholder resolved via `mod` (`Config::modkey`, default `"Alt"`, top-level `mod`/`modkey` or `input.mod`). In windowed/nested mode (`WAYLAND_DISPLAY`/`DISPLAY` present at launch, `Config::isWindowedMode()`), `Super` as mod falls back to `Alt` to avoid parent compositor conflict (`Config::effectiveMod()`).
- Plans: `ai/superpowers/plans/` (renamed from `docs/superpowers/plans/` on 2026-08-22 — `docs/` is deprecated, use `ai/`).
- Must run inside a Wayland-capable environment (wlroots backend); cannot be unit-tested headless without a nested backend (except `--headless`/`--dry-run` which does a virtual 1280x720 arrange + `StateDumper::snapshot()` without DRM).

## Tests (`--test`)

In-binary runner in `src/test_runner.h:6` / `src/test_runner.cpp:23` (`ASTICK_ENABLE_TESTS` only when `cmake -B build -DASTICK_TESTS=ON`).

```sh
cmake -B build -DASTICK_TESTS=ON && cmake --build build -j$(nproc)
./build/Astick --test
# [TEST] RUNNING resource.id.monotonic
# [TEST] PASS   resource.id.monotonic (0 ms)
# [TEST] SUMMARY 3 ran, 3 passed, 0 failed
# exit 0 if all pass, 1 if any fail
```

- `TestCase` struct (`src/test_runner.h:6`):
  ```cpp
  struct TestCase {
      QString name;                                   // e.g. "layout.bsp.splitBoxHorizontally"
      std::function<bool(QString *outDetail)> run;    // true=pass, false=fail; write detail on fail
      bool enabled = true;
  };
  ```
- `TestRunner::runAll()` continues after failure, collects `TestResult` in pre-reserved `QList`. Registry via `registerAllTests()` (`src/test_runner.cpp:79`) calling weak `register*Tests()` per `src/*_test.cpp`.
- To extend: add `TestCase{"my.feature", [](QString *out){ ... }}` in `src/my_test.cpp`, register in `registerAllTests()`. Naming: `area.subsystem.case`. See skill `astick-test` (`.agents/skills/astick-test/SKILL.md`).
- `src/resource.h:37-47` uses per-kind monotonic counters (`ResourceKind::WindowBase` etc.), not title/size hashing. `Resource::resetForTests()` zeros counters for deterministic runs.

## Stress / fuzz (`-st`)

`StressEngine` (`src/stress.h:18` / `src/stress.cpp`) pokes the live compositor; `fuzz.log.json` is JSONL (one object per line, append-only via `QFile` + `flush()`).

```sh
./build/Astick -st --stress-seed 1 --stress-duration 30 --stress-rate 200 --stress-dump ./fuzz.log.json
./build/Astick --headless -st --stress-seed 42 --stress-duration 5 --stress-rate 100 --stress-dump ./fuzz.log.json
./build/Astick -st --trace-state --stress-dump ./fuzz.log.json   # heartbeat every 1s + anomaly stderr
```

Flags: `--test`, `-st`/`--stress-test`, `--stress-seed <n>` (1), `--stress-duration <s>` (30, 0=forever), `--stress-rate <hz>` (200), `--stress-dump <path>` (`./fuzz.log.json`), `--headless`/`--dry-run`, `--trace-state` (`src/main.cpp:69-84`). Headless + stress is synchronous (caps 2000 ticks, writes `kind: periodic/final` without `wl_display_run`, `src/main.cpp:150-265`).

Fuzz log schema (each line self-contained):
```json
{"t": 1714051200.123, "tick": 421, "seed": 1, "kind": "invariant", "name": "bsp.ratio.out_of_range", "detail": {"node": 42, "ratio": 1.7}, "state": {"outputs": [...], "workspaces": [...], "windows": [...]}, "counters": {"ticks": 421, "anomalies": 1}}
{"t": 1714051201.001, "tick": 600, "kind": "crash", "signal": "SIGSEGV", "stack": "...", "state": {...}}
```
`kind` is `invariant`|`crash`|`periodic`|`final`|`stress_start`. `state` is `StateDumper::snapshot()` (`src/state_dump.h:10`). Read with:
```sh
jq 'select(.kind=="invariant")' fuzz.log.json
jq -s 'group_by(.kind) | map({kind: .[0].kind, count: length})' fuzz.log.json
```

Live mutate while `-st` runs: `echo '{"rate":500}' > /tmp/astick.stress.cmd` (polled 1s, `src/main.cpp:286`) or `echo 1000 > /tmp/...`; `kill -USR1 $(pidof Astick)` dumps `Counters` (`src/main.cpp:38`). Same `--stress-seed` must hash to identical `StateDumper::hashState()` — non-determinism is a bug.

Heap/cache discipline: `StressEngine::fakeClients` is `QVector<FakeClient>` `reserve(256)` arena (`src/stress.h:51`), no `new` per `onTick`; `QString`/`QJsonObject` only on anomaly; single `QTimer` at `rateHz`; buffered `QFile` flush only on anomaly/heartbeat. Hot helpers take `const wlr_box&` and return `wlr_box` by value.

## Architecture

- Entrypoint: `src/main.cpp` → `src/application.h` (`Astick : QCoreApplication`) → `src/compositor.h` (`Compositor : QObject`, central wiring).
- Core objects: `Compositor` owns `wlr_backend`, `wlr_renderer`, `wlr_allocator`, `wlr_scene`, `wlr_output_layout`, `xdgShell`/`layerShell`, `wlr_seat`/`wlr_cursor`, and lists of `Output`/`Toplevel`/`Popup`/`LayerSurface`/`Keyboard`/`Mouse`.
- Layout: `src/layout.h` (`LayoutManager`) — 3 modes `Tiling`/`Floating`/`MonoWindow`, workspace-based. `Compositor::rearrangeTiled()` / `arrangeForOutput()` delegate to it.
- Input split: `src/input/keyboard.*`, `src/input/mouse.*` (Qt `QObject` with `Q_OBJECT`/`AUTOMOC`).
- Surfaces: `src/toplevel.*`, `src/popup.*`, `src/layersurface.*`, `src/output.*`, `src/cursor.*`.
- Config: `src/config.*` — output/monitor matching by persistent ID (`Config::outputId()`), DPI/scale logic, keybind parsing (`xkbcommon`).
- Testing: `src/test_runner.h` (`TestRunner`), `src/stress.h` (`StressEngine`), `src/state_dump.h` (`StateDumper::snapshot()`/`hashState()`).
- `src/wlroots.h:41-68` is the sole wlroots include shim: wraps C headers in `extern "C"` and hacks `#define static` / `#define namespace namespace_` to make wlroots compile as C++. Do not include wlroots headers elsewhere.
- `src/util.h` provides `signal` macro wrapping `wl_signal_add` via `signal_()` (`src/util.cpp`).
- `protocols/` contains pre-generated Wayland protocol headers (e.g. `xdg-shell`, `wlr-layer-shell`). Included via `target_include_directories(... protocols)` in `CMakeLists.txt:58`. Do not hand-edit; regenerate from XML if needed.

## Conventions / Gotchas

- CMake adds all sources explicitly in `CMakeLists.txt:27-42` — add new `.cpp` files there.
- `main.cpp` now uses XDG-only lookup; no hardcoded dev path remains.
- No existing `AGENTS.md`/`CLAUDE.md`/`opencode.json` — this file is the only agent instruction source.
- Keep `build/` out of commits (`.gitignore`).
- Headers are decl-only (PLAN 4.1): `src/surfaces.h` merges `toplevel`+`popup`+`layersurface`+`output`+`cursor`; `src/input.h` merges `keyboard`+`mouse`; `src/animation.h` merges `animation_pool.h`. Add new template/inline logic in `.cpp` (explicit instantiation in `animation_pool.cpp`), not in headers.
- Skill for test/stress: `.agents/skills/astick-test/SKILL.md` — build/run `--test`/`-st`, read `fuzz.log.json`, live-mutate, determinism, heap discipline.
