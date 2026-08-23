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

#include "util.h"
#include "debug.h"
#include <QDebug>
#include <QMutexLocker>
#include <cstdarg>
#include <cstdio>

void logAndContinue(const QString &ctx, const QString &detail) {
    const QString msg = QStringLiteral("[%1] %2").arg(ctx, detail);
    // wlroots log
    wlr_log(WLR_ERROR, "[%s] %s", ctx.toUtf8().constData(), detail.toUtf8().constData());
    // Debugger.errors thread-safe
    {
        QMutexLocker lock(&debugger.mutex);
        debugger.errors.append(msg);
    }
    qWarning().noquote() << "[Astick]" << ctx << detail;
}

void logAndContinue(const char *ctx, const char *detail) {
    logAndContinue(QString::fromUtf8(ctx), QString::fromUtf8(detail));
}

bool logIf(bool ok, const char *ctx, const char *fmt, ...) {
    if (ok) return true;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    logAndContinue(QString::fromUtf8(ctx), QString::fromUtf8(buf));
    return false;
}

// Explicit instantiations
template int valueOrLog<int>(int, int, const char *);
template float valueOrLog<float>(float, float, const char *);
template double valueOrLog<double>(double, double, const char *);

struct wl_listener signal_(
    void (*callback)(struct wl_listener *, void *)
)
{
    struct wl_listener ret;
    ret.notify = callback;
    return ret;
}
