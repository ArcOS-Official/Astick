#include "output.h"
#include <ctime>
#include "wlroots.h"
#include "util.h"

void onFrame(struct wl_listener *listener, void *)
{
    Output *self = wl_container_of(listener, self, frameListener);
    struct wlr_scene *scene = self->scene;
    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(scene, self->output);
    if (scene_output) {
        wlr_scene_output_commit(scene_output, nullptr);
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        wlr_scene_output_send_frame_done(scene_output, &now);
    }
    emit self->frameReady();
}

void onRequestState(struct wl_listener *listener, void *data)
{
    Output *self = wl_container_of(listener, self, requestStateListener);
    const struct wlr_output_event_request_state *event =
        (const struct wlr_output_event_request_state *)data;
    wlr_output_commit_state(self->output, event->state);
}

void onDestroy(struct wl_listener *listener, void *)
{
    Output *self = wl_container_of(listener, self, destroyListener);
    wl_list_remove(&self->frameListener.link);
    wl_list_remove(&self->requestStateListener.link);
    wl_list_remove(&self->destroyListener.link);
    emit self->destroyed();
    delete self;
}

Output::Output(
    struct wlr_output *output_,
    struct wlr_renderer *renderer,
    struct wlr_allocator *allocator,
    struct wlr_scene *scene_
)
{
    output = output_;
    scene = scene_;
    wlr_log(WLR_INFO, "initialized output of size %dx%d", output->width, output->height);
    wlr_output_init_render(output, allocator, renderer);
    wlr_output_schedule_frame(output);

    signal(frameListener, &output->events.frame, onFrame);
    signal(requestStateListener, &output->events.request_state, onRequestState);
    signal(destroyListener, &output->events.destroy, onDestroy);
}

struct wlr_output *Output::get() const
{
    return output;
}