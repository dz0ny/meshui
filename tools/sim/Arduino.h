#pragma once
// Minimal host stand-in for <Arduino.h> so pure-logic headers that include it
// (waypoint_store.h, trail_store.h, …) compile natively under the sim.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>

using std::min;
using std::max;

#ifndef F
#define F(x) (x)     // Arduino flash-string macro → identity on host
#endif

uint32_t millis();   // defined in sim_display.cpp
