#pragma once

#include <qobject.h>
#include "../wlroots.h"

class Mouse : public QObject {
    Q_OBJECT

public:
    Mouse(struct wlr_input_device *device);
    ~Mouse();

signals:
    void destroyed();

private:
    struct wlr_input_device *device;
};