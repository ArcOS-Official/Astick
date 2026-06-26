#include "mouse.h"
#include "../util.h"

struct MouseDestroyListener {
    struct wl_listener listener;
    Mouse *self;
};

static void mouse_handle_destroy(struct wl_listener *listener, void *)
{
    MouseDestroyListener *wrapper = wl_container_of(listener, wrapper, listener);
    Mouse *self = wrapper->self;
    wl_list_remove(&wrapper->listener.link);
    delete wrapper;
    emit self->destroyed();
    delete self;
}

Mouse::Mouse(struct wlr_input_device *device_)
{
    device = device_;
    auto *wrapper = new MouseDestroyListener;
    wrapper->self = this;
    wrapper->listener.notify = mouse_handle_destroy;
    wl_signal_add(&device->events.destroy, &wrapper->listener);
}

Mouse::~Mouse() {}