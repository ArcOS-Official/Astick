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
