#include "settings_debug.h"
#include "../ui_theme.h"
#include "../ui_screen_mgr.h"
#include "../components/nav_button.h"
#include "../components/statusbar.h"
#include "../../nvs_param.h"

namespace ui::screen::settings_debug {

static lv_obj_t* scr = NULL;
static lv_obj_t* lbl_memory = NULL;
static lv_obj_t* lbl_hit_areas = NULL;
static lv_obj_t* lbl_hit_hint = NULL;

static void on_back(lv_event_t* e) { ui::screen_mgr::pop(true); }

static void on_memory_toggle(lv_event_t* e) {
    bool enabled = !ui::statusbar::memory_enabled();
    ui::statusbar::set_memory_enabled(enabled);
    nvs_param_set_u8(NVS_ID_STATUSBAR_MEMORY, enabled ? 1 : 0);
    if (lbl_memory) lv_label_set_text(lbl_memory, enabled ? "On" : "Off");
}

static void sync_hit_hint(bool enabled) {
    if (!lbl_hit_hint) return;
    if (enabled) {
        lv_obj_clear_flag(lbl_hit_hint, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(lbl_hit_hint, LV_OBJ_FLAG_HIDDEN);
    }
}

static void on_hit_area_toggle(lv_event_t* e) {
    bool enabled = !ui::nav::hit_area_debug_enabled();
    ui::nav::set_hit_area_debug(enabled);
    nvs_param_set_u8(NVS_ID_HIT_AREA_DEBUG, enabled ? 1 : 0);
    if (lbl_hit_areas) lv_label_set_text(lbl_hit_areas, enabled ? "On" : "Off");
    sync_hit_hint(enabled);
    ui::screen_mgr::reload_stack();
}

static void create(ui::kit::Handle parent_kit) {
    lv_obj_t* parent = (lv_obj_t*)parent_kit;
    scr = parent;

    lv_obj_t* list = ui::nav::scroll_list(parent);

    lbl_hit_areas = ui::nav::toggle_item(list, "Hit Areas", ui::nav::hit_area_debug_enabled() ? "On" : "Off", on_hit_area_toggle, NULL);
    lbl_hit_hint = lv_label_create(list);
    lv_obj_set_width(lbl_hit_hint, lv_pct(100));
    lv_obj_set_style_text_font(lbl_hit_hint, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_hit_hint, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_pad_left(lbl_hit_hint, UI_MENU_ITEM_INSET, LV_PART_MAIN);
    lv_obj_set_style_pad_right(lbl_hit_hint, UI_MENU_ITEM_INSET, LV_PART_MAIN);
    lv_obj_set_style_pad_top(lbl_hit_hint, 2, LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(lbl_hit_hint, 4, LV_PART_MAIN);
    lv_label_set_long_mode(lbl_hit_hint, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lbl_hit_hint, "Hint: outlined rows show the full touch target.");
    sync_hit_hint(ui::nav::hit_area_debug_enabled());

    lbl_memory = ui::nav::toggle_item(list, "Memory Bar", ui::statusbar::memory_enabled() ? "On" : "Off", on_memory_toggle, NULL);

    ui::nav::menu_item(list, NULL, "Touch Debug", [](lv_event_t* e) {
        ui::screen_mgr::push(SCREEN_TOUCH_DEBUG, true);
    }, NULL);
}

static void entry() {}
static void exit_fn() {}
static void destroy() { scr = NULL; lbl_memory = NULL; lbl_hit_areas = NULL; lbl_hit_hint = NULL; }

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::settings_debug
