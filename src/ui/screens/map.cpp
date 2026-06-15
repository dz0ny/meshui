#include <cstring>
#include <cstdio>
#include <cmath>
#include <esp_heap_caps.h>
#include "map.h"
#include "../ui_theme.h"
#include "../ui_port.h"
#include "../ui_screen_mgr.h"
#include "../components/nav_button.h"
#include "../components/toast.h"
#include "../components/geo_utils.h"
#include "../components/text_utils.h"
#include "../../model.h"
#include "../components/statusbar.h"

namespace ui::screen::map {

static bool fullscreen = false;
static lv_obj_t* scr = NULL;
static lv_obj_t* canvas_obj = NULL;
static lv_obj_t* tap_layer = NULL;
static lv_obj_t* lbl_zoom = NULL;
static lv_obj_t* grid_label = NULL;
static lv_obj_t* no_fix_label = NULL;
static lv_obj_t* contact_taps[32] = {};
static lv_obj_t* contact_name_labels[32] = {};
static uint32_t last_contacts_revision = 0;
static double last_gps_lat = 0.0;
static double last_gps_lon = 0.0;
static bool last_gps_fix = false;

// Zoom levels in km radius
static const double zoom_levels[] = {0.5, 1.0, 5.0, 20.0, 50.0};
static const int n_zoom = 5;
static int zoom_idx = 2;  // default 5km

static int map_w = 0;
static int map_h = 0;
static int map_cx = 0;
static int map_cy = 0;

// Canvas buffer in PSRAM (L8 = 1 byte per pixel)
static uint8_t* canvas_buf = NULL;
static size_t canvas_buf_size = 0;

struct MapContact {
    char name[32];
    double lat, lon;
    int px, py;
};
static MapContact contacts[32];
static int contact_count = 0;

static void on_back(lv_event_t* e) { ui::screen_mgr::pop(true); }

static void rebuild_map();

static uint8_t color_to_l8(uint32_t color) {
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    return (uint8_t)((r * 77 + g * 150 + b * 29) >> 8);
}

static uint8_t map_bg_color() { return color_to_l8(EPD_COLOR_BG); }
static uint8_t map_grid_color() { return color_to_l8(EPD_COLOR_BORDER); }
static uint8_t map_axis_color() { return color_to_l8(EPD_COLOR_TEXT); }
static uint8_t map_marker_color() { return color_to_l8(EPD_COLOR_FOCUS); }

static void style_overlay_label(lv_obj_t* obj) {
    lv_obj_set_style_text_color(obj, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_bg_color(obj, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, UI_BORDER_THIN, LV_PART_MAIN);
    lv_obj_set_style_border_color(obj, lv_color_hex(EPD_COLOR_BORDER), LV_PART_MAIN);
}

static void on_zoom_in(lv_event_t* e) {
    if (zoom_idx > 0) zoom_idx--;
    rebuild_map();
}

static void on_zoom_out(lv_event_t* e) {
    if (zoom_idx < n_zoom - 1) zoom_idx++;
    rebuild_map();
}

static void on_contact_tap(lv_event_t* e) {
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx < 0 || idx >= contact_count) return;
    if (!model::gps.has_fix) return;

    double dist = ui::geo::distance_km(model::gps.lat, model::gps.lng,
                                        contacts[idx].lat, contacts[idx].lon);
    double bear = ui::geo::bearing(model::gps.lat, model::gps.lng,
                                    contacts[idx].lat, contacts[idx].lon);
    static char buf[64];
    if (dist < 1.0) {
        snprintf(buf, sizeof(buf), "%s: %dm %s", contacts[idx].name,
                 (int)(dist * 1000), ui::geo::bearing_to_cardinal(bear));
    } else {
        snprintf(buf, sizeof(buf), "%s: %.1fkm %s", contacts[idx].name,
                 dist, ui::geo::bearing_to_cardinal(bear));
    }
    ui::toast::show(buf);
}

static void load_contacts() {
    contact_count = 0;
    for (int i = 0; i < model::contact_count && contact_count < 32; i++) {
        const model::ContactEntry& contact = model::contacts[i];
        if (contact.gps_lat == 0 && contact.gps_lon == 0) continue;
        strncpy(contacts[contact_count].name, contact.name, 31);
        contacts[contact_count].name[31] = 0;
        ui::text::strip_emoji(contacts[contact_count].name);
        contacts[contact_count].lat = contact.gps_lat / 1e6;
        contacts[contact_count].lon = contact.gps_lon / 1e6;
        contacts[contact_count].px = -1;
        contacts[contact_count].py = -1;
        contact_count++;
    }
}

static void draw_filled_circle(int cx, int cy, int r, uint8_t color) {
    for (int dy = -r; dy <= r; dy++) {
        for (int dx = -r; dx <= r; dx++) {
            if (dx * dx + dy * dy <= r * r) {
                int x = cx + dx;
                int y = cy + dy;
                if (x >= 0 && x < map_w && y >= 0 && y < map_h) {
                    canvas_buf[y * map_w + x] = color;
                }
            }
        }
    }
}

static void draw_hline_dashed(int y, uint8_t color) {
    if (y < 0 || y >= map_h) return;
    for (int x = 0; x < map_w; x++) {
        if ((x / 4) % 2 == 0) canvas_buf[y * map_w + x] = color;
    }
}

static void draw_vline_dashed(int x, uint8_t color) {
    if (x < 0 || x >= map_w) return;
    for (int y = 0; y < map_h; y++) {
        if ((y / 4) % 2 == 0) canvas_buf[y * map_w + x] = color;
    }
}

static void draw_hline(int y, uint8_t color) {
    if (y < 0 || y >= map_h) return;
    memset(&canvas_buf[y * map_w], color, map_w);
}

static void draw_vline(int x, uint8_t color) {
    if (x < 0 || x >= map_w) return;
    for (int y = 0; y < map_h; y++) {
        canvas_buf[y * map_w + x] = color;
    }
}

static void draw_hline_dashed_thick(int y, int thickness, uint8_t color) {
    int half = thickness / 2;
    for (int dy = -half; dy <= half; dy++) {
        draw_hline_dashed(y + dy, color);
    }
}

static void draw_vline_dashed_thick(int x, int thickness, uint8_t color) {
    int half = thickness / 2;
    for (int dx = -half; dx <= half; dx++) {
        draw_vline_dashed(x + dx, color);
    }
}

static void draw_hline_thick(int y, int thickness, uint8_t color) {
    int half = thickness / 2;
    for (int dy = -half; dy <= half; dy++) {
        draw_hline(y + dy, color);
    }
}

static void draw_vline_thick(int x, int thickness, uint8_t color) {
    int half = thickness / 2;
    for (int dx = -half; dx <= half; dx++) {
        draw_vline(x + dx, color);
    }
}

static void hide_contact_overlays() {
    for (int i = 0; i < 32; i++) {
        if (contact_taps[i]) lv_obj_add_flag(contact_taps[i], LV_OBJ_FLAG_HIDDEN);
        if (contact_name_labels[i]) lv_obj_add_flag(contact_name_labels[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static void rebuild_map() {
    if (!canvas_obj || !canvas_buf || map_w <= 0 || map_h <= 0) return;

    double my_lat = model::gps.lat;
    double my_lon = model::gps.lng;
    double zoom_km = zoom_levels[zoom_idx];

    if (lbl_zoom) {
        static char zbuf[16];
        if (zoom_km < 1.0)
            snprintf(zbuf, sizeof(zbuf), "%dm", (int)(zoom_km * 1000));
        else
            snprintf(zbuf, sizeof(zbuf), "%.0fkm", zoom_km);
        lv_label_set_text(lbl_zoom, zbuf);
    }

    memset(canvas_buf, map_bg_color(), canvas_buf_size);

    double scale_y = map_h / (zoom_km * 2.0);
    double scale_x = map_w / (zoom_km * 2.0);
    double cos_lat = cos(my_lat * ui::geo::DEG_TO_RAD);
    if (cos_lat < 0.01) cos_lat = 0.01;

    double grid_km;
    if (zoom_km <= 0.5)      grid_km = 0.1;
    else if (zoom_km <= 1.0) grid_km = 0.25;
    else if (zoom_km <= 5.0) grid_km = 1.0;
    else if (zoom_km <= 20.0) grid_km = 5.0;
    else                     grid_km = 10.0;

    int grid_px_y = (int)(grid_km * scale_y);
    int grid_px_x = (int)(grid_km * scale_x);
    if (grid_px_y > 20 && grid_px_x > 20) {
        for (int gy = grid_px_y; map_cy + gy < map_h; gy += grid_px_y) {
            draw_hline_dashed_thick(map_cy + gy, UI_BORDER_CARD, map_grid_color());
            draw_hline_dashed_thick(map_cy - gy, UI_BORDER_CARD, map_grid_color());
        }
        for (int gx = grid_px_x; map_cx + gx < map_w; gx += grid_px_x) {
            draw_vline_dashed_thick(map_cx + gx, UI_BORDER_CARD, map_grid_color());
            draw_vline_dashed_thick(map_cx - gx, UI_BORDER_CARD, map_grid_color());
        }
    }

    draw_hline(map_cy, map_axis_color());
    draw_vline(map_cx, map_axis_color());
    draw_filled_circle(map_cx, map_cy, 6, map_marker_color());

    hide_contact_overlays();

    static char grid_str[16];
    if (grid_km < 1.0)
        snprintf(grid_str, sizeof(grid_str), "grid: %dm", (int)(grid_km * 1000));
    else
        snprintf(grid_str, sizeof(grid_str), "grid: %.0fkm", grid_km);
    if (grid_label) {
        lv_label_set_text(grid_label, grid_str);
        lv_obj_clear_flag(grid_label, LV_OBJ_FLAG_HIDDEN);
    }

    if (!model::gps.has_fix) {
        if (no_fix_label) {
            lv_obj_clear_flag(no_fix_label, LV_OBJ_FLAG_HIDDEN);
        }
        lv_obj_invalidate(canvas_obj);
        return;
    }

    if (no_fix_label) {
        lv_obj_add_flag(no_fix_label, LV_OBJ_FLAG_HIDDEN);
    }

    for (int i = 0; i < contact_count; i++) {
        double dlat = contacts[i].lat - my_lat;
        double dlon = contacts[i].lon - my_lon;

        double dy_km = dlat * ui::geo::KM_PER_DEG_LAT;
        double dx_km = dlon * ui::geo::KM_PER_DEG_LAT * cos_lat;

        double dist = sqrt(dx_km * dx_km + dy_km * dy_km);
        if (dist > zoom_km) {
            contacts[i].px = -1;
            continue;
        }

        int px = map_cx + (int)(dx_km * scale_x);
        int py = map_cy - (int)(dy_km * scale_y);

        if (px < 10 || px > map_w - 10 || py < 10 || py > map_h - 30) {
            contacts[i].px = -1;
            continue;
        }

        contacts[i].px = px;
        contacts[i].py = py;

        draw_filled_circle(px, py, 8, map_marker_color());

        char short_name[10];
        strncpy(short_name, contacts[i].name, 9);
        short_name[9] = 0;

        if (contact_taps[i]) {
            lv_obj_set_pos(contact_taps[i], px - 25, py - 25);
            lv_obj_clear_flag(contact_taps[i], LV_OBJ_FLAG_HIDDEN);
        }
        if (contact_name_labels[i]) {
            lv_label_set_text(contact_name_labels[i], short_name);
            lv_obj_set_pos(contact_name_labels[i], px - 30, py + 12);
            lv_obj_clear_flag(contact_name_labels[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_obj_invalidate(canvas_obj);
}

void toggle_fullscreen() {
    if (!scr) return;
    fullscreen = !fullscreen;

    // Nav bar is the first child of screen_obj (parent of content container)
    lv_obj_t* screen_obj = lv_obj_get_parent(scr);
    lv_obj_t* nav_obj = screen_obj ? lv_obj_get_child(screen_obj, 0) : NULL;

    if (fullscreen) {
        ui::statusbar::hide();
        if (nav_obj) lv_obj_add_flag(nav_obj, LV_OBJ_FLAG_HIDDEN);
        // Expand content to full screen
        lv_obj_set_style_pad_top(screen_obj, 0, LV_PART_MAIN);
    } else {
        ui::statusbar::show();
        if (nav_obj) lv_obj_clear_flag(nav_obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_style_pad_top(screen_obj, UI_STATUSBAR_BOTTOM, LV_PART_MAIN);
    }

    // Resize canvas to new content dimensions
    lv_obj_t* map_content = canvas_obj ? lv_obj_get_parent(canvas_obj) : scr;
    lv_obj_update_layout(screen_obj);
    int new_w = lv_obj_get_content_width(map_content);
    int new_h = lv_obj_get_content_height(map_content);
    if (new_w != map_w || new_h != map_h) {
        map_w = new_w;
        map_h = new_h;
        map_cx = map_w / 2;
        map_cy = map_h / 2;
        size_t needed = (size_t)map_w * (size_t)map_h;
        if (!canvas_buf || canvas_buf_size != needed) {
            if (canvas_buf) heap_caps_free(canvas_buf);
            canvas_buf = (uint8_t*)heap_caps_malloc(needed, MALLOC_CAP_SPIRAM);
            canvas_buf_size = canvas_buf ? needed : 0;
        }
        if (canvas_obj) {
            lv_canvas_set_buffer(canvas_obj, canvas_buf, map_w, map_h, LV_COLOR_FORMAT_L8);
        }
        rebuild_map();
    }
}

void process_events() {
    if (!canvas_obj) return;

    bool gps_changed = (last_gps_fix != model::gps.has_fix) ||
                       (fabs(last_gps_lat - model::gps.lat) > 0.00001) ||
                       (fabs(last_gps_lon - model::gps.lng) > 0.00001);
    bool contacts_changed = last_contacts_revision != model::contacts_revision;
    if (!gps_changed && !contacts_changed) return;

    if (contacts_changed) {
        load_contacts();
        last_contacts_revision = model::contacts_revision;
    }
    rebuild_map();
    last_gps_fix = model::gps.has_fix;
    last_gps_lat = model::gps.lat;
    last_gps_lon = model::gps.lng;
}

static void create(ui::kit::Handle parent_kit) {
    lv_obj_t* parent = (lv_obj_t*)parent_kit;
    scr = parent;
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* content = ui::nav::content_area(parent);
    lv_obj_set_style_bg_color(content, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, UI_BORDER_THIN, LV_PART_MAIN);
    lv_obj_set_style_border_color(content, lv_color_hex(EPD_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(content, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_clear_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_update_layout(content);

    map_w = lv_obj_get_content_width(content);
    map_h = lv_obj_get_content_height(content);
    map_cx = map_w / 2;
    map_cy = map_h / 2;

    size_t needed = (size_t)map_w * (size_t)map_h;
    if (!canvas_buf || canvas_buf_size != needed) {
        if (canvas_buf) heap_caps_free(canvas_buf);
        canvas_buf = (uint8_t*)heap_caps_malloc(needed, MALLOC_CAP_SPIRAM);
        canvas_buf_size = canvas_buf ? needed : 0;
    }

    canvas_obj = lv_canvas_create(content);
    lv_obj_set_size(canvas_obj, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(canvas_obj, 0, 0);
    lv_canvas_set_buffer(canvas_obj, canvas_buf, map_w, map_h, LV_COLOR_FORMAT_L8);

    tap_layer = lv_obj_create(content);
    lv_obj_set_size(tap_layer, lv_pct(100), lv_pct(100));
    lv_obj_set_pos(tap_layer, 0, 0);
    lv_obj_set_style_bg_opa(tap_layer, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(tap_layer, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(tap_layer, 0, LV_PART_MAIN);
    lv_obj_clear_flag(tap_layer, LV_OBJ_FLAG_SCROLLABLE);

    grid_label = lv_label_create(tap_layer);
    lv_obj_set_style_text_font(grid_label, UI_FONT_SMALL, LV_PART_MAIN);
    style_overlay_label(grid_label);
    lv_obj_set_style_pad_all(grid_label, 3, LV_PART_MAIN);
    lv_obj_set_pos(grid_label, 5, 5);

    no_fix_label = lv_label_create(tap_layer);
    lv_obj_set_style_text_font(no_fix_label, UI_FONT_BODY, LV_PART_MAIN);
    style_overlay_label(no_fix_label);
    lv_obj_set_style_text_align(no_fix_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(no_fix_label, 6, LV_PART_MAIN);
    lv_obj_set_width(no_fix_label, map_w);
    lv_label_set_text(no_fix_label, "Waiting for GPS fix...");
    lv_obj_set_pos(no_fix_label, 0, map_cy + 30);
    lv_obj_add_flag(no_fix_label, LV_OBJ_FLAG_HIDDEN);

    for (int i = 0; i < 32; i++) {
        contact_taps[i] = lv_obj_create(tap_layer);
        lv_obj_set_size(contact_taps[i], 50, 50);
        lv_obj_set_style_bg_opa(contact_taps[i], LV_OPA_0, LV_PART_MAIN);
        lv_obj_set_style_border_width(contact_taps[i], 0, LV_PART_MAIN);
        lv_obj_add_flag(contact_taps[i], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(contact_taps[i], 15);
        lv_obj_add_event_cb(contact_taps[i], on_contact_tap, LV_EVENT_CLICKED, (void*)(intptr_t)i);
        lv_obj_add_flag(contact_taps[i], LV_OBJ_FLAG_HIDDEN);
        ui::port::keyboard_focus_register(contact_taps[i]);

        contact_name_labels[i] = lv_label_create(tap_layer);
        lv_obj_set_style_text_font(contact_name_labels[i], UI_FONT_SMALL, LV_PART_MAIN);
        style_overlay_label(contact_name_labels[i]);
        lv_obj_set_style_pad_all(contact_name_labels[i], 2, LV_PART_MAIN);
        lv_obj_add_flag(contact_name_labels[i], LV_OBJ_FLAG_HIDDEN);
    }

    lv_obj_t* btn_in = ui::nav::text_button(tap_layer, "+", on_zoom_in, NULL);
    lv_obj_set_size(btn_in, UI_MAP_BTN_W, UI_MAP_BTN_H);
    lv_obj_align(btn_in, LV_ALIGN_BOTTOM_LEFT, 6, -6);

    lbl_zoom = lv_label_create(tap_layer);
    lv_obj_set_style_text_font(lbl_zoom, UI_FONT_TITLE, LV_PART_MAIN);
    style_overlay_label(lbl_zoom);
    lv_obj_set_style_pad_hor(lbl_zoom, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(lbl_zoom, 2, LV_PART_MAIN);
    lv_obj_align(lbl_zoom, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_label_set_text(lbl_zoom, "5km");

    lv_obj_t* btn_out = ui::nav::text_button(tap_layer, "-", on_zoom_out, NULL);
    lv_obj_set_size(btn_out, UI_MAP_BTN_W, UI_MAP_BTN_H);
    lv_obj_align(btn_out, LV_ALIGN_BOTTOM_RIGHT, -6, -6);

    load_contacts();
    rebuild_map();
}

static void entry() {
    last_contacts_revision = 0;
    last_gps_fix = !model::gps.has_fix;
    last_gps_lat = 0.0;
    last_gps_lon = 0.0;
    model::refresh_contacts();
    process_events();
}

static void exit_fn() {
    if (fullscreen) {
        ui::statusbar::show();
        fullscreen = false;
    }
}

static void destroy() {
    scr = NULL;
    canvas_obj = NULL;
    tap_layer = NULL;
    lbl_zoom = NULL;
    grid_label = NULL;
    no_fix_label = NULL;
    map_w = 0;
    map_h = 0;
    map_cx = 0;
    map_cy = 0;
    contact_count = 0;
    last_contacts_revision = 0;
    last_gps_lat = 0.0;
    last_gps_lon = 0.0;
    last_gps_fix = false;
    for (int i = 0; i < 32; i++) {
        contact_taps[i] = NULL;
        contact_name_labels[i] = NULL;
    }
    // canvas_buf persists — reused on next map open, freed only on reboot
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::map
