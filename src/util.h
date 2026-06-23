#pragma once
#include "wlroots.h"

#define signal(name, sig, callback) \
    name = signal_(callback); \
    wl_signal_add(sig, &name);
struct wl_listener signal_(
    void (*callback)(struct wl_listener *, void *)
);
