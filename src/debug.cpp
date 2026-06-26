#include "debug.h"
#include "wlroots.h"
#include <print>
#include <cstdio>
#include <vector>


Debugger::Debugger()
{
    wlr_log_init(WLR_INFO, handler);
}

QString Debugger::lastError()
{
    return errors.last();
}

QString Debugger::lastInfo()
{
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
    std::println("{}: {}", prefix, log.toStdString());
    switch (importance) {
        case WLR_ERROR:
            debugger.errors.append(log);
            break;
        case WLR_INFO:
            debugger.info.append(log);
            break;
        default:
            break;
    }
}
