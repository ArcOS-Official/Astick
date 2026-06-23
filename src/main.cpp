#include "compositor.h"
#include "application.h"

/*
 * Even though we don't use specific compiler features here,
 * we will not support compiling with weird compilers that
 * don't work most of the time.
*/
#if !defined(__clang__) && !defined(__GNUC__)
#error "Unsupported compiler"
#endif

int main(int argc, char **argv) {
    Astick app(argc, argv);
    Compositor comp(app);
    return app.exec();
}
