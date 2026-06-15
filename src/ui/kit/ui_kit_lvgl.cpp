// ui::kit LVGL backend — thin shim over lv_* for the ESP32 boards.
// Handle is literally an lv_obj_t*, so these are near-zero-overhead wrappers.
// Compiled on boards that ship LVGL; the nRF52 mono backend replaces this file.
#ifndef BOARD_WIO_L1

#include "ui_kit.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <esp_heap_caps.h>
#include "lvgl.h"
#include "../ui_theme.h"
#include "../ui_port.h"
#include "../components/nav_button.h"
#include "../components/msg_list.h"

namespace ui::kit {

// ---- size tokens -----------------------------------------------------------
int pct(int v) { return (int)LV_PCT(v); }
const int CONTENT = (int)LV_SIZE_CONTENT;

static lv_obj_t* O(Handle h) { return (lv_obj_t*)h; }

// kit::Cb is void(*)(void*); LVGL wants lv_event_cb_t. Bridge with a tiny box
// carried as the event user-data and freed when the widget is deleted.
struct CbBox { Cb cb; void* user; };
static void cb_trampoline(lv_event_t* e) {
    CbBox* b = (CbBox*)lv_event_get_user_data(e);
    if (b && b->cb) b->cb(b->user);
}
static void cb_free(lv_event_t* e) {
    delete (CbBox*)lv_event_get_user_data(e);
}

// ---- tree / layout ---------------------------------------------------------
Handle screen_root() { return (Handle)lv_screen_active(); }

static lv_obj_t* bare(lv_obj_t* parent) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(o, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

Handle panel(Handle parent) { return (Handle)bare(O(parent)); }

Handle column(Handle parent) {
    lv_obj_t* o = bare(O(parent));
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return (Handle)o;
}

Handle row(Handle parent) {
    lv_obj_t* o = bare(O(parent));
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return (Handle)o;
}

void size(Handle h, int w, int hgt) { lv_obj_set_size(O(h), w, hgt); }
int  px_width(Handle h)  { lv_obj_update_layout(O(h)); return lv_obj_get_content_width(O(h)); }
int  px_height(Handle h) { lv_obj_update_layout(O(h)); return lv_obj_get_content_height(O(h)); }
void pos(Handle h, int x, int y)    { lv_obj_set_pos(O(h), x, y); }
void pad(Handle h, int all)         { lv_obj_set_style_pad_all(O(h), all, LV_PART_MAIN); }
void gap(Handle h, int between) {
    lv_obj_set_style_pad_row(O(h), between, LV_PART_MAIN);
    lv_obj_set_style_pad_column(O(h), between, LV_PART_MAIN);
}
void grow(Handle h, int factor) { lv_obj_set_flex_grow(O(h), factor); }
void scrollable(Handle h, bool on) {
    if (on) lv_obj_add_flag(O(h), LV_OBJ_FLAG_SCROLLABLE);
    else    lv_obj_clear_flag(O(h), LV_OBJ_FLAG_SCROLLABLE);
}
void hidden(Handle h, bool on) {
    if (on) lv_obj_add_flag(O(h), LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_clear_flag(O(h), LV_OBJ_FLAG_HIDDEN);
}
void text_center(Handle h) {
    lv_obj_set_style_text_align(O(h), LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
}

void align(Handle h, Align a, int dx, int dy) {
    lv_align_t la = LV_ALIGN_TOP_LEFT;
    switch (a) {
        case Align::TopLeft:    la = LV_ALIGN_TOP_LEFT;     break;
        case Align::TopMid:     la = LV_ALIGN_TOP_MID;      break;
        case Align::TopRight:   la = LV_ALIGN_TOP_RIGHT;    break;
        case Align::LeftMid:    la = LV_ALIGN_LEFT_MID;     break;
        case Align::Center:     la = LV_ALIGN_CENTER;       break;
        case Align::RightMid:   la = LV_ALIGN_RIGHT_MID;    break;
        case Align::BottomLeft: la = LV_ALIGN_BOTTOM_LEFT;  break;
        case Align::BottomMid:  la = LV_ALIGN_BOTTOM_MID;   break;
        case Align::BottomRight:la = LV_ALIGN_BOTTOM_RIGHT; break;
    }
    lv_obj_align(O(h), la, dx, dy);
}

// ---- widgets ---------------------------------------------------------------
Handle label(Handle parent, const char* text) {
    lv_obj_t* l = lv_label_create(O(parent));
    lv_obj_set_style_text_font(l, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(l, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    if (text) lv_label_set_text(l, text);
    return (Handle)l;
}
void set_text(Handle h, const char* text) { lv_label_set_text(O(h), text ? text : ""); }
void set_textf(Handle h, const char* fmt, ...) {
    char buf[64];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lv_label_set_text(O(h), buf);
}

Handle button(Handle parent, const char* text, Cb cb, void* user) {
    CbBox* box = new CbBox{cb, user};
    lv_obj_t* b = ui::nav::text_button(O(parent), text, cb_trampoline, box);
    lv_obj_add_event_cb(b, cb_free, LV_EVENT_DELETE, box);
    return (Handle)b;
}

void button_text(Handle btn, const char* text) {
    lv_obj_t* lbl = lv_obj_get_child(O(btn), 0);
    if (lbl) lv_label_set_text(lbl, text);
}

Handle menu_row(Handle parent, const char* text, Cb cb, void* user) {
    CbBox* box = new CbBox{cb, user};
    lv_obj_t* r = ui::nav::menu_item(O(parent), nullptr, text, cb_trampoline, box);
    lv_obj_add_event_cb(r, cb_free, LV_EVENT_DELETE, box);
    return (Handle)r;
}

Handle list(Handle parent) { return (Handle)ui::nav::scroll_list(O(parent)); }

Handle content_area(Handle parent) { return (Handle)ui::nav::content_area(O(parent)); }

Handle toggle_item(Handle parent, const char* label, const char* value, Cb cb, void* user) {
    CbBox* box = cb ? new CbBox{cb, user} : nullptr;
    // ui::nav::toggle_item returns the value label; the click handler is attached
    // to the row internally with box as user-data. Free the box when the value
    // label is deleted (it dies with its row), so no leak across screen rebuilds.
    lv_obj_t* val = ui::nav::toggle_item(O(parent), label, value, cb ? cb_trampoline : nullptr, box);
    if (box) lv_obj_add_event_cb(val, cb_free, LV_EVENT_DELETE, box);
    return (Handle)val;
}

Handle info_row(Handle parent, const char* lblText) {
    lv_obj_t* r = lv_obj_create(O(parent));
    lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(r, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(r, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(r, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(r, 0, LV_PART_MAIN);
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lbl = lv_label_create(r);
    lv_obj_set_style_text_font(lbl, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(lbl, lblText);

    lv_obj_t* val = lv_label_create(r);
    lv_obj_set_style_text_font(val, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(val, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(val, "--");
    return (Handle)val;
}

Handle section_header(Handle parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(O(parent));
    lv_obj_set_style_text_font(lbl, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_pad_top(lbl, 10, LV_PART_MAIN);
    lv_label_set_text(lbl, text);
    return (Handle)lbl;
}

void long_mode(Handle h, LongMode m) {
    lv_label_long_mode_t lm = LV_LABEL_LONG_CLIP;
    switch (m) {
        case LongMode::Clip: lm = LV_LABEL_LONG_CLIP; break;
        case LongMode::Dot:  lm = LV_LABEL_LONG_DOT;  break;
        case LongMode::Wrap: lm = LV_LABEL_LONG_WRAP; break;
    }
    lv_label_set_long_mode(O(h), lm);
}

// ---- style -----------------------------------------------------------------
void card(Handle h) {
    lv_obj_t* o = O(h);
    lv_obj_set_style_bg_color(o, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(o, UI_BORDER_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_color(o, lv_color_hex(EPD_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(o, 8, LV_PART_MAIN);
}

void font(Handle h, Font f) {
    const lv_font_t* fnt = UI_FONT_BODY;
    switch (f) {
        case Font::Small:   fnt = UI_FONT_SMALL;    break;
        case Font::Body:    fnt = UI_FONT_BODY;     break;
        case Font::Title:   fnt = UI_FONT_TITLE;    break;
        case Font::ClockLg: fnt = UI_FONT_CLOCK_LG; break;
    }
    lv_obj_set_style_text_font(O(h), fnt, LV_PART_MAIN);
}

void text_color(Handle h, Color c) {
    uint32_t col = EPD_COLOR_TEXT;
    switch (c) {
        case Color::Fg:     col = EPD_COLOR_TEXT;   break;
        case Color::Bg:     col = EPD_COLOR_BG;     break;
        case Color::Border: col = EPD_COLOR_BORDER; break;
        case Color::Focus:  col = EPD_COLOR_FOCUS;  break;
    }
    lv_obj_set_style_text_color(O(h), lv_color_hex(col), LV_PART_MAIN);
}

// ---- input -----------------------------------------------------------------
void focusable(Handle h) { ui::port::keyboard_focus_register(O(h)); }

void on_click(Handle h, Cb cb, void* user) {
    lv_obj_t* o = O(h);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    CbBox* box = new CbBox{cb, user};
    lv_obj_add_event_cb(o, cb_trampoline, LV_EVENT_CLICKED, box);
    lv_obj_add_event_cb(o, cb_free, LV_EVENT_DELETE, box);
}

void free_layout(Handle h) { lv_obj_set_layout(O(h), LV_LAYOUT_NONE); }

void focus_refresh() { ui::port::keyboard_focus_invalidate(); }

// ---- canvas ----------------------------------------------------------------
// L8 (8-bit gray) surface in PSRAM; the e-ink flush thresholds it to 1-bit. The
// mono backend will use a 1bpp buffer drawn straight to GxEPD2 instead.
struct CanvasState { uint8_t* buf; int w; int h; };

static uint8_t canvas_l8(Color col) {
    uint32_t v = (col == Color::Bg)     ? EPD_COLOR_BG
               : (col == Color::Focus)  ? EPD_COLOR_FOCUS
               : (col == Color::Border) ? EPD_COLOR_BORDER
               :                          EPD_COLOR_TEXT;
    uint8_t r = (v >> 16) & 0xFF, g = (v >> 8) & 0xFF, b = v & 0xFF;
    return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
}

static CanvasState* CS(Handle c) { return (CanvasState*)lv_obj_get_user_data(O(c)); }

static void canvas_free(lv_event_t* e) {
    CanvasState* st = (CanvasState*)lv_event_get_user_data(e);
    if (st) { if (st->buf) heap_caps_free(st->buf); delete st; }
}

Handle canvas(Handle parent, int w, int h) {
    CanvasState* st = new CanvasState{nullptr, w, h};
    st->buf = (uint8_t*)heap_caps_malloc((size_t)w * h, MALLOC_CAP_SPIRAM);
    lv_obj_t* cv = lv_canvas_create(O(parent));
    if (st->buf) lv_canvas_set_buffer(cv, st->buf, w, h, LV_COLOR_FORMAT_L8);
    lv_obj_set_user_data(cv, st);
    lv_obj_add_event_cb(cv, canvas_free, LV_EVENT_DELETE, st);
    return (Handle)cv;
}

int canvas_w(Handle c) { CanvasState* s = CS(c); return s ? s->w : 0; }
int canvas_h(Handle c) { CanvasState* s = CS(c); return s ? s->h : 0; }

static inline void cv_px(CanvasState* s, int x, int y, uint8_t v) {
    if (s && s->buf && x >= 0 && x < s->w && y >= 0 && y < s->h) s->buf[y * s->w + x] = v;
}

void canvas_clear(Handle c, Color col) {
    CanvasState* s = CS(c);
    if (s && s->buf) memset(s->buf, canvas_l8(col), (size_t)s->w * s->h);
}
void canvas_pixel(Handle c, int x, int y, Color col) { cv_px(CS(c), x, y, canvas_l8(col)); }

void canvas_line(Handle c, int x0, int y0, int x1, int y1, Color col) {
    CanvasState* s = CS(c);
    if (!s) return;
    uint8_t v = canvas_l8(col);
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        cv_px(s, x0, y0, v);
        cv_px(s, x0 + 1, y0, v);   // 2px thick for e-ink legibility
        cv_px(s, x0, y0 + 1, v);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void canvas_fill_circle(Handle c, int cx, int cy, int r, Color col) {
    CanvasState* s = CS(c);
    if (!s) return;
    uint8_t v = canvas_l8(col);
    for (int dy = -r; dy <= r; dy++)
        for (int dx = -r; dx <= r; dx++)
            if (dx * dx + dy * dy <= r * r) cv_px(s, cx + dx, cy + dy, v);
}

void canvas_flush(Handle c) { lv_obj_invalidate(O(c)); }

// ---- message list ----------------------------------------------------------
Handle msglist(Handle parent) { return (Handle)ui::msg_list::create(O(parent)); }
void   msg_append(Handle l, const char* sender, const char* text, bool is_self, int msg_idx) {
    ui::msg_list::append(O(l), sender, text, 0, is_self, msg_idx);
}
void   msg_clear(Handle l)         { ui::msg_list::clear(O(l)); }
void   msg_scroll_bottom(Handle l) { ui::msg_list::scroll_to_bottom(O(l)); }

// ---- timers ----------------------------------------------------------------
struct TimerBox { Cb cb; void* user; };
static void timer_trampoline(lv_timer_t* t) {
    TimerBox* b = (TimerBox*)lv_timer_get_user_data(t);
    if (b && b->cb) b->cb(b->user);
}
Timer every(uint32_t ms, Cb cb, void* user) {
    TimerBox* b = new TimerBox{cb, user};
    lv_timer_t* t = lv_timer_create(timer_trampoline, ms, b);
    return (Timer)t;
}
void stop(Timer t) {
    if (!t) return;
    lv_timer_t* lt = (lv_timer_t*)t;
    TimerBox* b = (TimerBox*)lv_timer_get_user_data(lt);
    lv_timer_delete(lt);
    delete b;
}

// lv_async_cb_t is already void(*)(void*) — same shape as kit::Cb.
void defer(Cb cb, void* user) { lv_async_call((lv_async_cb_t)cb, user); }

} // namespace ui::kit

#endif // !BOARD_WIO_L1
