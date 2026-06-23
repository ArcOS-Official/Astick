#include <qobject.h>
#include "../wlroots.h"

class Mouse : public QObject {
    Q_OBJECT

public:
    Mouse(struct wlr_input_device *device);
    ~Mouse();
};
