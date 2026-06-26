#include "application.h"

Astick::Astick(int argc, char **argv) : QCoreApplication(argc, argv)
{}

int Astick::exec()
{
    emit aboutToRun();
    return QCoreApplication::exec();
}