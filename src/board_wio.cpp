#ifdef BOARD_WIO_L1
#include <Arduino.h>
#include <SPI.h>
#include "board_wio.h"

// Display pins (from variants/wio-tracker-l1/variant.h): the panel sits on the
// secondary SPI bus (SPI1 = pins 31/33/37).
#ifndef PIN_DISPLAY_CS
  #define PIN_DISPLAY_CS   36
  #define PIN_DISPLAY_DC   34
  #define PIN_DISPLAY_RST  32
  #define PIN_DISPLAY_BUSY 35
#endif

GxEPD2_BW<GxEPD2_213_B74, GxEPD2_213_B74::HEIGHT> display(
    GxEPD2_213_B74(PIN_DISPLAY_CS, PIN_DISPLAY_DC, PIN_DISPLAY_RST, PIN_DISPLAY_BUSY));

namespace board_wio {

void init() {
    // The Adafruit nRF52 core provides SPI1 wired to the variant's PIN_SPI1_*.
    display.epd2.selectSPI(SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
    SPI1.begin();
    display.init(115200, true, 2, false);
    display.setRotation(1);   // landscape: 250 x 122
}

} // namespace board_wio
#endif // BOARD_WIO_L1
