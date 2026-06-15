#ifdef BOARD_EPAPER

#include "ui_port.h"
#include "../board.h"
#include "../model.h"
#include "../nvs_param.h"
#include <epdiy.h>
#include <Wire.h>

namespace ui::port {

#ifndef T_PAPER_EPD_DEBUG
#define T_PAPER_EPD_DEBUG 0
#endif

static int refresh_mode = UI_REFRESH_MODE_NORMAL;
static volatile bool touch_enabled = true;
static int backlight_mode = 0;
static const char* mode_names_bl[] = {"Auto", "On", "Off"};
static int brightness = 1;  // default Mid
static const char* bright_names[] = {"Low", "Mid", "High"};
static const int bright_pwm[] = {50, 100, 230};
static lv_display_t* epaper_disp = NULL;
static int32_t i1_stride = 0;               // bytes per row of LVGL's I1 draw buffer
// LVGL's default I1 palette: bit 1 = white. The e-ink theme draws black-on-white,
// so a set bit is a white (background) pixel. Flip this if the panel comes out
// inverted on first flash.
#define I1_BIT_IS_WHITE 1
static uint8_t* packed_prev_frame = NULL;  // packed 4-bit rotated framebuffer shadow
static int32_t* rotated_row_offsets = NULL;
static bool cycle_force_full_refresh = false;
static bool cycle_had_updates = false;
static bool cycle_panel_powered = false;
static int cycle_temperature = 0;
static uint32_t cycle_dirty_pixels = 0;
static uint16_t cycle_dirty_areas = 0;
static uint32_t cycle_started_ms = 0;
// Union of all changed rects in the current refresh cycle — coalesced into a single
// epd_hl_update_area call to avoid stacking panel waveforms for each dirty region.
static lv_area_t cycle_union_rect = {};
static bool cycle_union_valid = false;
static uint32_t partial_refresh_count_since_full = 0;
static uint32_t accumulated_partial_pixels_since_full = 0;
static const uint32_t MAX_PARTIAL_REFRESHES_BEFORE_FULL = 96;
static const uint32_t MAX_ACCUMULATED_COVERAGE_BEFORE_FULL = 3;
struct FullRefreshStamp {
    uint8_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    bool valid;
};
static FullRefreshStamp last_full_refresh = {};

static inline void checkError(enum EpdDrawError err) {
    if (err != EPD_DRAW_SUCCESS) {
        Serial.printf("EPD draw error: %X\n", err);
    }
}

static void sync_packed_prev_frame(uint8_t *fb, int32_t phys_w, int32_t phys_h) {
    if (!packed_prev_frame || !fb) {
        return;
    }
    memcpy(packed_prev_frame, fb, (size_t)(phys_w / 2) * phys_h);
}

// Read one pixel out of an I1 row (MSB-first, 8 px/byte) and return whether it
// is a white pixel — i.e. should map to epdiy's white nibble (0xF).
static inline bool i1_is_white(const uint8_t* row, int32_t rx) {
    uint8_t bit = (row[rx >> 3] >> (7 - (rx & 7))) & 1u;
#if I1_BIT_IS_WHITE
    return bit != 0;
#else
    return bit == 0;
#endif
}

static void init_rotated_row_offsets(int32_t phys_h, int32_t half_w) {
    if (rotated_row_offsets) {
        return;
    }
    rotated_row_offsets = (int32_t *)ps_malloc(sizeof(int32_t) * phys_h);
    if (!rotated_row_offsets) {
        return;
    }
    for (int32_t rx = 0; rx < phys_h; rx++) {
        rotated_row_offsets[rx] = (phys_h - 1 - rx) * half_w;
    }
}

static inline int32_t rotated_offset(int32_t rx, int32_t phys_h, int32_t half_w) {
    if (rotated_row_offsets) {
        return rotated_row_offsets[rx];
    }
    return (phys_h - 1 - rx) * half_w;
}

static FullRefreshStamp current_refresh_stamp() {
    return {
        .year = model::clock.year,
        .month = model::clock.month,
        .day = model::clock.day,
        .hour = model::clock.hour,
        .valid = true,
    };
}

static bool should_do_hourly_full_refresh() {
    FullRefreshStamp now = current_refresh_stamp();
    if (!last_full_refresh.valid) {
        last_full_refresh = now;
        return false;
    }
    return now.year != last_full_refresh.year ||
           now.month != last_full_refresh.month ||
           now.day != last_full_refresh.day ||
           now.hour != last_full_refresh.hour;
}

static void note_full_refresh_done() {
    last_full_refresh = current_refresh_stamp();
}

static uint32_t display_pixel_count() {
    return (uint32_t)epd_rotated_display_width() * (uint32_t)epd_rotated_display_height();
}

static bool should_force_budget_full_refresh() {
    uint32_t total_pixels = display_pixel_count();
    if (partial_refresh_count_since_full >= MAX_PARTIAL_REFRESHES_BEFORE_FULL) {
        return true;
    }
    if (total_pixels == 0) {
        return false;
    }
    return accumulated_partial_pixels_since_full >= (total_pixels * MAX_ACCUMULATED_COVERAGE_BEFORE_FULL);
}

static void render_start_cb(lv_event_t *event) {
    (void)event;
    cycle_force_full_refresh = should_do_hourly_full_refresh() || should_force_budget_full_refresh();
    cycle_had_updates = false;
    cycle_dirty_pixels = 0;
    cycle_dirty_areas = 0;
    cycle_started_ms = millis();
    cycle_union_valid = false;
    if (!cycle_panel_powered) {
        epd_poweron();
        cycle_panel_powered = true;
    }
    cycle_temperature = epd_ambient_temperature();
}

static void render_ready_cb(lv_event_t *event) {
    (void)event;
    uint32_t elapsed_ms = millis() - cycle_started_ms;
    if (cycle_force_full_refresh && cycle_had_updates) {
        checkError(epd_hl_update_screen(&board::hl, MODE_GC16, cycle_temperature));
        note_full_refresh_done();
        sync_packed_prev_frame(epd_hl_get_framebuffer(&board::hl), epd_width(), epd_height());
        partial_refresh_count_since_full = 0;
        accumulated_partial_pixels_since_full = 0;
    } else if (cycle_had_updates && cycle_union_valid) {
        // Single panel waveform for the union of all dirty rects in this cycle.
        EpdRect update_rect = {
            .x = (int)cycle_union_rect.x1,
            .y = (int)cycle_union_rect.y1,
            .width = (int)(cycle_union_rect.x2 - cycle_union_rect.x1 + 1),
            .height = (int)(cycle_union_rect.y2 - cycle_union_rect.y1 + 1),
        };
        EpdDrawMode mode = (refresh_mode == UI_REFRESH_MODE_FAST) ? MODE_DU : MODE_GL16;
        checkError(epd_hl_update_area(&board::hl, mode, cycle_temperature, update_rect));
        partial_refresh_count_since_full++;
        accumulated_partial_pixels_since_full += cycle_dirty_pixels;
    }
#if T_PAPER_EPD_DEBUG
    if (cycle_had_updates) {
        uint32_t total_pixels = display_pixel_count();
        uint32_t coverage_pct = total_pixels == 0 ? 0 : (accumulated_partial_pixels_since_full * 100) / total_pixels;
        Serial.printf("EPD: %s pass areas=%u px=%lu t=%lums partials=%lu coverage=%lu%%\n",
                      cycle_force_full_refresh ? "full" : "partial",
                      cycle_dirty_areas,
                      (unsigned long)cycle_dirty_pixels,
                      (unsigned long)elapsed_ms,
                      (unsigned long)partial_refresh_count_since_full,
                      (unsigned long)coverage_pct);
    }
#else
    (void)elapsed_ms;
#endif
    if (cycle_panel_powered) {
        epd_poweroff();
        cycle_panel_powered = false;
    }
    cycle_force_full_refresh = false;
    cycle_had_updates = false;
    cycle_dirty_pixels = 0;
    cycle_dirty_areas = 0;
    cycle_union_valid = false;
}

static void round_invalidate_area_cb(lv_event_t *event) {
    lv_area_t *area = (lv_area_t *)lv_event_get_param(event);
    if (!area || !epaper_disp) {
        return;
    }

    if (area->y1 < 0) {
        area->y1 = 0;
    }
    if (area->y1 & 1) {
        area->y1 -= 1;
    }

    lv_coord_t max_y = lv_display_get_vertical_resolution(epaper_disp) - 1;
    if ((area->y2 & 1) == 0) {
        area->y2 += 1;
    }
    if (area->y2 > max_y) {
        area->y2 = max_y;
    }
    if (area->y1 > area->y2) {
        area->y1 = area->y2;
    }
}

static inline void pack_single_row(uint8_t *fb, const uint8_t *prev_fb, const uint8_t *src_row,
                                   int32_t phys_h, int32_t half_w, int32_t ry, int32_t x1, int32_t x2,
                                   bool track_dirty_area, bool *changed) {
    int32_t px_x = ry;
    int32_t byte_x = px_x / 2;
    bool is_odd = px_x & 1;

    if (is_odd) {
        for (int32_t rx = x1; rx <= x2; rx++) {
            int32_t offset = rotated_offset(rx, phys_h, half_w) + byte_x;
            uint8_t nib = i1_is_white(src_row, rx) ? 0xF0 : 0x00;
            uint8_t packed = (uint8_t)((prev_fb[offset] & 0x0F) | nib);
            if (track_dirty_area && packed != prev_fb[offset]) {
                *changed = true;
            }
            fb[offset] = packed;
        }
    } else {
        for (int32_t rx = x1; rx <= x2; rx++) {
            int32_t offset = rotated_offset(rx, phys_h, half_w) + byte_x;
            uint8_t nib = i1_is_white(src_row, rx) ? 0x0F : 0x00;
            uint8_t packed = (uint8_t)((prev_fb[offset] & 0xF0) | nib);
            if (track_dirty_area && packed != prev_fb[offset]) {
                *changed = true;
            }
            fb[offset] = packed;
        }
    }
}

static inline void pack_row_pair(uint8_t *fb, const uint8_t *prev_fb, const uint8_t *src_even,
                                 const uint8_t *src_odd, int32_t phys_h, int32_t half_w, int32_t even_ry,
                                 int32_t x1, int32_t x2,
                                 bool track_dirty_area, bool *changed) {
    int32_t byte_x = even_ry / 2;
    for (int32_t rx = x1; rx <= x2; rx++) {
        int32_t offset = rotated_offset(rx, phys_h, half_w) + byte_x;
        uint8_t hi = i1_is_white(src_odd, rx)  ? 0xF0 : 0x00;
        uint8_t lo = i1_is_white(src_even, rx) ? 0x0F : 0x00;
        uint8_t packed = (uint8_t)(hi | lo);
        if (track_dirty_area && packed != prev_fb[offset]) {
            *changed = true;
        }
        fb[offset] = packed;
    }
}

// RENDER_MODE_DIRECT flush: px_map is the persistent full-screen L8 buffer,
// area identifies only the dirty region. L8 format means each pixel is already
// 8-bit grayscale — we just pack into 4-bit nibbles for epdiy.
//
// Rotation inlined for EPD_ROT_INVERTED_PORTRAIT:
//   physical_x = rotated_y
//   physical_y = epd_height() - 1 - rotated_x
static void disp_flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    // I1 draw buffer: an 8-byte palette prefixes the pixel data, and each row is
    // i1_stride bytes (1 bit/pixel, MSB first). Skip the palette, then address
    // rows by the I1 stride instead of the old L8 byte-per-pixel layout.
    const uint8_t *i1pix = px_map + 8;
    const int32_t stride = i1_stride;

    int32_t area_w = lv_area_get_width(area);
    int32_t area_h = lv_area_get_height(area);
    int32_t area_px = area_w * area_h;

    uint8_t *fb = epd_hl_get_framebuffer(&board::hl);
    int32_t phys_w = epd_width();
    int32_t phys_h = epd_height();

    // The panel width (rotated 540) is not a multiple of 8, so LVGL pads I1 rows
    // up to 544 and can hand us an area with x past the real edge. Clamp X to the
    // physical width — the padding columns are off-panel (and indexing the
    // rotation table / framebuffer with them reads/writes out of bounds).
    int32_t disp_w_rot = epd_rotated_display_width();
    int32_t x1 = area->x1 < 0 ? 0 : area->x1;
    int32_t x2 = area->x2 >= disp_w_rot ? disp_w_rot - 1 : area->x2;
    if (x1 > x2) { lv_display_flush_ready(disp); return; }

    // Pack I1 (1-bit) → 4-bit nibbles in epdiy framebuffer with inline rotation.
    // Each source pixel is black or white; two adjacent source rows map to the
    // same destination byte, so pack row pairs together.
    int32_t half_w = phys_w / 2;
    init_rotated_row_offsets(phys_h, half_w);
    if (!packed_prev_frame) {
        packed_prev_frame = (uint8_t *)ps_malloc((size_t)half_w * phys_h);
        if (packed_prev_frame) {
            memcpy(packed_prev_frame, fb, (size_t)half_w * phys_h);
        }
    }

    bool changed = false;
    bool track_dirty_area = packed_prev_frame && rotated_row_offsets;
    int32_t ry = area->y1;
    if (ry & 1) {
        const uint8_t *src_row = &i1pix[ry * stride];
        pack_single_row(fb, packed_prev_frame ? packed_prev_frame : fb, src_row, phys_h, half_w, ry,
                        x1, x2, track_dirty_area, &changed);
        ry++;
    }
    for (; ry < area->y2; ry += 2) {
        const uint8_t *src_even = &i1pix[ry * stride];
        const uint8_t *src_odd = &i1pix[(ry + 1) * stride];
        pack_row_pair(fb, packed_prev_frame ? packed_prev_frame : fb, src_even, src_odd, phys_h, half_w, ry,
                      x1, x2, track_dirty_area, &changed);
    }
    if (ry == area->y2) {
        const uint8_t *src_row = &i1pix[ry * stride];
        pack_single_row(fb, packed_prev_frame ? packed_prev_frame : fb, src_row, phys_h, half_w, ry,
                        x1, x2, track_dirty_area, &changed);
    }

    if (track_dirty_area && !changed) {
        // Packed bytes are identical to shadow — fb and packed_prev_frame already match,
        // so no shadow sync needed and no panel waveform needed for this area.
        lv_display_flush_ready(disp);
        return;
    }

    // Sync the shadow for the dirty region (fb now holds the new packed bytes).
    if (packed_prev_frame) {
        int32_t packed_y1 = phys_h - 1 - x2;
        int32_t packed_y2 = phys_h - 1 - x1;
        int32_t packed_x1 = area->y1 / 2;
        int32_t packed_x2 = area->y2 / 2;
        int32_t packed_len = packed_x2 - packed_x1 + 1;
        for (int32_t py = packed_y1; py <= packed_y2; py++) {
            int32_t offset = py * half_w + packed_x1;
            memcpy(&packed_prev_frame[offset], &fb[offset], packed_len);
        }
    }

    cycle_had_updates = true;
    cycle_dirty_pixels += area_px;
    cycle_dirty_areas++;

    // Accumulate into the per-cycle union rect; actual epd_hl_update_area runs once
    // in render_ready_cb so we get a single panel waveform per refresh cycle.
    if (!cycle_force_full_refresh) {
        if (!cycle_union_valid) {
            cycle_union_rect.x1 = x1;
            cycle_union_rect.y1 = area->y1;
            cycle_union_rect.x2 = x2;
            cycle_union_rect.y2 = area->y2;
            cycle_union_valid = true;
        } else {
            if (x1 < cycle_union_rect.x1) cycle_union_rect.x1 = x1;
            if (area->y1 < cycle_union_rect.y1) cycle_union_rect.y1 = area->y1;
            if (x2 > cycle_union_rect.x2) cycle_union_rect.x2 = x2;
            if (area->y2 > cycle_union_rect.y2) cycle_union_rect.y2 = area->y2;
        }
    }

    if (!cycle_panel_powered) {
        epd_poweron();
        cycle_panel_powered = true;
        cycle_temperature = epd_ambient_temperature();
    }

    lv_display_flush_ready(disp);
}

static void touch_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    static int16_t x = 0, y = 0;
    static lv_indev_state_t last_state = LV_INDEV_STATE_RELEASED;
    static uint32_t next_poll = 0;

    // Throttle I2C touch reads to ~20Hz — faster than e-paper refresh, but still light on the bus
    uint32_t now = millis();
    if (now >= next_poll) {
        next_poll = now + 50;
        if (board::touch.isPressed() && touch_enabled) {
            if (board::touch.getPoint(&x, &y, 1)) {
                last_state = LV_INDEV_STATE_PRESSED;
            }
        } else {
            last_state = LV_INDEV_STATE_RELEASED;
        }
    }
    data->state = last_state;
    data->point.x = x;
    data->point.y = y;
}

static void keyboard_read_cb(lv_indev_t *indev, lv_indev_data_t *data) {
    static uint32_t last_key = 0;
    static uint32_t last_backspace_ms = 0;
    static uint8_t backspace_repeat_count = 0;
    static bool backspace_cleared = false;

    int c = board::keyboard_read_char();
    if (c > 0) {
        uint32_t now = millis();
        last_key = (uint32_t)c;
        data->state = LV_INDEV_STATE_PRESSED;
        model::touch_activity();

        switch (c) {
            case 0x08:
                if (last_backspace_ms == 0 || (now - last_backspace_ms) > 250) {
                    backspace_repeat_count = 1;
                    backspace_cleared = false;
                } else {
                    backspace_repeat_count++;
                }
                last_backspace_ms = now;

                if (!backspace_cleared && backspace_repeat_count >= 4) {
                    lv_group_t *group = lv_group_get_default();
                    lv_obj_t *focused = group ? lv_group_get_focused(group) : NULL;
                    if (focused && lv_obj_check_type(focused, &lv_textarea_class)) {
                        lv_textarea_set_text(focused, "");
                        backspace_cleared = true;
                    }
                }

                data->key = LV_KEY_BACKSPACE;
                break;
            case 0x0D: data->key = LV_KEY_ENTER; break;
            case 0x1B: data->key = LV_KEY_ESC; break;
            case 0x09: data->key = LV_KEY_NEXT; break;
            case 0xF1: data->key = LV_KEY_DOWN; break;
            case 0xF2: data->key = LV_KEY_UP; break;
            case 0xF3: data->key = LV_KEY_LEFT; break;
            case 0xF4: data->key = LV_KEY_RIGHT; break;
            default:
                last_backspace_ms = 0;
                backspace_repeat_count = 0;
                backspace_cleared = false;
                data->key = (uint32_t)c;
                break;
        }
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
        data->key = last_key;
    }
}

static uint32_t tick_cb(void) {
    return millis();
}

void init() {
    lv_init();
    lv_tick_set_cb(tick_cb);

    int mode = nvs_param_get_u8(NVS_ID_REFRESH_MODE);
    if (mode < UI_REFRESH_MODE_NORMAL || mode > UI_REFRESH_MODE_FAST) {
        mode = UI_REFRESH_MODE_NORMAL;
    }
    refresh_mode = mode;

    int stored_brightness = nvs_param_get_u8(NVS_ID_BRIGHTNESS);
    if (stored_brightness < 0 || stored_brightness > 2) {
        stored_brightness = 1;
    }
    brightness = stored_brightness;

    int disp_w = epd_rotated_display_width();
    int disp_h = epd_rotated_display_height();
    size_t pixel_count = disp_w * disp_h;

    lv_display_t *disp = lv_display_create(disp_w, disp_h);
    epaper_disp = disp;
    lv_display_set_flush_cb(disp, disp_flush_cb);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_I1);
    lv_display_add_event_cb(disp, round_invalidate_area_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_display_add_event_cb(disp, render_start_cb, LV_EVENT_RENDER_START, NULL);
    lv_display_add_event_cb(disp, render_ready_cb, LV_EVENT_REFR_READY, NULL);

    // DIRECT mode with I1 (1-bit): LVGL renders pure black/white. The flush
    // packs each bit to epdiy's 4-bit nibble (black 0x0 / white 0xF). The buffer
    // is row-padded to i1_stride bytes with an 8-byte palette prefix, per LVGL's
    // monochrome buffer contract. Single buffered in PSRAM.
    i1_stride = (int32_t)lv_draw_buf_width_to_stride(disp_w, LV_COLOR_FORMAT_I1);
    size_t buf_size = (size_t)i1_stride * disp_h + 8;  // +8 = I1 palette header
    (void)pixel_count;
    void *buf1 = ps_calloc(1, buf_size);
    lv_display_set_buffers(disp, buf1, NULL, buf_size, LV_DISPLAY_RENDER_MODE_DIRECT);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, touch_read_cb);
    lv_indev_set_display(indev, disp);

    // Disable scroll momentum/throw — instant stop on e-ink (100 = max deceleration)
    lv_indev_set_scroll_throw(indev, 100);

    if (board::peri_status[E_PERI_KEYBOARD]) {
        lv_group_t *g = lv_group_create();
        lv_group_set_default(g);
        lv_group_set_focus_cb(g, keyboard_focus_group_cb);
        lv_indev_t *kb_indev = lv_indev_create();
        lv_indev_set_type(kb_indev, LV_INDEV_TYPE_KEYPAD);
        lv_indev_set_read_cb(kb_indev, keyboard_read_cb);
        lv_indev_set_display(kb_indev, disp);
        lv_indev_set_group(kb_indev, g);
    }

}

void set_refresh_mode(int mode) { refresh_mode = mode; }
int  get_refresh_mode() { return refresh_mode; }

void full_refresh() {
    epd_hl_set_all_white(&board::hl);
    epd_poweron();
    checkError(epd_hl_update_screen(&board::hl, MODE_GL16, epd_ambient_temperature()));
    epd_poweroff();
    sync_packed_prev_frame(epd_hl_get_framebuffer(&board::hl), epd_width(), epd_height());
    note_full_refresh_done();
    partial_refresh_count_since_full = 0;
    accumulated_partial_pixels_since_full = 0;
}

void full_clean() {
    int t = 12;
    epd_poweron();
    for (int i = 0; i < 10; i++) epd_push_pixels(epd_full_screen(), t, 0);
    for (int i = 0; i < 10; i++) epd_push_pixels(epd_full_screen(), t, 1);
    for (int i = 0; i < 2; i++)  epd_push_pixels(epd_full_screen(), t, 2);
    epd_poweroff();
    partial_refresh_count_since_full = 0;
    accumulated_partial_pixels_since_full = 0;
}

void touch_enable()  { touch_enabled = true; }
void touch_disable() { touch_enabled = false; }

// Backlight mode: 0=Auto, 1=On, 2=Off

void set_backlight(int mode) {
    if (mode < 0) mode = 0;
    if (mode > 2) mode = 2;
    backlight_mode = mode;
    if (mode == 1) {
        analogWrite(BOARD_BL_EN, bright_pwm[brightness]);  // On at current brightness
    } else {
        analogWrite(BOARD_BL_EN, 0);  // Off or Auto (auto handled by ui_task)
    }
}

void set_brightness(int level) {
    if (level < 0) level = 0;
    if (level > 2) level = 2;
    brightness = level;
}

void apply_backlight() {
    analogWrite(BOARD_BL_EN, bright_pwm[brightness]);
}

bool is_backlight_auto() { return backlight_mode == 0; }
int get_backlight() { return backlight_mode; }
const char* get_backlight_name() { return mode_names_bl[backlight_mode]; }
int get_brightness() { return brightness; }
const char* get_brightness_name() { return bright_names[brightness]; }
int get_brightness_pwm() { return bright_pwm[brightness]; }

} // namespace ui::port

#endif // BOARD_EPAPER
