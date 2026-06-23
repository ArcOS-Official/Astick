#include "output.h"
#include <ctime>
#include "wlroots.h"
#include "util.h"

void onFrame(struct wl_listener *listener, void *)
{
    wlr_log(WLR_INFO, "rendering frame");
    Output *self = wl_container_of(listener, self, onFrameListener);
    //struct wlr_scene_output *output =
    //    wlr_scene_get_scene_output(self->scene, self->output);
    //wlr_scene_output_commit(output, {});
    //clock_gettime(CLOCK_MONOTONIC, &self->lastFrame);
    struct wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_render_pass *pass = wlr_output_begin_render_pass(
        self->output,
        &state,
        {}
    );
    const struct wlr_render_rect_options opts = {
        .box = {
            .x = 0,
            .y = 0,
            .width = self->output->width,
            .height = self->output->height
        },
        .color = { .r = 255, .g = 0, .b = 0, .a = 1.0f },
        .clip = {},
        .blend_mode = {},
    };
    wlr_render_pass_add_rect(pass, &opts);
    if (!wlr_render_pass_submit(pass)) {
        throw std::runtime_error("Failed to get render pass");
    }
    std::timespec_get(&self->lastFrame, TIME_UTC);
}

void onDestroy(struct wl_listener *listener, void *)
{
    Output *self = wl_container_of(listener, self, onDestroyListener);
}

Output::Output(
    struct wlr_output *output_,
    struct wlr_renderer *renderer,
    struct wlr_allocator *allocator,
    struct wlr_scene *scene_
)
{
    output = output_;
    wlr_log(WLR_INFO, "intialized output of size %dx%d", output->width, output->height);
    wlr_output_init_render(output, allocator, renderer);
    wlr_output_schedule_frame(output);
    scene = scene_;
    signal(onFrameListener, &output->events.frame, &onFrame);
    signal(onFrameListener, &output->events.needs_frame, &onFrame);
    signal(onFrameListener, &output->events.damage, &onFrame);
    signal(onDestroyListener, &output->events.destroy, &onDestroy);
}

wlr_output *Output::get() const
{
    return output;
}
