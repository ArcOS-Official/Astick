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

    friend void onFrame(struct wl_listener *listener, void *data);
    friend void onRequestState(struct wl_listener *listener, void *data);
    friend void onDestroy(struct wl_listener *listener, void *data);

signals:
    void frameReady();
    void destroyed();

private:
    struct wlr_output *output;
    std::timespec lastFrame;
    struct wlr_scene *scene;
    struct wl_listener frameListener;
    struct wl_listener requestStateListener;
    struct wl_listener destroyListener;
};