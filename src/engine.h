/*
   The backend engine that gives data to the state machine. Also handles
   animations. C/pyright (C) 2026 Eyad Ahmed Ragheb

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "common.h"
#include "wlr.h"
#include <QObject>
#include <qrandom.h>
#include <wayland-server.h>

class Compositor {
public:
    struct Server;

    struct Toplevel {
        uint64_t id;

        struct Server *server;
        struct wlr_xdg_toplevel *xdg_toplevel;
        struct wlr_scene_tree *scene_tree;
        struct wl_listener map;
        struct wl_listener unmap;
        struct wl_listener commit;
        struct wl_listener destroy;
        struct wl_listener request_move;
        struct wl_listener request_resize;
        struct wl_listener request_maximize;
        struct wl_listener request_fullscreen;
    };
    struct Popup {
        Server *server;
        uint32_t id;
        struct wlr_xdg_popup *xdg_popup;
        struct wl_listener commit;
        struct wl_listener destroy;
    };

    struct Workspace {
        std::vector<Toplevel *> toplevels;
    };

    enum class CursorMode { Passthrough, Move, Resize };

    struct Keyboard {
        uint32_t id;
        Server *server;
        struct wlr_keyboard *wlr_keyboard;

        struct wl_listener modifiers;
        struct wl_listener key;
        struct wl_listener destroy;
    };

    struct Output {
        Server *server;
        uint32_t id;
        struct wlr_output *wlr_output;
        struct wl_listener frame;
        struct wl_listener request_state;
        struct wl_listener destroy;
    };

    struct Server {
        Compositor *comp;

        struct wl_display *wl_display;
        struct wlr_backend *backend;
        struct wlr_renderer *renderer;
        struct wlr_allocator *allocator;
        struct wlr_scene *scene;
        struct wlr_scene_output_layout *scene_layout;

        struct wlr_xdg_shell *xdg_shell;
        struct wl_listener new_xdg_toplevel;
        struct wl_listener new_xdg_popup;

        struct wlr_cursor *cursor;
        struct wlr_xcursor_manager *cursor_mgr;
        struct wl_listener cursor_motion;
        struct wl_listener cursor_motion_absolute;
        struct wl_listener cursor_button;
        struct wl_listener cursor_axis;
        struct wl_listener cursor_frame;

        struct wlr_seat *seat;
        struct wl_listener new_input;
        struct wl_listener request_cursor;
        struct wl_listener request_set_selection;
        CursorMode cursor_mode;
        struct Toplevel *grabbed_toplevel;
        double grab_x, grab_y;
        struct wlr_box grab_geobox;
        uint32_t resize_edges;

        struct wlr_output_layout *output_layout;
        struct wl_listener new_output;

        std::vector<Output> outputs;
        std::vector<Toplevel> toplevels;
        std::vector<Popup> popups;
        std::vector<Keyboard> keyboards;
    };

    Server server;
    void process_cursor_motion(uint32_t time);
    void process_cursor_move();
    void process_cursor_resize();
    void focus(uint32_t id);
    void run();
    Compositor::Toplevel *desktop_toplevel_at(
            double lx, double ly, struct wlr_surface **surface,
            double *sx, double *sy);

    explicit Compositor();
    ~Compositor();
    signals:
    void newEvent(Compositor *);
};

uint32_t genId() noexcept;
