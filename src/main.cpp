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

int main(int argc, char **argv) {
    Config::captureOriginalDisplay();
    wlr_log_init(WLR_DEBUG, nullptr);
    Astick app(argc, argv);

    QString mode = "tiling";
    QString configPath;
    for (int i = 1; i < argc; ++i) {
        QString arg = QString::fromUtf8(argv[i]);
        if (arg == "--help" || arg == "-h") {
            fprintf(stderr,
                "Usage: Astick [options]\n"
                "  --mode tiling|floating|monowindow\n"
                "  --config <path>\n");
            return 0;
        }
        if (arg == "--mode" && i + 1 < argc) mode = QString::fromUtf8(argv[++i]);
        else if (arg == "--config" && i + 1 < argc) configPath = QString::fromUtf8(argv[++i]);
    }

    Config config;
    if (!configPath.isEmpty()) {
        if (!config.load(configPath.toStdString())) {
            fprintf(stderr, "Config not found at %s, using defaults\n", configPath.toUtf8().data());
            config.loadOrCreateDefault();
        }
    } else {
        config.loadOrCreateDefault();
    }

    astick::State state;
    Engine engine(app, &config);
    engine.setState(&state);
    astick::InputManager input(state);
    astick::AnimationManager anim(state);
    astick::OutputManager outputs(state);
    state.addEventSource(&anim);
    state.addEventSource(&engine);
    state.addCommandReceiver(&engine);

    engine.setInitialLayoutMode(mode);

    return app.exec();
}
