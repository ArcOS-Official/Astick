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