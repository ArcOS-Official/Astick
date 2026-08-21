/*
 *  Astick, the wayland compositor for ArcDE.
 *  Copyright (C) 2026 Eyad Ahmed Ragheb

 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.

 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.

 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "output.h"
#include <ctime>
#include "wlroots.h"
#include "util.h"

int frameTimerTick(void *data)
{
    Output *self = (Output *)data;
    if (self->output) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        double stall = (now.tv_sec - self->lastHostFrame.tv_sec) +
            (now.tv_nsec - self->lastHostFrame.tv_nsec) / 1e9;
        if (stall > self->frameInterval / 1000.0)
            self->renderFrame();
        wl_event_source_timer_update(self->frameTimer, self->frameInterval);
    }
    return 0;
}

void onFrame(struct wl_listener *listener, void *)
{
    Output *self = wl_container_of(listener, self, frameListener);
    clock_gettime(CLOCK_MONOTONIC, &self->lastHostFrame);
    self->renderFrame();
}

void Output::renderFrame()
{
    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(scene, output);
    if (scene_output) {
        bool needs_frame = wlr_scene_output_needs_frame(scene_output);
        wlr_scene_output_commit(scene_output, nullptr);
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        wlr_scene_output_send_frame_done(scene_output, &now);

        if (fpsFrames == 0) fpsTimer = now;
        fpsFrames++;
        if (needs_frame) fpsRendered++;
        double dt = (now.tv_sec - fpsTimer.tv_sec) +
            (now.tv_nsec - fpsTimer.tv_nsec) / 1e9;
        if (dt >= 1.0) {
            wlr_log(WLR_INFO, "Output FPS: %.1f frame events, %d rendered (%.1f%%)",
                fpsFrames / dt, fpsRendered,
                100.0 * fpsRendered / fpsFrames);
            fpsFrames = 0;
            fpsRendered = 0;
        }
    }
    emit frameReady();
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
    if (self->frameTimer) wl_event_source_remove(self->frameTimer);
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

    clock_gettime(CLOCK_MONOTONIC, &lastHostFrame);
    if (output->refresh > 0)
        frameInterval = std::max(1, (int)(1'000'000'000 / output->refresh / 1000));
    frameTimer = wl_event_loop_add_timer(output->event_loop, frameTimerTick, this);
    wl_event_source_timer_update(frameTimer, frameInterval);
    wlr_output_schedule_frame(output);

    signal(frameListener, &output->events.frame, onFrame);
    signal(requestStateListener, &output->events.request_state, onRequestState);
    signal(destroyListener, &output->events.destroy, onDestroy);
}

struct wlr_output *Output::get() const
{
    return output;
}

void Output::setWorkspace(int ws)
{
    if (ws == workspace || ws < 1) return;
    int old = workspace;
    workspace = ws;
    emit workspaceChanged(old, ws);
}
