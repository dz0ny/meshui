#include "contacts.h"
#include "contact_detail.h"
#include "../ui_theme.h"
#include "../ui_port.h"
#include "../ui_screen_mgr.h"
#include "../components/nav_button.h"
#include "../components/text_utils.h"
#include "../../model.h"
#include <helpers/AdvertDataHelpers.h>
#include <cstdio>

namespace ui::screen::contacts {

static lv_obj_t* scr = NULL;
static lv_obj_t* contact_list = NULL;
static lv_obj_t* lbl_filter = NULL;
static lv_obj_t* lbl_count = NULL;
static lv_obj_t* contact_rows[MAX_CONTACTS] = {};
static lv_obj_t* contact_row_labels[MAX_CONTACTS] = {};
static int row_contact_idx[MAX_CONTACTS] = {};
static lv_obj_t* empty_label = NULL;
static uint32_t last_contacts_revision = 0;

// Filter: 0=Chat, 1=Relay, 2=Favorite, 3=All
static int filter_mode = 0;
static const char* filter_names[] = {"Chat", "Relay", "Fav", "All"};

struct DisplayContact {
    char name[32];
    uint8_t pub_key[32];
    uint8_t type;
    uint8_t flags;
    bool has_path;
    int32_t gps_lat;
    int32_t gps_lon;
};
static DisplayContact displayed[MAX_CONTACTS];
static int display_count = 0;

static void on_back(lv_event_t* e) { ui::screen_mgr::pop(true); }
static void on_discovery(lv_event_t* e) { ui::screen_mgr::push(SCREEN_DISCOVERY, true); }

static void on_contact_click(lv_event_t* e) {
    int row_idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (row_idx < 0 || row_idx >= MAX_CONTACTS) return;
    int idx = row_contact_idx[row_idx];
    if (idx >= 0 && idx < display_count) {
        ui::screen::contact_detail::set_contact(
            displayed[idx].name, displayed[idx].gps_lat, displayed[idx].gps_lon,
            displayed[idx].type, displayed[idx].has_path, displayed[idx].pub_key);
        ui::screen_mgr::push(SCREEN_CONTACT_DETAIL, true);
    }
}

static bool passes_filter(const DisplayContact& c) {
    switch (filter_mode) {
        case 0: return (c.type == ADV_TYPE_CHAT || c.type == ADV_TYPE_ROOM);
        case 1: return (c.type == ADV_TYPE_REPEATER);
        case 2: return (c.flags & 0x01) != 0;
        case 3: return true;
        default: return true;
    }
}

static void rebuild_list();

static void on_filter_cycle(lv_event_t* e) {
    filter_mode = (filter_mode + 1) % 4;
    if (lbl_filter) lv_label_set_text(lbl_filter, filter_names[filter_mode]);
    rebuild_list();
}

static void ensure_row(int idx) {
    if (!contact_list || idx < 0 || idx >= MAX_CONTACTS || contact_rows[idx]) return;

    lv_obj_t* hit = ui::nav::menu_item(contact_list, NULL, "", on_contact_click, (void*)(intptr_t)idx);
    lv_obj_t* row = lv_obj_get_child(hit, 0);
    contact_rows[idx] = hit;
    contact_row_labels[idx] = row ? lv_obj_get_child(row, 0) : NULL;
    row_contact_idx[idx] = -1;
}

static void prune_rows(int keep_count) {
    for (int i = keep_count; i < MAX_CONTACTS; i++) {
        if (!contact_rows[i]) continue;
        lv_obj_delete(contact_rows[i]);
        contact_rows[i] = NULL;
        contact_row_labels[i] = NULL;
        row_contact_idx[i] = -1;
    }
}

static void load_contacts_from_model() {
    display_count = 0;
    for (int i = 0; i < model::contact_count && display_count < MAX_CONTACTS; i++) {
        const model::ContactEntry& src = model::contacts[i];
        strncpy(displayed[display_count].name, src.name, sizeof(displayed[display_count].name) - 1);
        displayed[display_count].name[sizeof(displayed[display_count].name) - 1] = 0;
        ui::text::strip_emoji(displayed[display_count].name);
        memcpy(displayed[display_count].pub_key, src.pub_key, sizeof(displayed[display_count].pub_key));
        displayed[display_count].type = src.type;
        displayed[display_count].flags = src.flags;
        displayed[display_count].has_path = src.has_path;
        displayed[display_count].gps_lat = src.gps_lat;
        displayed[display_count].gps_lon = src.gps_lon;
        display_count++;
    }
}

static void rebuild_list() {
    if (!contact_list) return;
    lv_display_t* disp = lv_obj_get_display(contact_list);
    lv_display_enable_invalidation(disp, false);

    if (lbl_filter) {
        lv_label_set_text(lbl_filter, filter_names[filter_mode]);
    }

    int shown = 0;
    for (int i = 0; i < display_count; i++) {
        if (!passes_filter(displayed[i])) continue;
        ensure_row(shown);
        const char* icon = (displayed[i].flags & 0x01) ? "\xE2\x98\x85 " : "";
        char label[40];
        snprintf(label, sizeof(label), "%s%s", icon, displayed[i].name);
        if (contact_row_labels[shown]) {
            lv_label_set_text(contact_row_labels[shown], label);
        }
        row_contact_idx[shown] = i;
        shown++;
    }

    prune_rows(shown);

    if (lbl_count) {
        lv_label_set_text_fmt(lbl_count, "%d contact%s", shown, shown == 1 ? "" : "s");
    }

    if (empty_label) {
        if (shown == 0) {
            lv_obj_clear_flag(empty_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(empty_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_display_enable_invalidation(disp, true);
    lv_obj_invalidate(contact_list);
    ui::port::keyboard_focus_invalidate();
}

void process_events() {
    if (!contact_list) return;
    if (last_contacts_revision == model::contacts_revision) return;

    load_contacts_from_model();
    last_contacts_revision = model::contacts_revision;
    rebuild_list();
}

static void create(ui::kit::Handle parent_kit) {
    lv_obj_t* parent = (lv_obj_t*)parent_kit;
    scr = parent;
    lbl_filter = ui::screen_mgr::set_nav_actions(filter_names[filter_mode], on_filter_cycle, NULL,
                                                 "Discover", on_discovery, NULL);
    contact_list = ui::nav::scroll_list(parent);

    lbl_count = lv_label_create(contact_list);
    lv_obj_set_width(lbl_count, lv_pct(100));
    lv_obj_set_style_text_font(lbl_count, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_count, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(lbl_count, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(lbl_count, "0 contacts");

    empty_label = lv_label_create(contact_list);
    lv_obj_set_width(empty_label, lv_pct(100));
    lv_obj_set_flex_grow(empty_label, 1);
    lv_obj_set_style_text_font(empty_label, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(empty_label, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(empty_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(empty_label, "\n\n\nNo contacts");
    lv_obj_add_flag(empty_label, LV_OBJ_FLAG_HIDDEN);

    rebuild_list();
}

static void entry() {
    last_contacts_revision = 0;
    model::refresh_contacts();
    process_events();
}

static void exit_fn() {
}

static void destroy() {
    scr = NULL;
    contact_list = NULL;
    lbl_filter = NULL;
    lbl_count = NULL;
    empty_label = NULL;
    last_contacts_revision = 0;
    for (int i = 0; i < MAX_CONTACTS; i++) {
        contact_rows[i] = NULL;
        contact_row_labels[i] = NULL;
        row_contact_idx[i] = -1;
    }
}

screen_lifecycle_t lifecycle = {
    .create  = create,
    .entry   = entry,
    .exit    = exit_fn,
    .destroy = destroy,
};

} // namespace ui::screen::contacts
