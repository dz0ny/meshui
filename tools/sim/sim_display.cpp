#include "sim_arduino.h"
#include "glcdfont.h"
#include <cstdio>
#include <cstdint>
#include <vector>

SimDisplay display;

static uint32_t s_millis = 0;
uint32_t millis() { return s_millis; }
void sim_set_millis(uint32_t ms) { s_millis = ms; }

// Classic Adafruit font: 5 columns/glyph, column byte holds 8 rows (LSB = top).
// Mirrors Adafruit_GFX::drawChar so the sim's text is pixel-identical to the panel.
void SimDisplay::drawGlyphChar(char ch) {
    uint8_t c = (uint8_t)ch;
    for (int i = 0; i < 5; i++) {
        uint8_t line = glcd_font[c * 5 + i];
        for (int j = 0; j < 8; j++, line >>= 1) {
            if (line & 1) {
                if (_size == 1) drawPixel(_cx + i, _cy + j, _txt);
                else fillRect(_cx + i * _size, _cy + j * _size, _size, _size, _txt);
            }
        }
    }
}

// ---- minimal 8-bit grayscale PNG writer (zlib stored blocks, no deps) -------

static uint32_t crc_table[256];
static bool crc_ready = false;
static void crc_init() {
    for (uint32_t n = 0; n < 256; n++) {
        uint32_t c = n;
        for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        crc_table[n] = c;
    }
    crc_ready = true;
}
static uint32_t crc32(const uint8_t* p, size_t n, uint32_t crc = 0xFFFFFFFFu) {
    if (!crc_ready) crc_init();
    for (size_t i = 0; i < n; i++) crc = crc_table[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return crc;
}

static void put32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x);
}
static void chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    put32(out, (uint32_t)data.size());
    size_t crc_start = out.size();
    out.insert(out.end(), type, type + 4);
    out.insert(out.end(), data.begin(), data.end());
    uint32_t c = crc32(&out[crc_start], out.size() - crc_start) ^ 0xFFFFFFFFu;
    put32(out, c);
}

bool SimDisplay::savePng(const char* path) const {
    return write_gray_png(path, _w, _h, _buf);
}

bool write_gray_png(const char* path, int W, int H, const unsigned char* buf) {
    // Raw image: each row prefixed with filter byte 0. Grayscale, 8-bit.
    std::vector<uint8_t> raw;
    raw.reserve((W + 1) * H);
    for (int y = 0; y < H; y++) {
        raw.push_back(0);
        for (int x = 0; x < W; x++) raw.push_back(buf[y * W + x]);
    }

    // zlib stream: 2-byte header + stored (uncompressed) deflate blocks + adler32.
    std::vector<uint8_t> zlib;
    zlib.push_back(0x78); zlib.push_back(0x01);
    size_t pos = 0;
    while (pos < raw.size()) {
        size_t block = raw.size() - pos;
        if (block > 65535) block = 65535;
        bool last = (pos + block >= raw.size());
        zlib.push_back(last ? 1 : 0);
        zlib.push_back(block & 0xFF); zlib.push_back((block >> 8) & 0xFF);
        zlib.push_back(~block & 0xFF); zlib.push_back((~block >> 8) & 0xFF);
        zlib.insert(zlib.end(), raw.begin() + pos, raw.begin() + pos + block);
        pos += block;
    }
    // adler32 over raw
    uint32_t a = 1, b = 0;
    for (uint8_t byte : raw) { a = (a + byte) % 65521; b = (b + a) % 65521; }
    put32(zlib, (b << 16) | a);

    std::vector<uint8_t> out = {0x89,'P','N','G',0x0D,0x0A,0x1A,0x0A};
    std::vector<uint8_t> ihdr;
    put32(ihdr, W); put32(ihdr, H);
    ihdr.push_back(8);   // bit depth
    ihdr.push_back(0);   // colour type 0 = grayscale
    ihdr.push_back(0); ihdr.push_back(0); ihdr.push_back(0);
    chunk(out, "IHDR", ihdr);
    chunk(out, "IDAT", zlib);
    chunk(out, "IEND", {});

    FILE* f = fopen(path, "wb");
    if (!f) return false;
    fwrite(out.data(), 1, out.size(), f);
    fclose(f);
    return true;
}
