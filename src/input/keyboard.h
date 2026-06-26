#pragma once

#include <qobject.h>
#include "../wlroots.h"

class Keyboard : public QObject {
    Q_OBJECT

public:
    Keyboard(struct wlr_input_device *device, struct wlr_seat *seat);
    ~Keyboard();
    struct wlr_keyboard *getKeyboard() const { return wlrKeyboard; }

    friend void keyboard_handle_modifiers(struct wl_listener *listener, void *data);
    friend void keyboard_handle_key(struct wl_listener *listener, void *data);
    friend void keyboard_handle_destroy(struct wl_listener *listener, void *data);

signals:
    void modifiersChanged();
    void keyPressed(struct wlr_keyboard_key_event *event);
    void destroyed();

private:
    struct wlr_keyboard *wlrKeyboard;
    struct wlr_seat *seat;
    struct wl_listener modifiersListener;
    struct wl_listener keyListener;
    struct wl_listener destroyListener;
};