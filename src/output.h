#pragma once

#include <QObject>
#include <ctime>
#include "wlroots.h"

class Output : public QObject
{
    Q_OBJECT

public:
    explicit Output(struct wlr_output *output_);
    wlr_output *get() const;
    friend void onFrame(struct wl_listener *listener, void *_);
    friend void onDestroy(struct wl_listener *listener, void *_);
signals:
    void beforeDestroy();
    void frame();

private:
    struct wlr_output *output;
    std::timespec lastFrame;
    struct wl_listener onDestroyListener;
    struct wl_listener onFrameListener;
    struct wlr_scene *scene;
};

void OutputFrame(Output *output);
