// host stand-in for the Teensy core, enough for lib/bent_bus (see bent_render.cpp)
#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>
using std::min; using std::max;
static inline void __disable_irq() {}
static inline void __enable_irq() {}
