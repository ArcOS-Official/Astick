# AGENTS.md — Astick

Wayland compositor for ArcDE. C++23 + Qt6 Core + wlroots 0.19. Single CMake binary, no tests/CI.

## Build

```sh
cmake -B build
cmake --build build          # binary: build/Astick
# or: make -C build -j$(nproc)
```

- `CMAKE_BUILD_TYPE` is hardcoded to `Debug` in `CMakeLists.txt:7`; override with `-DCMAKE_BUILD_TYPE=Release` if needed.
- Requires: `Qt6::Core`, `wlroots-0.19`, `wayland-server`, `wayland-client`, `xkbcommon`, `libinput`, `nlohmann_json` (header-only fallback if CMake package missing), `pixman-1`, `libdrm`.
- Flags set by CMake: `-DWLR_USE_UNSTABLE -Wno-vla`, `CMAKE_AUTOMOC ON`, `CMAKE_CXX_STANDARD 23`.
- Out-of-source build dir `build/` is gitignored. `compile_commands.json` is duplicated at repo root and `build/` — keep in sync after reconfiguring.
- No lint/format/test/typecheck config in repo.

## Run

```sh
./build/Astick --help
./build/Astick --mode tiling|floating|monowindow --config /path/to/config.json
```

- Config resolution in `src/main.cpp:39-64`: `--config` arg > `./config.json` (cwd) > `<appDir>/config.json` > hardcoded `/home/kernelstate/data/personal/Astick/config.json` (dev leftover — do not rely on) > `~/.config/Astick/config.json` (`Config::defaultPath()` respects `XDG_CONFIG_HOME`).
- Sample config at `config.json` (root). `Config::save()` preserves unknown JSON fields via `rawJson`.
- Must run inside a Wayland-capable environment (wlroots backend); cannot be unit-tested headless without a nested backend.

## Architecture

- Entrypoint: `src/main.cpp` → `src/application.h` (`Astick : QCoreApplication`) → `src/compositor.h` (`Compositor : QObject`, central wiring).
- Core objects: `Compositor` owns `wlr_backend`, `wlr_renderer`, `wlr_allocator`, `wlr_scene`, `wlr_output_layout`, `xdgShell`/`layerShell`, `wlr_seat`/`wlr_cursor`, and lists of `Output`/`Toplevel`/`Popup`/`LayerSurface`/`Keyboard`/`Mouse`.
- Layout: `src/layout.h` (`LayoutManager`) — 3 modes `Tiling`/`Floating`/`MonoWindow`, workspace-based. `Compositor::rearrangeTiled()` / `arrangeForOutput()` delegate to it.
- Input split: `src/input/keyboard.*`, `src/input/mouse.*` (Qt `QObject` with `Q_OBJECT`/`AUTOMOC`).
- Surfaces: `src/toplevel.*`, `src/popup.*`, `src/layersurface.*`, `src/output.*`, `src/cursor.*`.
- Config: `src/config.*` — output/monitor matching by persistent ID (`Config::outputId()`), DPI/scale logic, keybind parsing (`xkbcommon`).
- `src/wlroots.h:41-68` is the sole wlroots include shim: wraps C headers in `extern "C"` and hacks `#define static` / `#define namespace namespace_` to make wlroots compile as C++. Do not include wlroots headers elsewhere.
- `src/util.h` provides `signal` macro wrapping `wl_signal_add` via `signal_()` (`src/util.cpp`).
- `protocols/` contains pre-generated Wayland protocol headers (e.g. `xdg-shell`, `wlr-layer-shell`). Included via `target_include_directories(... protocols)` in `CMakeLists.txt:58`. Do not hand-edit; regenerate from XML if needed.

## Conventions / Gotchas

- CMake adds all sources explicitly in `CMakeLists.txt:27-42` — add new `.cpp` files there.
- `main.cpp:51` hardcodes an absolute developer path; fix or remove before distributing.
- No existing `AGENTS.md`/`CLAUDE.md`/`opencode.json` — this file is the only agent instruction source.
- Keep `build/` out of commits (`.gitignore`).
