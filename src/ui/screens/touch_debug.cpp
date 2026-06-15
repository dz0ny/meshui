#include "touch_debug.h"
#include "../ui_theme.h"
#include "../ui_screen_mgr.h"
#include "../components/statusbar.h"
#include "../../board.h"
#include <cstdio>

namespace ui::screen::touch_debug {

static lv_obj_t* scr = NULL;
static lv_obj_t* lbl_coords = NULL;
static lv_obj_t* canvas = NULL;
static lv_obj_t* crosshair_h = NULL;
static lv_obj_t* crosshair_v = NULL;
static lv_obj_t* dot = NULL;
static lv_timer_t* timer = NULL;

static void draw_crosshair(int16_t x, int16_t y) {
    if (!crosshair_h || !crosshair_v || !dot) return;
    lv_obj_set_pos(crosshair_h, 0, y);
    lv_obj_clear_flag(crosshair_h, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(crosshair_v, x, 0);
    lv_obj_clear_flag(crosshair_v, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(dot, x - 4, y - 4);
    lv_obj_clear_flag(dot, LV_OBJ_FLAG_HIDDEN);
}

static void hide_crosshair() {
    if (crosshair_h) lv_obj_add_flag(crosshair_h, LV_OBJ_FLAG_HIDDEN);
    if (crosshair_v) lv_obj_add_flag(crosshair_v, LV_OBJ_FLAG_HIDDEN);
    if (dot) lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
}

static void poll_touch(lv_timer_t* t) {
    (void)t;
    if (!lbl_coords) return;

    int16_t x = 0, y = 0;
    if (board::touch.isPressed() && board::touch.getPoint(&x, &y, 1)) {
        char buf[48];
        snprintf(buf, sizeof(buf), "x=%d  y=%d", x, y);
        lv_label_set_text(lbl_coords, buf);
        draw_crosshair(x, y);
    } else {
        lv_label_set_text(lbl_coords, "Touch screen | Boot=back");
        hide_crosshair();
    }
}

static void create(ui::kit::Handle parent_kit) {
    lv_obj_t* parent = (lv_obj_t*)parent_kit;
    scr = parent;

    // Remove all padding so canvas is truly 0,0 based
    lv_obj_set_style_pad_all(parent, 0, LV_PART_MAIN);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_FLEX_IN_NEW_TRACK);
    lv_obj_set_flex_flow(parent, LV_FLEX_FLOW_COLUMN);

    int32_t w = lv_display_get_horizontal_resolution(lv_display_get_default());
    int32_t h = lv_display_get_vertical_resolution(lv_display_get_default());

    // Full-screen canvas at absolute 0,0
    canvas = lv_obj_create(parent);
    lv_obj_set_size(canvas, w, h);
    lv_obj_set_pos(canvas, 0, 0);
    lv_obj_set_style_bg_color(canvas, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(canvas, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(canvas, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(canvas, 0, LV_PART_MAIN);
    lv_obj_clear_flag(canvas, LV_OBJ_FLAG_SCROLLABLE);

    // Grid lines every 40px
    for (int gx = 40; gx < w; gx += 40) {
        lv_obj_t* line = lv_obj_create(canvas);
        lv_obj_set_size(line, 1, h);
        lv_obj_set_pos(line, gx, 0);
        lv_obj_set_style_bg_color(line, lv_color_hex(EPD_COLOR_BORDER), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(line, LV_OPA_40, LV_PART_MAIN);
        lv_obj_set_style_border_width(line, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(line, 0, LV_PART_MAIN);
        lv_obj_clear_flag(line, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    }
    for (int gy = 40; gy < h; gy += 40) {
        lv_obj_t* line = lv_obj_create(canvas);
        lv_obj_set_size(line, w, 1);
        lv_obj_set_pos(line, 0, gy);
        lv_obj_set_style_bg_color(line, lv_color_hex(EPD_COLOR_BORDER), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(line, LV_OPA_40, LV_PART_MAIN);
        lv_obj_set_style_border_width(line, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(line, 0, LV_PART_MAIN);
        lv_obj_clear_flag(line, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    }

    // Corner labels showing coordinates at their actual position (40px inward)
    char tl[16], tr[16], bl[16], br[16];
    snprintf(tl, sizeof(tl), "%d,%d", 40, 40);
    snprintf(tr, sizeof(tr), "%ld,%d", (long)(w-1-40), 40);
    snprintf(bl, sizeof(bl), "%d,%ld", 40, (long)(h-1-40));
    snprintf(br, sizeof(br), "%ld,%ld", (long)(w-1-40), (long)(h-1-40));
    const char* corners[] = {tl, tr, bl, br};
    lv_align_t aligns[] = {LV_ALIGN_TOP_LEFT, LV_ALIGN_TOP_RIGHT, LV_ALIGN_BOTTOM_LEFT, LV_ALIGN_BOTTOM_RIGHT};
    int ox[] = {40, -40, 40, -40};
    int oy[] = {40, 40, -40, -40};
    for (int i = 0; i < 4; i++) {
        lv_obj_t* lbl = lv_label_create(canvas);
        lv_obj_set_style_text_font(lbl, UI_FONT_SMALL, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
        lv_obj_set_style_bg_color(lbl, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(lbl, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_pad_all(lbl, 2, LV_PART_MAIN);
        lv_label_set_text(lbl, corners[i]);
        lv_obj_align(lbl, aligns[i], ox[i], oy[i]);
    }

    // Center crosshair target
    lv_obj_t* center_mark = lv_label_create(canvas);
    lv_obj_set_style_text_font(center_mark, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(center_mark, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    char center_buf[24];
    snprintf(center_buf, sizeof(center_buf), "+%ld,%ld", (long)(w/2), (long)(h/2));
    lv_label_set_text(center_mark, center_buf);
    lv_obj_align(center_mark, LV_ALIGN_CENTER, 0, 0);

    // Crosshair lines (hidden until touch)
    crosshair_h = lv_obj_create(canvas);
    lv_obj_set_size(crosshair_h, w, 1);
    lv_obj_set_style_bg_color(crosshair_h, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_border_width(crosshair_h, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(crosshair_h, 0, LV_PART_MAIN);
    lv_obj_clear_flag(crosshair_h, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_flag(crosshair_h, LV_OBJ_FLAG_HIDDEN);

    crosshair_v = lv_obj_create(canvas);
    lv_obj_set_size(crosshair_v, 1, h);
    lv_obj_set_style_bg_color(crosshair_v, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_border_width(crosshair_v, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(crosshair_v, 0, LV_PART_MAIN);
    lv_obj_clear_flag(crosshair_v, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_flag(crosshair_v, LV_OBJ_FLAG_HIDDEN);

    dot = lv_obj_create(canvas);
    lv_obj_set_size(dot, 9, 9);
    lv_obj_set_style_bg_color(dot, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(dot, 0, LV_PART_MAIN);
    lv_obj_clear_flag(dot, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);

    // Coordinate readout
    lbl_coords = lv_label_create(canvas);
    lv_obj_set_style_text_font(lbl_coords, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_coords, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(lbl_coords, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(lbl_coords, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(lbl_coords, 4, LV_PART_MAIN);
    lv_label_set_text(lbl_coords, "Touch screen | Boot=back");
    lv_obj_align(lbl_coords, LV_ALIGN_TOP_MID, 0, 2);
}

static void entry() {
    ui::statusbar::hide();
    timer = lv_timer_create(poll_touch, 30, NULL);
}

static void exit_fn() {
    if (timer) { lv_timer_delete(timer); timer = NULL; }
    ui::statusbar::show();
}

static void destroy() {
    scr = NULL; lbl_coords = NULL; canvas = NULL;
    crosshair_h = NULL; crosshair_v = NULL; dot = NULL;
    timer = NULL;
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::touch_debug
