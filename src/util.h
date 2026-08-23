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
#include "wlroots.h"
#include <QString>
#include <type_traits>

#define signal(name, sig, callback) \
    name = signal_(callback); \
    wl_signal_add(sig, &name);
struct wl_listener signal_(
    void (*callback)(struct wl_listener *, void *)
);

// Log-and-continue helpers — never throw/abort, return safe fallback.
void logAndContinue(const QString &ctx, const QString &detail);
void logAndContinue(const char *ctx, const char *detail);
bool logIf(bool ok, const char *ctx, const char *fmt, ...);

template<typename T>
T valueOrLog(T v, T fallback, const char *ctx) {
    bool invalid = false;
    if constexpr (std::is_pointer_v<T>) {
        invalid = (v == nullptr);
    } else if constexpr (std::is_same_v<T, QString>) {
        invalid = v.isEmpty();
    } else if constexpr (std::is_arithmetic_v<T>) {
        invalid = (v == T{});
    } else {
        invalid = (v == T{});
    }
    if (invalid) {
        logAndContinue(QString::fromUtf8(ctx), QStringLiteral("value invalid, using fallback"));
        return fallback;
    }
    return v;
}

// Explicit instantiation declarations for common types (definitions in util.cpp if needed)
extern template int valueOrLog<int>(int, int, const char *);
extern template float valueOrLog<float>(float, float, const char *);
extern template double valueOrLog<double>(double, double, const char *);
