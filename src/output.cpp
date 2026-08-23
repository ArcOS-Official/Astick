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

void onFrame(struct wl_listener *listener, void *)
{
    Output *self = wl_container_of(listener, self, frameListener);
    self->renderFrame();
}

void Output::renderFrame()
{
    wlr_log(WLR_INFO, "Output::renderFrame output %p", (void*)output);
    struct wlr_scene_output *scene_output = wlr_scene_get_scene_output(scene, output);
    if (!scene_output) {
        wlr_log(WLR_INFO, "  no scene_output");
        return;
    }
    // Keep background opaque and sized to current output - fixes host wallpaper
    // bleed-through when running nested (Wayland backend) or when clients have
    // alpha/CSD shadows (e.g. Dolphin). Scene otherwise clears to transparent.
    if (background) {
        if (background->width != output->width || background->height != output->height) {
            wlr_scene_rect_set_size(background, output->width, output->height);
        }
    }
    // Only commit when scene actually has damage. The old frameTimer forced
    // commits off-vblank (tearing) and with no damage (flicker).
    if (!wlr_scene_output_needs_frame(scene_output)) {
        return;
    }
    if (!wlr_scene_output_commit(scene_output, nullptr)) {
        return;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    wlr_scene_output_send_frame_done(scene_output, &now);

    if (fpsFrames == 0) fpsTimer = now;
    fpsFrames++;
    fpsRendered++;
    double dt = (now.tv_sec - fpsTimer.tv_sec) +
        (now.tv_nsec - fpsTimer.tv_nsec) / 1e9;
    if (dt >= 1.0) {
        wlr_log(WLR_INFO, "Output FPS: %.1f (%.1f%% rendered)",
            fpsFrames / dt,
            100.0 * fpsRendered / fpsFrames);
        fpsFrames = 0;
        fpsRendered = 0;
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
    wl_list_remove(&self->frameListener.link);
    wl_list_remove(&self->requestStateListener.link);
    wl_list_remove(&self->destroyListener.link);
    emit self->destroyed();
    delete self;
}

uint64_t Output::genId() {
    return allocateId(ResourceKind::OutputBase);
}

Output::Output(
    struct wlr_output *output_,
    struct wlr_renderer *renderer,
    struct wlr_allocator *allocator,
    struct wlr_scene *scene_
)
{
    generateId();
    output = output_;
    scene = scene_;
    wlr_log(WLR_INFO, "initialized output of size %dx%d", output->width, output->height);
    wlr_output_init_render(output, allocator, renderer);

    // Opaque black background at the very bottom of the scene. Prevents
    // transparent clears (nested Wayland hosts wallpaper through) and
    // Dolphin's CSD shadow alpha from revealing the host.
    {
        const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        int w = output->width > 0 ? output->width : 1280;
        int h = output->height > 0 ? output->height : 720;
        background = wlr_scene_rect_create(&scene->tree, w, h, black);
        wlr_scene_node_lower_to_bottom(&background->node);
    }

    wlr_output_schedule_frame(output);

    signal(frameListener, &output->events.frame, onFrame);
    signal(requestStateListener, &output->events.request_state, onRequestState);
    signal(destroyListener, &output->events.destroy, onDestroy);
}

struct wlr_output *Output::get() const
{
    return output;
}

int Output::getWorkspace() const { return workspace; }

void Output::setWorkspace(int ws)
{
    if (ws == workspace || ws < 1) return;
    int old = workspace;
    workspace = ws;
    emit workspaceChanged(old, ws);
}
