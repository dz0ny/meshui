#pragma once

#include <stdint.h>
#include "lvgl.h"

namespace ui::theme {

enum theme_id : uint8_t {
    THEME_LIGHT = 0,
    THEME_DARK = 1,
    THEME_SAR_RED = 2,
    THEME_SAR_GREEN = 3,
    THEME_SAR_NAVY_BLUE = 4,
    THEME_NEO_VICE = 5,
    THEME_SIGNAL_ORANGE = 6,
    THEME_LAGOON = 7,
    THEME_ORCHID = 8,
    THEME_CITRINE = 9,
};

struct palette_t {
    uint32_t bg;
    uint32_t fg;
    uint32_t text;
    uint32_t border;
    uint32_t focus;
    uint32_t prompt_bg;
    uint32_t prompt_txt;
};

void init();
uint8_t count();
theme_id current();
bool set(theme_id id);
theme_id next();
const char* current_name();
const palette_t& colors();
void style_scrollbar_hint(lv_obj_t* obj);

// Shared styles — applied via lv_obj_add_style() to reduce per-widget local style allocations.
// Re-initialized on theme change; must be applied before widget creation so new widgets pick up
// the current palette.
extern lv_style_t style_menu_row;       // inner container of menu_item / toggle_item rows
extern lv_style_t style_text_button;    // large action button (text_button)
extern lv_style_t style_nav_action;     // small action button in nav bar
extern lv_style_t style_transparent;    // bg_opa=0, border=0, pad=0 containers

} // namespace ui::theme

#define EPD_COLOR_BG          (ui::theme::colors().bg)
#define EPD_COLOR_FG          (ui::theme::colors().fg)
#define EPD_COLOR_TEXT        (ui::theme::colors().text)
#define EPD_COLOR_BORDER      (ui::theme::colors().border)
#define EPD_COLOR_FOCUS       (ui::theme::colors().focus)
#define EPD_COLOR_PROMPT_BG   (ui::theme::colors().prompt_bg)
#define EPD_COLOR_PROMPT_TXT  (ui::theme::colors().prompt_txt)

// ---------- Screen IDs ----------
#include "screen_ids.h"

// ---------- Font declarations ----------

LV_FONT_DECLARE(lv_font_noto_14);               // T-Deck statusbar, small text
LV_FONT_DECLARE(lv_font_noto_15);               // T-Deck nav/back button
LV_FONT_DECLARE(lv_font_noto_16);               // T-Deck titles, menu labels
LV_FONT_DECLARE(lv_font_noto_24);               // statusbar, sender names
LV_FONT_DECLARE(lv_font_noto_28);               // message text, values
LV_FONT_DECLARE(lv_font_montserrat_bold_30);    // menus, titles, settings
LV_FONT_DECLARE(lv_font_montserrat_bold_80);    // lock screen clock
LV_FONT_DECLARE(lv_font_montserrat_bold_120);   // home screen clock

// ---------- Per-device layout ----------

#if defined(BOARD_TDECK)
#include "layout/tdeck.h"
#else
#include "layout/epaper.h"
#endif
