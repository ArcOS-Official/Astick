#include "compositor.h"
#include "application.h"

#if !defined(__clang__) && !defined(__GNUC__)
#error "Unsupported compiler"
#endif

int main(int argc, char **argv) {
    Astick app(argc, argv);
    Compositor comp(app);
    return app.exec();
}
