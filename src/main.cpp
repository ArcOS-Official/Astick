#include "compositor.h"
#include "application.h"
#include "config.h"
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
            );
            return 0;
        }
        if (arg == "--mode" && i + 1 < argc) {
            mode = QString::fromUtf8(argv[++i]);
        } else if (arg == "--config" && i + 1 < argc) {
            configPathOverride = QString::fromUtf8(argv[++i]);
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

    Compositor comp(app, &config);
    comp.setInitialLayoutMode(mode);

    return app.exec();
}
