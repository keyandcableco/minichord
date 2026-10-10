// host stand-in for the Teensy core, enough for drum_machine.h
#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>
#include <algorithm>
#define PROGMEM
template <class T, class L, class H> static inline T constrain(T x, L lo, H hi) { return x < lo ? lo : x > hi ? hi : x; }
using std::min; using std::max;
static inline void __disable_irq() {}
static inline void __enable_irq() {}
