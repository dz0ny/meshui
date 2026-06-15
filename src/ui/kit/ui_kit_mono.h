#pragma once
#include <stdint.h>

// Mono-backend-only entry points (the LVGL backend has lv_timer_handler instead).
// main_wio builds a screen with the ui::kit facade, then drives it with these.
namespace ui::kit::mono {

void reset();                 // drop the current node tree (start a new screen)
void render();                // layout + draw the tree to the e-ink (paged)
void redraw();                // force a redraw on next render (e.g. clock ticked)

// Fixed top status bar (clock / GPS / battery …). The engine reserves `h` px at
// the top and calls fn() each render to paint it (fn draws via the display +
// reads the model); screen content lives/scrolls below it.
typedef void (*StatusbarFn)(int w, int h);
void set_statusbar(int h, StatusbarFn fn);
void tick(uint32_t now_ms);   // fire any due timers
void feed_key(char key);      // input: 'U'/'D' move focus, 'E' activate, 'B' back

// Transient banner overlay drawn near the top of the screen, auto-expiring after
// `ms`. Backs ui::toast::show() on the mono panel.
void toast(const char* msg, uint32_t ms);

// Minimal screen stack. A screen is a builder function that constructs its tree
// with the ui::kit facade (it must NOT call reset() — go()/back() handle that).
typedef void (*ScreenFn)();
void go(ScreenFn fn);         // push + build + draw a new screen
void back();                  // pop to the previous screen

} // namespace ui::kit::mono
