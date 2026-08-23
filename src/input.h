#pragma once

#include "resource.h"
#include "wlroots.h"

struct KeyboardConfig;
struct MouseConfig;

class Keyboard : public Resource {
    Q_OBJECT
public:
    uint64_t genId() override;
    Keyboard(struct wlr_input_device *device, struct wlr_seat *seat);
    ~Keyboard();
    struct wlr_keyboard *getKeyboard() const;
    void applyConfig(const KeyboardConfig &cfg);
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

struct MouseConfig;
class Mouse : public Resource {
    Q_OBJECT
public:
    uint64_t genId() override;
    Mouse(struct wlr_input_device *device);
    ~Mouse();
    void applyConfig(const MouseConfig &cfg);
signals:
    void destroyed();
private:
    struct wlr_input_device *device;
};
