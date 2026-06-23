#include <qobject.h>
#include "../wlroots.h"

class Keyboard : public QObject {
    Q_OBJECT

public:
    Keyboard(struct wlr_input_device *device);
    ~Keyboard();
};
