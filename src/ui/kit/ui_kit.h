#pragma once
#include <stdint.h>

// ui::kit — a small UI facade for meshui screens.
//
// Why this exists: LVGL is ~120 KB of flash and assumes PSRAM — it cannot fit
// the nRF52840 e-ink tracker (708 KB app / 256 KB SRAM, no PSRAM). But our
// screens only use a handful of UI concepts: containers with flex layout,
// labels, tappable rows/buttons, scrollable lists, a text input and a canvas.
// This header is that handful — in OUR vocabulary, not LVGL's generic object
// model — so it can be backed by two implementations:
//
//   ui_kit_lvgl.cpp  — thin shim over lv_* (ESP32 boards: T5 ePaper, T-Deck)
//   ui_kit_mono.cpp  — tiny 1-bit engine drawing straight to GxEPD2 (nRF52)
//
// Screens are written ONCE against ui::kit. Per-panel layout differences stay
// in the UI_* layout constants, exactly as they do today.
//
// Handle is opaque: under the LVGL backend it is literally an lv_obj_t*, so the
// shim is close to zero-overhead.

namespace ui::kit {

typedef void* Handle;             // opaque widget; LVGL backend: lv_obj_t*
typedef void (*Cb)(void* user);   // click / value-changed callback

// ---- size tokens -----------------------------------------------------------
// A dimension is either pixels (a plain non-negative int), a percentage of the
// parent (pct(n)), or "fit content" (CONTENT). Encoded so the LVGL backend can
// translate to lv_pct()/LV_SIZE_CONTENT without allocating.
int pct(int v);
extern const int CONTENT;

enum class Align : uint8_t {
    TopLeft, TopMid, TopRight,
    LeftMid, Center, RightMid,
    BottomLeft, BottomMid, BottomRight,
};

// Logical font roles — each backend maps these to a concrete font for its
// panel. On a 250x122 mono panel ClockLg is just a slightly bigger small font.
enum class Font : uint8_t { Small, Body, Title, ClockLg };

// Logical colors. On mono panels these collapse to black/white; Fg=ink, Bg=paper.
enum class Color : uint8_t { Fg, Bg, Border, Focus };

// ---- tree / layout ---------------------------------------------------------
Handle screen_root();                       // content area of the active screen
Handle panel(Handle parent);               // plain container, no auto layout
Handle column(Handle parent);              // vertical flex container
Handle row(Handle parent);                 // horizontal flex container (space-between)
void   size(Handle h, int w, int hgt);     // w/hgt: px | pct(n) | CONTENT
int    px_width(Handle h);                 // resolved inner width in pixels
int    px_height(Handle h);                // resolved inner height in pixels
void   pos(Handle h, int x, int y);        // absolute position within parent
void   align(Handle h, Align a, int dx = 0, int dy = 0);
void   pad(Handle h, int all);             // inner padding
void   gap(Handle h, int between);         // gap between flex children
void   grow(Handle h, int factor);         // flex-grow weight within a row/column
void   scrollable(Handle h, bool on);

// ---- widgets ---------------------------------------------------------------
Handle label(Handle parent, const char* text);
void   set_text(Handle h, const char* text);
void   set_textf(Handle h, const char* fmt, ...);   // printf-style label update
Handle button(Handle parent, const char* text, Cb cb, void* user);
void   button_text(Handle btn, const char* text);   // update a button's label
Handle menu_row(Handle parent, const char* text, Cb cb, void* user);  // full-width tappable row
Handle list(Handle parent);                // scrollable vertical flex (no momentum/elastic)
Handle content_area(Handle parent);        // standard screen content panel below the nav row

// Settings row: label on the left, value on the right. Returns the VALUE handle
// (set_text it to update). Pass cb=nullptr for a read-only/display row.
Handle toggle_item(Handle parent, const char* label, const char* value, Cb cb, void* user);

// Read-only "label ......... value" row (no tap). Returns the value handle to
// set_text(). A bold section title above a group of rows.
Handle info_row(Handle parent, const char* label);
Handle section_header(Handle parent, const char* text);

// How a label handles text wider than its box.
enum class LongMode : uint8_t { Clip, Dot, Wrap };
void   long_mode(Handle h, LongMode m);

// ---- style -----------------------------------------------------------------
void   card(Handle h);                     // bordered, rounded card surface
void   font(Handle h, Font f);
void   text_color(Handle h, Color c);
void   text_center(Handle h);              // center-align a label's text
void   hidden(Handle h, bool on);

// ---- canvas (raster drawing) -----------------------------------------------
// A pixel surface for the trail/map view. Backed by an L8 buffer on LVGL and a
// 1-bit buffer on the mono backend; draw in logical colors so screen code is
// panel-agnostic. Coordinates are canvas-local pixels. Call flush() after a
// batch of draws to push to the display.
Handle canvas(Handle parent, int w, int h);
int    canvas_w(Handle c);
int    canvas_h(Handle c);
void   canvas_clear(Handle c, Color col);
void   canvas_pixel(Handle c, int x, int y, Color col);
void   canvas_line(Handle c, int x0, int y0, int x1, int y1, Color col);
void   canvas_fill_circle(Handle c, int cx, int cy, int r, Color col);
void   canvas_flush(Handle c);

// ---- message list ----------------------------------------------------------
// Chat bubble list. Caller owns message indices (passed back via on_click rows).
Handle msglist(Handle parent);
void   msg_append(Handle list, const char* sender, const char* text, bool is_self, int msg_idx);
void   msg_clear(Handle list);
void   msg_scroll_bottom(Handle list);

// ---- input -----------------------------------------------------------------
void   focusable(Handle h);                // register for keypad/button focus nav
void   on_click(Handle h, Cb cb, void* user);  // make any widget tappable
void   free_layout(Handle h);              // disable auto (flex) layout for absolute positioning
void   focus_refresh();                    // re-scan focusable widgets after a rebuild

// ---- timers ----------------------------------------------------------------
// A periodic UI tick. Screens use this for their refresh cadence instead of
// LVGL's lv_timer, so screen code stays backend-agnostic.
typedef void* Timer;
Timer every(uint32_t ms, Cb cb, void* user);
void  stop(Timer t);

// Run cb once, after the current UI cycle (deferred). For work that must not
// happen mid-event, e.g. rebuilding the screen stack.
void  defer(Cb cb, void* user);

} // namespace ui::kit
