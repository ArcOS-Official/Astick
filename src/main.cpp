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
            fprintf(stderr, "Config not found at %s, using defaults\n", configPathOverride.toUtf8().data());
            config.loadOrCreateDefault();
        }
    } else {
        // Try root dir config.json first (for dev), then XDG
        std::filesystem::path rootConfig = std::filesystem::path(app.applicationDirPath().toStdString()) / "config.json";
        // Also try ./config.json from cwd and project root
        std::filesystem::path cwdConfig = std::filesystem::current_path() / "config.json";
        std::filesystem::path projConfig = std::filesystem::path("/home/kernelstate/data/personal/Astick/config.json");
        if (std::filesystem::exists(cwdConfig)) {
            loaded = config.load(cwdConfig);
            fprintf(stderr, "Loaded config from %s\n", cwdConfig.c_str());
        } else if (std::filesystem::exists(rootConfig)) {
            loaded = config.load(rootConfig);
            fprintf(stderr, "Loaded config from %s\n", rootConfig.c_str());
        } else if (std::filesystem::exists(projConfig)) {
            loaded = config.load(projConfig);
            fprintf(stderr, "Loaded config from %s\n", projConfig.c_str());
        } else {
            config.loadOrCreateDefault();
            fprintf(stderr, "Loaded config from %s\n", Config::defaultPath().c_str());
        }
    }

    Compositor comp(app, &config);
    comp.setInitialLayoutMode(mode);

    return app.exec();
}
