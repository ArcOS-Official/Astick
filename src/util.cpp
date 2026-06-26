#include "util.h"

struct wl_listener signal_(
    void (*callback)(struct wl_listener *, void *)
)
{
    struct wl_listener ret;
    ret.notify = callback;
    return ret;
}