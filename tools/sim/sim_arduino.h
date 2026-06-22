#pragma once
// Host (native) shim that stands in for board_wio.h + <Arduino.h> when the mono
// UI engine is compiled with -DMESHUI_SIM. It provides a `display` object with
// the exact Adafruit-GFX / GxEPD2 call surface the engine uses, rasterising into
// an in-memory 8-bit grayscale buffer instead of an e-ink panel.
#include <stdint.h>
#include <stddef.h>
#include <string>

// GxEPD2 colour constants the engine references (cfg()/cbg()).
static const uint16_t GxEPD_BLACK = 0x0000;
static const uint16_t GxEPD_WHITE = 0xFFFF;

// Milliseconds since sim start (monotonic, advanced manually by the harness).
uint32_t millis();
void sim_set_millis(uint32_t ms);

// An Adafruit_GFX-compatible 1-bit panel backed by a grayscale byte buffer.
// 0 = black ink, 255 = white paper.
class SimDisplay {
public:
    static const int W = 250;   // GxEPD2_213_B74 visible width  (rotation 1)
    static const int H = 122;   // GxEPD2_213_B74 visible height (rotation 1)

    int16_t width()  const { return W; }
    int16_t height() const { return H; }

    void setFont(const void*) {}                 // classic font only
    void setTextSize(uint8_t s) { _size = s ? s : 1; }
    void setTextColor(uint16_t c) { _txt = c; }
    void setCursor(int16_t x, int16_t y) { _cx = x; _cy = y; }

    // Paging: full-buffer panel, so a single page covers the screen.
    void setFullWindow() {}
    void setPartialWindow(int, int, int, int) {}
    void firstPage() {}
    bool nextPage() { return false; }   // full buffer: one page covers the screen

    void fillScreen(uint16_t c) { for (int i = 0; i < W * H; i++) _buf[i] = lum(c); }
    void drawPixel(int16_t x, int16_t y, uint16_t c) {
        if (x < 0 || y < 0 || x >= W || y >= H) return;
        _buf[y * W + x] = lum(c);
    }
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
        for (int16_t j = 0; j < h; j++) for (int16_t i = 0; i < w; i++) drawPixel(x + i, y + j, c);
    }
    void drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t c) {
        drawFastHLine(x, y, w, c); drawFastHLine(x, y + h - 1, w, c);
        for (int16_t j = 0; j < h; j++) { drawPixel(x, y + j, c); drawPixel(x + w - 1, y + j, c); }
    }
    void drawFastHLine(int16_t x, int16_t y, int16_t w, uint16_t c) {
        for (int16_t i = 0; i < w; i++) drawPixel(x + i, y, c);
    }

    // Text: classic 5x7 glyph in a 6x8 cell, scaled by _size — identical raster
    // to Adafruit_GFX::drawChar so on-host output matches the panel.
    void print(const char* s) { while (s && *s) drawChar(*s++); }
    void print(char ch) { drawChar(ch); }

    // Save the current buffer as an 8-bit grayscale PNG. Returns true on success.
    bool savePng(const char* path) const;
    const uint8_t* pixels() const { return _buf; }

private:
    static uint8_t lum(uint16_t c) { return c == GxEPD_BLACK ? 0 : 255; }
    void drawGlyphChar(char ch);
    void drawChar(char ch) {
        if (ch == '\n') { _cx = 0; _cy += 8 * _size; return; }
        if (ch == '\r') { _cx = 0; return; }
        drawGlyphChar(ch);
        _cx += 6 * _size;
    }

    uint8_t _buf[W * H];
    int16_t _cx = 0, _cy = 0;
    uint8_t _size = 1;
    uint16_t _txt = GxEPD_BLACK;
};

extern SimDisplay display;

// Write an arbitrary 8-bit grayscale image (0=black..255=white) as a PNG.
bool write_gray_png(const char* path, int w, int h, const unsigned char* buf);
