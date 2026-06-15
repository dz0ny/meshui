#pragma once
#include <GxEPD2_BW.h>

// Seeed Wio Tracker L1 — 2.13" 250x122 mono e-ink (GxEPD2_213_B74) on SPI1.
extern GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display;

namespace board_wio {
void init();   // bring up SPI1 + the panel, rotation 1 (landscape 250x122)
}
