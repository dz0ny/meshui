#pragma once

// target.h — hardware abstraction for t-paper board (companion radio style)
// This provides the same interface that MeshCore variants expect.

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <helpers/sensors/MicroNMEALocationProvider.h>

#if defined(BOARD_WIO_L1)
  // nRF52 Wio Tracker L1
  #include "WioTrackerL1Board.h"
  #include <helpers/AutoDiscoverRTCClock.h>
  extern WioTrackerL1Board mc_board;
  extern WRAPPER_CLASS radio_driver;
  extern AutoDiscoverRTCClock rtc_clock;
  extern EnvironmentSensorManager sensors;
  extern MicroNMEALocationProvider gps_provider;
#else
  // ESP32 boards (T5 e-paper / T-Deck)
  #include <helpers/ESP32Board.h>
  class T5ePaperBoard : public ESP32Board {
  public:
      uint16_t getBattMilliVolts() override;
      const char* getManufacturerName() const override { return "LilyGo T5-ePaper"; }
  };
  extern T5ePaperBoard mc_board;
  extern WRAPPER_CLASS radio_driver;
  extern ESP32RTCClock rtc_clock;
  extern EnvironmentSensorManager sensors;
  extern MicroNMEALocationProvider gps_provider;
#endif

void rtc_init();   // call from board::init() before tasks start
bool radio_init();
uint32_t radio_get_rng_seed();
void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr);
void radio_set_tx_power(int8_t dbm);
mesh::LocalIdentity radio_new_identity();
