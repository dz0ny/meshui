#include <cmath>
#include <cstring>
#include <cstdio>
#include <cctype>
#include "contact_detail.h"
#include "ping.h"
#include "compose.h"
#include "../ui_theme.h"
#include "../ui_screen_mgr.h"
#include "../components/nav_button.h"
#include "../components/geo_utils.h"
#include "../../model.h"
#include "../../mesh/mesh_task.h"
#include <helpers/AdvertDataHelpers.h>
#include "../components/toast.h"

namespace ui::screen::contact_detail {

static lv_obj_t* scr = NULL;
static lv_obj_t* content_list = NULL;

// Contact data (set before pushing screen)
static char contact_name[32];
static int32_t contact_lat = 0;  // scaled 1e6
static int32_t contact_lon = 0;
static uint8_t contact_type = 0;
static bool contact_has_path = false;
static uint8_t contact_pubkey[7] = {};
static bool has_pubkey = false;
static bool contact_is_favorite = false;

// UI widgets
static lv_obj_t* lbl_coords = NULL;
static lv_obj_t* compass_canvas = NULL;
static lv_obj_t* lbl_nav_action = NULL;
#define SYMBOL_STAR_FILLED   "\xE2\x98\x85" /* U+2605 ★ */
#define SYMBOL_STAR_EMPTY    "\xE2\x98\x86" /* U+2606 ☆ */
static const char* favorite_action_label(bool is_favorite) { return is_favorite ? SYMBOL_STAR_FILLED " Fav" : SYMBOL_STAR_EMPTY " Fav"; }

// Live fast-GPS beacon position for a contact, matched by the first 6 pub-key
// bytes. Same lookup the team/map screens use, so the card stays consistent.
static const model::LivePosition* find_live(const uint8_t* pub_key) {
    for (int i = 0; i < model::live_position_count; i++) {
        const model::LivePosition& p = model::live_positions[i];
        if (p.valid && memcmp(p.pub_key_prefix, pub_key, 6) == 0) return &p;
    }
    return nullptr;
}

// "now" / "5m" / "2h" / "3d" — relative to model::epoch_now. Empty if unknown.
static void fmt_age(char* buf, size_t n, uint32_t ts) {
    if (ts == 0 || model::epoch_now == 0 || model::epoch_now < ts) { buf[0] = 0; return; }
    uint32_t s = model::epoch_now - ts;
    if (s < 60)         snprintf(buf, n, "now");
    else if (s < 3600)  snprintf(buf, n, "%lum", (unsigned long)(s / 60));
    else if (s < 86400) snprintf(buf, n, "%luh", (unsigned long)(s / 3600));
    else                snprintf(buf, n, "%lud", (unsigned long)(s / 86400));
}

static const char* contact_type_label(uint8_t type) {
    switch (type) {
        case ADV_TYPE_CHAT: return "Direct chat";
        case ADV_TYPE_ROOM: return "Room";
        case ADV_TYPE_SENSOR: return "Sensor";
        case ADV_TYPE_REPEATER: return "Repeater";
        default: return "Mesh contact";
    }
}

static lv_obj_t* create_card(lv_obj_t* parent, lv_coord_t height) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    if (height > 0) {
        lv_obj_set_height(card, height);
    } else {
        lv_obj_set_height(card, LV_SIZE_CONTENT);
    }
    lv_obj_set_style_bg_color(card, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, UI_BORDER_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(EPD_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(card, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 10, LV_PART_MAIN);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t* create_meta_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(label, text);
    return label;
}

static lv_obj_t* create_value_label(lv_obj_t* parent, const char* text) {
    lv_obj_t* label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_width(label, lv_pct(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    return label;
}

static void create_detail_row(lv_obj_t* parent, const char* title, const char* value) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(row, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(row, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* title_label = lv_label_create(row);
    lv_obj_set_style_text_font(title_label, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(title_label, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(title_label, title);

    lv_obj_t* value_label = lv_label_create(row);
    lv_obj_set_width(value_label, lv_pct(55));
    lv_obj_set_style_text_font(value_label, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(value_label, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(value_label, LV_TEXT_ALIGN_RIGHT, LV_PART_MAIN);
    lv_label_set_long_mode(value_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(value_label, value);
}

static void contact_avatar_text(char* out, size_t out_size, const char* name) {
    if (!out || out_size < 3) return;

    out[0] = '?';
    out[1] = '?';
    out[2] = 0;

    size_t found = 0;
    bool take_next = true;
    for (size_t i = 0; name && name[i] && found < 2; i++) {
        unsigned char ch = (unsigned char)name[i];
        if (std::isalnum(ch) && take_next) {
            out[found++] = (char)std::toupper(ch);
            take_next = false;
        } else if (std::isspace(ch) || ch == '-' || ch == '_' || ch == '.') {
            take_next = true;
        }
    }

    if (found == 1) {
        out[1] = out[0];
    }
}

void set_contact(const char* name, int32_t gps_lat, int32_t gps_lon, uint8_t type, bool has_path,
                 const uint8_t* pubkey_prefix) {
    strncpy(contact_name, name, sizeof(contact_name) - 1);
    contact_name[31] = 0;
    contact_lat = gps_lat;
    contact_lon = gps_lon;
    contact_type = type;
    contact_has_path = has_path;
    if (pubkey_prefix) {
        memcpy(contact_pubkey, pubkey_prefix, 7);
        has_pubkey = true;
    } else {
        has_pubkey = false;
    }
}

// Draw compass rose with direction arrow using LVGL line objects
static void draw_compass(lv_obj_t* parent, double bearing_deg) {
    const int compass_size = 280;
    const int center = 140;
    const int label_radius = 105;
    const int arrow_len = 90;
    const int head_len = 20;
    const int center_dot = 12;
    const int shaft_width = 6;
    const int head_width = 5;

    // Compass container
    lv_obj_t* compass = lv_obj_create(parent);
    lv_obj_set_size(compass, compass_size, compass_size);
    lv_obj_set_style_bg_color(compass, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_border_width(compass, UI_BORDER_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_color(compass, lv_color_hex(EPD_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(compass, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_pad_all(compass, 0, LV_PART_MAIN);
    lv_obj_clear_flag(compass, LV_OBJ_FLAG_SCROLLABLE);

    int cx = center, cy = center;

    // Cardinal direction labels
    const char* dirs[] = {"N", "E", "S", "W"};
    int dx[] = {0, 1, 0, -1};
    int dy[] = {-1, 0, 1, 0};
    for (int i = 0; i < 4; i++) {
        lv_obj_t* lbl = lv_label_create(compass);
        lv_obj_set_style_text_font(lbl, UI_FONT_TITLE, LV_PART_MAIN);
        lv_obj_set_style_text_color(lbl, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
        lv_label_set_text(lbl, dirs[i]);
        lv_obj_align(lbl, LV_ALIGN_CENTER, dx[i] * label_radius, dy[i] * label_radius);
    }

    // Direction arrow — thick line from center toward the bearing
    double rad = bearing_deg * ui::geo::DEG_TO_RAD;
    int tip_x = cx + (int)(sin(rad) * arrow_len);
    int tip_y = cy - (int)(cos(rad) * arrow_len);

    // Arrow shaft
    static lv_point_precise_t shaft_pts[2];
    shaft_pts[0] = {(lv_value_precise_t)cx, (lv_value_precise_t)cy};
    shaft_pts[1] = {(lv_value_precise_t)tip_x, (lv_value_precise_t)tip_y};

    lv_obj_t* shaft = lv_line_create(compass);
    lv_line_set_points(shaft, shaft_pts, 2);
    lv_obj_set_style_line_width(shaft, shaft_width, LV_PART_MAIN);
    lv_obj_set_style_line_color(shaft, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);

    // Arrowhead — two short lines from tip
    double head_angle = 0.5;  // ~30 degrees
    static lv_point_precise_t head1_pts[2];
    static lv_point_precise_t head2_pts[2];

    head1_pts[0] = {(lv_value_precise_t)tip_x, (lv_value_precise_t)tip_y};
    head1_pts[1] = {
        (lv_value_precise_t)(tip_x - (int)(sin(rad - head_angle) * head_len)),
        (lv_value_precise_t)(tip_y + (int)(cos(rad - head_angle) * head_len))
    };
    head2_pts[0] = {(lv_value_precise_t)tip_x, (lv_value_precise_t)tip_y};
    head2_pts[1] = {
        (lv_value_precise_t)(tip_x - (int)(sin(rad + head_angle) * head_len)),
        (lv_value_precise_t)(tip_y + (int)(cos(rad + head_angle) * head_len))
    };

    lv_obj_t* h1 = lv_line_create(compass);
    lv_line_set_points(h1, head1_pts, 2);
    lv_obj_set_style_line_width(h1, head_width, LV_PART_MAIN);
    lv_obj_set_style_line_color(h1, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);

    lv_obj_t* h2 = lv_line_create(compass);
    lv_line_set_points(h2, head2_pts, 2);
    lv_obj_set_style_line_width(h2, head_width, LV_PART_MAIN);
    lv_obj_set_style_line_color(h2, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);

    // Small dot at center
    lv_obj_t* dot = lv_obj_create(compass);
    lv_obj_set_size(dot, center_dot, center_dot);
    lv_obj_align(dot, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_border_width(dot, 0, LV_PART_MAIN);

    compass_canvas = compass;
}

static void on_back(lv_event_t* e) { ui::screen_mgr::pop(true); }

static void on_add(lv_event_t* e) {
    if (has_pubkey) {
        bool ok = mesh::task::add_contact_by_prefix(contact_pubkey);
        model::refresh_contacts();
        model::refresh_discovery();
        ui::toast::show(ok ? "Contact added" : "Add failed");
    }
    ui::screen_mgr::pop(true);
}

static void on_remove(lv_event_t* e) {
    if (has_pubkey) {
        mesh::task::remove_contact_by_prefix(contact_pubkey);
        model::refresh_contacts();
        model::refresh_discovery();
        ui::toast::show("Contact removed");
    }
    ui::screen_mgr::pop(true);
}

static void on_favorite(lv_event_t* e) {
    if (has_pubkey) {
        contact_is_favorite = mesh::task::toggle_favorite(contact_pubkey);
        model::refresh_contacts();
        if (lbl_nav_action) {
            lv_label_set_text(lbl_nav_action, favorite_action_label(contact_is_favorite));
        }
        ui::toast::show(contact_is_favorite ? "Added to favorites" : "Removed from favorites");
    }
}

static void on_send_message(lv_event_t* e) {
    ui::screen::compose::set_recipient(contact_name);
    ui::screen_mgr::push(SCREEN_COMPOSE, true);
}

static void on_ping(lv_event_t* e) {
    ui::screen::ping::set_contact(contact_name, contact_lat, contact_lon, contact_type, contact_has_path, contact_pubkey);
    ui::screen_mgr::push(SCREEN_PING, true);
}

static void create(ui::kit::Handle parent_kit) {
    lv_obj_t* parent = (lv_obj_t*)parent_kit;
    scr = parent;
    bool is_existing = has_pubkey && mesh::task::is_contact(contact_pubkey);
    contact_is_favorite = is_existing && mesh::task::is_favorite(contact_pubkey);

    if (is_existing) {
        lbl_nav_action = ui::screen_mgr::set_nav_action(
            favorite_action_label(contact_is_favorite), on_favorite, NULL);
    } else {
        lbl_nav_action = NULL;
    }

    content_list = ui::nav::scroll_list(parent);
    lv_obj_set_style_pad_row(content_list, UI_MENU_ITEM_PAD, LV_PART_MAIN);

    // Prefer a fresh fast-GPS beacon for this node; fall back to the location
    // carried in its advert (contact_lat/lon — left untouched in storage). The
    // beacon is what the Map/compass track live, so the card should agree.
    const model::LivePosition* live = has_pubkey ? find_live(contact_pubkey) : nullptr;
    bool pos_is_live = live && (live->lat_e6 != 0 || live->lon_e6 != 0);
    int32_t pos_lat = pos_is_live ? live->lat_e6 : contact_lat;
    int32_t pos_lon = pos_is_live ? live->lon_e6 : contact_lon;

    double c_lat = pos_lat / 1e6;
    double c_lon = pos_lon / 1e6;
    bool has_contact_gps = (pos_lat != 0 || pos_lon != 0);
    bool has_own_gps = model::gps.has_fix;
    const char* route_text = contact_has_path ? "Direct" : "Unknown";
    const char* type_text = contact_type_label(contact_type);

    lv_obj_t* hero_card = create_card(content_list, UI_CONTACT_HERO_H);
    lv_obj_t* hero_badge = lv_obj_create(hero_card);
    lv_obj_set_size(hero_badge, UI_CONTACT_BADGE_SZ, UI_CONTACT_BADGE_SZ);
    lv_obj_align(hero_badge, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_bg_color(hero_badge, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_border_width(hero_badge, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(hero_badge, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_clear_flag(hero_badge, LV_OBJ_FLAG_SCROLLABLE);

    char initial_buf[3];
    contact_avatar_text(initial_buf, sizeof(initial_buf), contact_name);
    lv_obj_t* hero_initial = lv_label_create(hero_badge);
    lv_obj_set_style_text_font(hero_initial, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(hero_initial, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_label_set_text(hero_initial, initial_buf);
    lv_obj_center(hero_initial);

    lv_obj_t* lbl_name = lv_label_create(hero_card);
    lv_obj_set_width(lbl_name, lv_pct(60));
    lv_obj_set_style_text_font(lbl_name, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_name, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_long_mode(lbl_name, LV_LABEL_LONG_WRAP);
    lv_obj_align(lbl_name, LV_ALIGN_TOP_LEFT, UI_CONTACT_NAME_X, 2);
    lv_label_set_text(lbl_name, contact_name);

    lv_obj_t* lbl_type = create_meta_label(hero_card, type_text);
    lv_obj_align(lbl_type, LV_ALIGN_TOP_LEFT, UI_CONTACT_TYPE_X, UI_CONTACT_TYPE_Y);

    lv_obj_t* details_card = create_card(content_list, 0);
    lv_obj_set_style_pad_row(details_card, 8, LV_PART_MAIN);
    lv_obj_set_flex_flow(details_card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(details_card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

    static char distance_buf[48];
    static char coord_buf[48];
    const char* distance_text = "No position data";
    const char* coords_text = "Location unavailable";
    bool show_compass = false;
    double bearing = 0.0;

    if (has_contact_gps) {
        snprintf(coord_buf, sizeof(coord_buf), "%.4f, %.4f", c_lat, c_lon);
        coords_text = coord_buf;
    }

    if (has_contact_gps && has_own_gps) {
        double dist = ui::geo::distance_km(model::gps.lat, model::gps.lng, c_lat, c_lon);
        bearing = ui::geo::bearing(model::gps.lat, model::gps.lng, c_lat, c_lon);
        if (dist < 1.0) {
            snprintf(distance_buf, sizeof(distance_buf), "%dm %s", (int)(dist * 1000),
                     ui::geo::bearing_to_cardinal(bearing));
        } else {
            snprintf(distance_buf, sizeof(distance_buf), "%.1fkm %s", dist,
                     ui::geo::bearing_to_cardinal(bearing));
        }
        distance_text = distance_buf;
        show_compass = true;
    } else if (has_contact_gps) {
        distance_text = "Waiting for GPS fix";
    }

    create_detail_row(details_card, "Location", coords_text);
    create_detail_row(details_card, "Distance", distance_text);
    if (pos_is_live) {
        char age_buf[16];
        fmt_age(age_buf, sizeof(age_buf), live->timestamp);
        create_detail_row(details_card, "Live fix", age_buf[0] ? age_buf : "now");
    }
    if (has_pubkey) {
        char key_buf[32];
        snprintf(key_buf, sizeof(key_buf), "%02X%02X%02X%02X%02X%02X%02X",
                 contact_pubkey[0], contact_pubkey[1], contact_pubkey[2], contact_pubkey[3],
                 contact_pubkey[4], contact_pubkey[5], contact_pubkey[6]);
        create_detail_row(details_card, "Node key", key_buf);
    }
    create_detail_row(details_card, "Route", route_text);

    if (show_compass) {
        lv_obj_t* compass_card = create_card(content_list, 0);
        lv_obj_set_style_pad_all(compass_card, 8, LV_PART_MAIN);
        draw_compass(compass_card, bearing);
        lv_obj_center(compass_canvas);
    }

    if (has_pubkey) {
        if (is_existing) {
            bool can_chat = (contact_type == ADV_TYPE_CHAT || contact_type == ADV_TYPE_ROOM);
            bool can_ping = has_pubkey;

            lv_obj_t* btn_row = lv_obj_create(content_list);
            lv_obj_set_width(btn_row, lv_pct(100));
            lv_obj_set_height(btn_row, UI_TEXT_BTN_HEIGHT);
            lv_obj_set_style_bg_opa(btn_row, LV_OPA_0, LV_PART_MAIN);
            lv_obj_set_style_border_width(btn_row, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_all(btn_row, 0, LV_PART_MAIN);
            lv_obj_set_style_pad_column(btn_row, UI_MENU_ITEM_PAD, LV_PART_MAIN);
            lv_obj_clear_flag(btn_row, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_set_flex_flow(btn_row, LV_FLEX_FLOW_ROW);
            lv_obj_set_flex_align(btn_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

            if (can_chat) {
                lv_obj_t* msg_btn = ui::nav::text_button(btn_row, "Chat", on_send_message, NULL);
                lv_obj_set_height(msg_btn, lv_pct(100));
                lv_obj_set_flex_grow(msg_btn, 1);
            }
            if (can_ping) {
                lv_obj_t* ping_btn = ui::nav::text_button(btn_row, "Ping", on_ping, NULL);
                lv_obj_set_height(ping_btn, lv_pct(100));
                lv_obj_set_flex_grow(ping_btn, 1);
            }
            lv_obj_t* rm_btn = ui::nav::text_button(btn_row, "Rm", on_remove, NULL);
            lv_obj_set_size(rm_btn, LV_SIZE_CONTENT, lv_pct(100));
            lv_obj_set_flex_grow(rm_btn, 0);
        } else {
            ui::nav::text_button(content_list, "Add", on_add, NULL);
        }
    }
}

static void entry() {}
static void exit_fn() {}
static void destroy() {
    scr = NULL;
    content_list = NULL;
    lbl_coords = NULL;
    compass_canvas = NULL;
    lbl_nav_action = NULL;
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::contact_detail
