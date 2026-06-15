#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Emoji / pictographic stripping for user-supplied text (node names, chat
// messages, sender names). The e-ink fonts (Noto Latin-Extended) have no glyphs
// for emoji, so they render as blank "tofu" boxes — strip them at every input
// boundary instead. Pure C++ (no Arduino / LVGL deps) so both the LVGL and mono
// backends can share it.

namespace util {

// True for codepoints that are emoji / pictographs / their modifiers.
static inline bool is_emoji_cp(uint32_t cp) {
    if (cp >= 0x1F000 && cp <= 0x1FFFF) return true;   // SMP emoji / symbol blocks
    if (cp >= 0x2600  && cp <= 0x27BF)  return true;   // Misc Symbols + Dingbats
    if (cp >= 0x2300  && cp <= 0x23FF)  return true;   // Misc Technical (⌚ ⏰ ⏳ …)
    if (cp >= 0x2B00  && cp <= 0x2BFF)  return true;   // Misc Symbols & Arrows
    if (cp >= 0xFE00  && cp <= 0xFE0F)  return true;   // variation selectors
    if (cp == 0x200D)                   return true;   // zero-width joiner
    if (cp == 0x20E3)                   return true;   // combining enclosing keycap
    if (cp == 0x2122 || cp == 0x2139)   return true;   // ™  ℹ
    if (cp == 0x2194 || cp == 0x2195)   return true;   // ↔ ↕ (emoji-presented arrows)
    return false;
}

// Strip emoji codepoints from a UTF-8 string in place. Stripping only shortens,
// so this is safe to do over the original buffer. Returns the new byte length.
static inline size_t strip_emoji_inplace(char* s) {
    if (!s) return 0;
    const unsigned char* in = (const unsigned char*)s;
    char* out = s;
    while (*in) {
        unsigned char b = in[0];
        int len; uint32_t cp;
        if (b < 0x80)              { len = 1; cp = b; }
        else if ((b & 0xE0) == 0xC0) { len = 2; cp = b & 0x1F; }
        else if ((b & 0xF0) == 0xE0) { len = 3; cp = b & 0x0F; }
        else if ((b & 0xF8) == 0xF0) { len = 4; cp = b & 0x07; }
        else                       { in++; continue; }   // invalid lead byte: drop

        bool ok = true;
        for (int i = 1; i < len; i++) {
            if ((in[i] & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (in[i] & 0x3F);
        }
        if (!ok) { in++; continue; }                     // malformed: drop lead byte

        if (!is_emoji_cp(cp)) {
            for (int i = 0; i < len; i++) *out++ = (char)in[i];
        }
        in += len;
    }
    *out = 0;
    // Trim a trailing space left behind by a stripped emoji at the end.
    while (out > s && out[-1] == ' ') { *--out = 0; }
    return (size_t)(out - s);
}

// Copy src into dst (bounded) then strip emoji in place.
static inline void strip_emoji_copy(char* dst, size_t dst_sz, const char* src) {
    if (!dst || dst_sz == 0) return;
    if (!src) { dst[0] = 0; return; }
    strncpy(dst, src, dst_sz - 1);
    dst[dst_sz - 1] = 0;
    strip_emoji_inplace(dst);
}

} // namespace util
