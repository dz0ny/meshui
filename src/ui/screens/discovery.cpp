#include "discovery.h"
#include "contact_detail.h"
#include "../ui_theme.h"
#include "../ui_port.h"
#include "../ui_screen_mgr.h"
#include "../components/nav_button.h"
#include "../components/text_utils.h"
#include "../../model.h"

namespace ui::screen::discovery {

static lv_obj_t* scr = NULL;
static lv_obj_t* node_list = NULL;
static lv_obj_t* lbl_filter = NULL;
static lv_obj_t* node_rows[16] = {};
static lv_obj_t* node_row_labels[16] = {};
static lv_obj_t* node_row_values[16] = {};
static int row_node_idx[16] = {};
static bool row_visible[16] = {};
static lv_obj_t* empty_label = NULL;
static uint32_t last_discovery_revision = 0;
static int filter_mode = 0;
static const char* filter_names[] = {"New", "Added", "All"};

static void rebuild_list();

static void on_back(lv_event_t* e) { ui::screen_mgr::pop(true); }

static void on_node_click(lv_event_t* e) {
    int row_idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (row_idx >= 0 && row_idx < model::discovery_count) {
        int idx = row_node_idx[row_idx];
        if (idx < 0 || idx >= model::discovery_count) return;

        char clean_name[32];
        strncpy(clean_name, model::discovery[idx].name, sizeof(clean_name) - 1);
        clean_name[31] = 0;
        ui::text::strip_emoji(clean_name);

        ui::screen::contact_detail::set_contact(
            clean_name, 0, 0, 0, false, model::discovery[idx].pubkey_prefix);
        ui::screen_mgr::push(SCREEN_CONTACT_DETAIL, true);
    }
}

static bool passes_filter(const model::DiscoveryEntry& node) {
    bool already_added = model::find_contact_by_prefix(node.pubkey_prefix) != NULL;
    switch (filter_mode) {
        case 0: return !already_added;
        case 1: return already_added;
        case 2: return true;
        default: return true;
    }
}

static void on_filter_cycle(lv_event_t* e) {
    filter_mode = (filter_mode + 1) % 3;
    if (lbl_filter) lv_label_set_text(lbl_filter, filter_names[filter_mode]);
    rebuild_list();
}

static void ensure_row(int idx) {
    if (!node_list || idx < 0 || idx >= 16 || node_rows[idx]) return;

    lv_obj_t* value = ui::nav::toggle_item(node_list, "", "", on_node_click, (void*)(intptr_t)idx);
    lv_obj_t* hit = lv_obj_get_parent(value);
    lv_obj_t* row = hit ? lv_obj_get_child(hit, 0) : NULL;
    lv_obj_t* label = row ? lv_obj_get_child(row, 0) : NULL;

    node_rows[idx] = hit;
    node_row_labels[idx] = label;
    node_row_values[idx] = value;
    row_node_idx[idx] = -1;
    row_visible[idx] = false;
    lv_obj_add_flag(hit, LV_OBJ_FLAG_HIDDEN);
}

static void rebuild_list() {
    if (!node_list) return;
    lv_display_t* disp = lv_obj_get_display(node_list);
    lv_display_enable_invalidation(disp, false);

    if (lbl_filter) {
        lv_label_set_text(lbl_filter, filter_names[filter_mode]);
    }

    for (int i = 0; i < 16; i++) {
        ensure_row(i);
    }

    int shown = 0;

    for (int i = 0; i < model::discovery_count; i++) {
        if (!passes_filter(model::discovery[i])) continue;
        char clean_name[32];
        strncpy(clean_name, model::discovery[i].name, sizeof(clean_name) - 1);
        clean_name[31] = 0;
        ui::text::strip_emoji(clean_name);

        bool already_added = model::find_contact_by_prefix(model::discovery[i].pubkey_prefix) != NULL;
        lv_label_set_text(node_row_labels[shown], clean_name);
        lv_label_set_text(node_row_values[shown], already_added ? LV_SYMBOL_OK : "+Add");
        if (!row_visible[shown]) {
            lv_obj_clear_flag(node_rows[shown], LV_OBJ_FLAG_HIDDEN);
            row_visible[shown] = true;
        }
        row_node_idx[shown] = i;
        shown++;
    }

    for (int i = shown; i < 16; i++) {
        if (row_visible[i]) {
            lv_obj_add_flag(node_rows[i], LV_OBJ_FLAG_HIDDEN);
            row_visible[i] = false;
        }
        row_node_idx[i] = -1;
    }

    if (empty_label) {
        if (shown == 0) {
            lv_obj_clear_flag(empty_label, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(empty_label, LV_OBJ_FLAG_HIDDEN);
        }
    }

    lv_display_enable_invalidation(disp, true);
    lv_obj_invalidate(node_list);
    ui::port::keyboard_focus_invalidate();
}

void process_events() {
    if (!node_list) return;
    if (last_discovery_revision == model::discovery_revision) return;
    last_discovery_revision = model::discovery_revision;
    rebuild_list();
}

static void create(ui::kit::Handle parent_kit) {
    lv_obj_t* parent = (lv_obj_t*)parent_kit;
    scr = parent;
    lbl_filter = ui::screen_mgr::set_nav_action(filter_names[filter_mode], on_filter_cycle, NULL);

    node_list = ui::nav::scroll_list(parent);

    empty_label = lv_label_create(node_list);
    lv_obj_set_width(empty_label, lv_pct(100));
    lv_obj_set_flex_grow(empty_label, 1);
    lv_obj_set_style_text_font(empty_label, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(empty_label, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_text_align(empty_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_label_set_text(empty_label, "\n\n\nListening...");
    lv_obj_add_flag(empty_label, LV_OBJ_FLAG_HIDDEN);
}

static void entry() {
    last_discovery_revision = 0;
    model::refresh_discovery();
    process_events();
}

static void exit_fn() {
}

static void destroy() {
    scr = NULL;
    node_list = NULL;
    lbl_filter = NULL;
    empty_label = NULL;
    for (int i = 0; i < 16; i++) {
        node_rows[i] = NULL;
        node_row_labels[i] = NULL;
        node_row_values[i] = NULL;
        row_node_idx[i] = -1;
        row_visible[i] = false;
    }
    last_discovery_revision = 0;
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::discovery
