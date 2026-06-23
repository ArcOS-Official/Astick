#pragma once

#include <QtCore>
#include <QObject>

class Astick : public QCoreApplication {
    Q_OBJECT
public:
    Astick(int argc, char **argv);

    int exec();
signals:
    void aboutToRun();
};
