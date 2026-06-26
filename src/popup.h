#pragma once

#include <QObject>
#include <qobject.h>
#include "wlroots.h"

class Compositor;

class Popup : public QObject
{
    Q_OBJECT

public:
    Popup(
        Compositor *server,
        struct wlr_xdg_popup *popup_
    );
    friend void handle_popup_commit(wl_listener *listener, void *data);
    friend void handle_popup_destroy(wl_listener *listener, void *data);

signals:
    void committed();
    void destroyed();
    void closed();

private:
    Compositor *server;
    struct wlr_xdg_popup *popup;
    struct wl_listener commit;
    struct wl_listener destroy;
};