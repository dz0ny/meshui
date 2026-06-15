// nRF52 Wio Tracker L1 target — radio/board/RTC/sensors for the MeshCore stack.
// Mirrors the SAR firmware's target.cpp; provides the globals target.h declares.
#ifdef BOARD_WIO_L1

#include <Arduino.h>
#include "target.h"
#include <helpers/ArduinoHelpers.h>

WioTrackerL1Board mc_board;

RADIO_CLASS  radio = new Module(P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, SPI);
WRAPPER_CLASS radio_driver(radio, mc_board);

VolatileRTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);

// GPS (L76K) on Serial1; the sensor manager owns it. Pass &rtc_clock so the
// provider's loop() writes the GPS time into the RTC — the Wio has no hardware
// RTC, so this GPS sync is the *only* way the clock ever gets set.
MicroNMEALocationProvider gps_provider(Serial1, &rtc_clock);
EnvironmentSensorManager sensors(gps_provider);

void rtc_init() {
    rtc_clock.begin(Wire);
}

bool radio_init() {
    // CustomSX1262::std_init reads SX126X_DIO2_AS_RF_SWITCH / DIO3_TCXO_VOLTAGE
    // (from variant.h), so TCXO + RF-switch are configured for this board.
    return radio.std_init(&SPI);
}

uint32_t radio_get_rng_seed() { return radio.random(0x7FFFFFFF); }

void radio_set_params(float freq, float bw, uint8_t sf, uint8_t cr) {
    radio.setFrequency(freq);
    radio.setSpreadingFactor(sf);
    radio.setBandwidth(bw);
    radio.setCodingRate(cr);
}

void radio_set_tx_power(int8_t dbm) { radio.setOutputPower(dbm); }

mesh::LocalIdentity radio_new_identity() {
    RadioNoiseListener rng(radio);
    return mesh::LocalIdentity(&rng);
}
#endif // BOARD_WIO_L1
