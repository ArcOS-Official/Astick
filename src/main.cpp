#include "compositor.h"
#include "application.h"
#include <QString>
#include <cstdio>

#if !defined(__clang__) && !defined(__GNUC__)
#error "Unsupported compiler"
#endif

int main(int argc, char **argv) {
    Astick app(argc, argv);

    QString mode = "tiling";
    for (int i = 1; i < argc; i++) {
        QString arg = QString::fromUtf8(argv[i]);
        if (arg == "--help" || arg == "-h") {
            fprintf(stderr,
                "Usage: Astick [options]\n"
                "Wayland compositor for ArcDE.\n\n"
                "Options:\n"
                "  --help, -h          Show this help\n"
                "  --mode <mode>       Initial layout mode: tiling, floating, monowindow\n"
            );
            return 0;
        }
        if (arg == "--mode" && i + 1 < argc) {
            mode = QString::fromUtf8(argv[++i]);
        }
    }

    Compositor comp(app);
    comp.setInitialLayoutMode(mode);

    return app.exec();
}
