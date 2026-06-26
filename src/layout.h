#pragma once

#include <QObject>
#include <qobject.h>
#include <QList>
#include "wlroots.h"

class Toplevel;

class TilingLayout : public QObject
{
    Q_OBJECT

public:
    TilingLayout();

    void arrange(struct wlr_output *output, const QList<Toplevel *> &toplevels);
};