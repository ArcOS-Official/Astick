#include "compositor.h"
#include "application.h"
#include "config.h"
#include "state/state.h"
#include "state/animation_manager.h"
#include "input/input_manager.h"
#include "output/output_manager.h"
#include "engine/engine.h"
#include "layout.h"
#include <QString>
#include <cstdio>
#include <filesystem>

#if !defined(__clang__) && !defined(__GNUC__)
#error "Unsupported compiler"
#endif

int main(int argc, char **argv) {
    Config::captureOriginalDisplay();
    wlr_log_init(WLR_DEBUG, NULL);
    Astick app(argc, argv);

    QString mode = "tiling";
    QString configPathOverride;
    bool useClassic = false; // --classic keeps old Compositor path (Phase 0 shim)
    for (int i = 1; i < argc; i++) {
        QString arg = QString::fromUtf8(argv[i]);
        if (arg == "--help" || arg == "-h") {
            fprintf(stderr,
                "Usage: Astick [options]\n"
                "Wayland compositor for ArcDE.\n\n"
                "Options:\n"
                "  --help, -h          Show this help\n"
                "  --mode <mode>       Initial layout mode: tiling, floating, monowindow\n"
                "  --config <path>     Path to config.json\n"
                "  --classic           Use legacy Compositor (wl_event_loop) instead of Engine\n"
            );
            return 0;
        }
        if (arg == "--mode" && i + 1 < argc) {
            mode = QString::fromUtf8(argv[++i]);
        } else if (arg == "--config" && i + 1 < argc) {
            configPathOverride = QString::fromUtf8(argv[++i]);
        } else if (arg == "--classic") {
            useClassic = true;
        }
    }

    Config config;
    bool loaded = false;
    if (!configPathOverride.isEmpty()) {
        loaded = config.load(configPathOverride.toStdString());
        if (!loaded) {
            fprintf(stderr, "Config not found at %s, using defaults at %s\n", configPathOverride.toUtf8().data(), Config::defaultPath().c_str());
            config.loadOrCreateDefault();
        }
    } else {
        config.loadOrCreateDefault();
        fprintf(stderr, "Loaded config from %s\n", Config::defaultPath().c_str());
    }

    if (useClassic) {
        // Legacy path — single binary still supports old Compositor for testing.
        Compositor comp(app, &config);
        comp.setInitialLayoutMode(mode);
        return app.exec();
    }

    // New component architecture — single thread, custom loop, no wl_event_loop.
    // Zero-copy: State hub owns buses, Engine owns wlroots memory (stale until hooked).
    astick::State state;
    LayoutManager layout; // now takes State* in future phase — here we keep classic but wiring via State
    // NOTE: LayoutManager will subscribe internally once Phase 2 lands:
    //   layout.attach(state); // does state.subscribe(this, Keyboard|Pointer|Window(id), cb)
    astick::InputManager input(state);  // owns KeyboardState+PointerState, subscribes in ctor
    astick::AnimationManager anim(state); // EventSource + Listener
    astick::OutputManager outMgr(state);
    state.addEventSource(&anim);

    // Engine owns all wlr_* (scene, backend, seat) and is stale until hookState.
    // main() builds consumers + State, then Engine, then run — per plan §1.
    astick::Engine engine(state, config);
    engine.init();
    engine.hookState();

    // Set initial layout mode via LayoutManager (zero-copy Box math stays in State domain)
    {
        QString m = mode.toLower().trimmed();
        LayoutManager::Mode lm = LayoutManager::Mode::Tiling;
        if (m == "floating") lm = LayoutManager::Mode::Floating;
        else if (m == "monowindow") lm = LayoutManager::Mode::MonoWindow;
        layout.setWorkspaceLayoutMode(1, lm);
    }

    // Custom loop: polls fds + timer, drains events via move, flushes commands — one thread.
    return engine.run();
}
