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

#include "debug.h"
#include "wlroots.h"
#include <cstdio>
#include <vector>


Debugger debugger;

Debugger::Debugger()
{
    wlr_log_init(WLR_INFO, handler);
}

QString Debugger::lastError()
{
    QMutexLocker lock(&mutex);
    if (errors.isEmpty()) return {};
    return errors.last();
}

QString Debugger::lastInfo()
{
    QMutexLocker lock(&mutex);
    if (info.isEmpty()) return {};
    return info.last();
}

void handler(enum wlr_log_importance importance, const char *fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    int len = std::vsnprintf(nullptr, 0, fmt, copy);
    va_end(copy);

    std::vector<char> buf(static_cast<size_t>(len) + 1);
    std::vsnprintf(buf.data(), buf.size(), fmt, args);

    const QString log = QString::fromUtf8(buf.data());
    const char *prefix;
    if (importance == WLR_ERROR)
        prefix = "ERROR";
    else
        prefix = "INFO";
    fprintf(stdout, "%s: %s\n", prefix, log.toUtf8().constData());
    fflush(stdout);
    switch (importance) {
        case WLR_ERROR: {
            QMutexLocker lock(&debugger.mutex);
            debugger.errors.append(log);
            break;
        }
        case WLR_INFO: {
            QMutexLocker lock(&debugger.mutex);
            debugger.info.append(log);
            break;
        }
        default:
            break;
    }
}
