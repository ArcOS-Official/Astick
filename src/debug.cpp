#include "debug.h"
#include "wlroots.h"
#include <print>


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
    const QString log = QString::vasprintf(fmt, args);
    std::println("{}", log.toStdString());
    switch (importance) {
        case WLR_ERROR:
            debugger.errors.append(log);
            break;
        case WLR_INFO:
            debugger.errors.append(log);
            break;
        default:
            break;
    }
}
