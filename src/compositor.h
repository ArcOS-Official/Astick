#pragma once
// Compatibility shim — Engine is now the compositor. Keeps old #include "compositor.h" working.
#include "engine/engine.h"
#include <QList>
class Output;
class Toplevel;
using Compositor = astick::Engine;
