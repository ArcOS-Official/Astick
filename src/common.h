/*
   The common classes and data structures for the engine and the manager.
   Copyright (C) 2026 Eyad Ahmed Ragheb

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

#include <QObject>
#include <functional>
#include <qtmetamacros.h>

struct Vec2 {
    uint32_t x, y;

    Vec2 operator+(Vec2 &other) {
        return { other.x + x, other.y + y };
    }

    Vec2 operator-(Vec2 &other) {
        return { x - other.x, y - other.y };
    }

    Vec2 operator/(Vec2 &other) {
        return { x / other.x, y / other.y };
    }

    Vec2 operator*(Vec2 &other) {
        return { x * other.x, y * other.y };
    }
};

class Draggable : public QObject {
    Q_OBJECT
    public:
signals:
        void dragStart();
};

union Event {
    struct Keyboard {
        struct Mods {
            bool super;
            bool shift_l;
            bool shift_r;
            bool alt;
            bool ctrl;
            bool caps;
        };

        Mods mods;
        // this excludes the modifiers pressed
        std::vector<uint32_t> keycodes;
    };

    struct Pointer {
        Vec2 pos;
        // dragging does not necessarily mean that it's moving or resizing it's just
        // in interactive mode
        bool dragging;
        // id of dragged window
        uint32_t dragged;
    };

    struct Window {
        enum class Kind {
            New,
            ResizeRequest,
            MoveRequest,
            Close
        };
        uint32_t id;
        Kind kind;
        void *data;
    };

    struct Output {
        enum class Kind {
            New,
            Remove
        };
        enum class ColorProfile {
            SRGB
        };
        uint32_t id;
        Kind kind;
        Vec2 size;
        float brightness;
        ColorProfile colorProfile;
    };

    struct Input {
        enum class Kind {
            Keyboard,
            Mouse
        };
        uint32_t id;
        Kind kind;
    };
    Keyboard keyboard;
    Pointer pointer;
    Window window;
    Output output;
    Input input;
};


enum class WindowMode {
    Tiling,
    Floating
};

class Config {
    public:
        uint32_t gap;
        uint32_t animationSpeed;
        WindowMode defaultMode;

        explicit Config() { }
};
