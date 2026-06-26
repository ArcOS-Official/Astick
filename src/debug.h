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

#include <cstdarg>
#include <qobject.h>
#include "wlroots.h"

class Debugger : public QObject
{
    public:
        QList<QString> errors;
        QList<QString> info;

        Debugger();
        QString lastError();
        QString lastInfo();
};

static Debugger debugger;

void handler(enum wlr_log_importance importance, const char *fmt, va_list args);
