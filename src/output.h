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

#pragma once

#include <QObject>
#include <ctime>
#include "wlroots.h"

class Output : public QObject
{
    Q_OBJECT

public:
    Output(
        struct wlr_output *output_,
        struct wlr_renderer *renderer,
        struct wlr_allocator *allocator,
        struct wlr_scene *scene_
    );
    struct wlr_output *get() const;
    int getWorkspace() const { return workspace; }
    void setWorkspace(int ws);

    friend void onFrame(struct wl_listener *listener, void *data);
    friend void onRequestState(struct wl_listener *listener, void *data);
    friend void onDestroy(struct wl_listener *listener, void *data);

signals:
    void frameReady();
    void destroyed();
    void workspaceChanged(int oldWorkspace, int newWorkspace);

private:
    struct wlr_output *output;
    int workspace = 1;
    std::timespec lastFrame;
    struct wlr_scene *scene;
    struct wl_listener frameListener;
    struct wl_listener requestStateListener;
    struct wl_listener destroyListener;
};
