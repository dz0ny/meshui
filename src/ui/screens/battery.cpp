#include "battery.h"
#include "../ui_theme.h"
#include "../ui_screen_mgr.h"
#include "../components/nav_button.h"
#include "../../model.h"
#include "../../board.h"

namespace ui::screen::battery {

static lv_obj_t* scr = NULL;
static lv_timer_t* update_timer = NULL;

static lv_obj_t* lbl_percent = NULL;
static lv_obj_t* lbl_voltage = NULL;
static lv_obj_t* lbl_current = NULL;
static lv_obj_t* lbl_temp = NULL;
static lv_obj_t* lbl_remain = NULL;
static lv_obj_t* lbl_full = NULL;
static lv_obj_t* lbl_design = NULL;
static lv_obj_t* lbl_health = NULL;
static lv_obj_t* lbl_chg_status = NULL;
static lv_obj_t* lbl_bus_status = NULL;
static lv_obj_t* lbl_ntc = NULL;
static lv_obj_t* lbl_vbus = NULL;
static lv_obj_t* lbl_vsys = NULL;
static lv_obj_t* lbl_vbat = NULL;
static lv_obj_t* lbl_chg_curr = NULL;

static lv_obj_t* info_row(lv_obj_t* parent, const char* label) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(row, LV_OPA_0, LV_PART_MAIN);
    lv_obj_set_style_border_width(row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_ver(row, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(row, 0, LV_PART_MAIN);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t* lbl = lv_label_create(row);
    lv_obj_set_style_text_font(lbl, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(lbl, label);

    lv_obj_t* val = lv_label_create(row);
    lv_obj_set_style_text_font(val, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(val, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_label_set_text(val, "--");
    return val;
}

static lv_obj_t* section_header(lv_obj_t* parent, const char* text) {
    lv_obj_t* lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_style_pad_top(lbl, 10, LV_PART_MAIN);
    lv_label_set_text(lbl, text);
    return lbl;
}

static void on_back(lv_event_t* e) { ui::screen_mgr::pop(true); }

static void update_cb(lv_timer_t* t) {
    model::update_battery();
    auto& b = model::battery;
    lv_label_set_text_fmt(lbl_percent, "%d%%", b.percent);
    lv_label_set_text_fmt(lbl_voltage, "%d mV", b.voltage_mv);
    lv_label_set_text_fmt(lbl_current, "%d mA", b.current_ma);
    if (lbl_temp) lv_label_set_text_fmt(lbl_temp, "%.1f C", b.temperature_c);
    if (lbl_remain) lv_label_set_text_fmt(lbl_remain, "%d mAh", b.remain_mah);
    if (lbl_full) lv_label_set_text_fmt(lbl_full, "%d mAh", b.full_mah);
    if (lbl_design) lv_label_set_text_fmt(lbl_design, "%d mAh", b.design_mah);
    if (lbl_health) lv_label_set_text_fmt(lbl_health, "%d%%", b.health_pct);

    if (b.charger_ok && lbl_chg_status) {
        lv_label_set_text(lbl_chg_status, b.charge_status ? b.charge_status : "--");
        lv_label_set_text(lbl_bus_status, b.bus_status ? b.bus_status : "--");
        if (lbl_ntc) lv_label_set_text(lbl_ntc, b.ntc_status ? b.ntc_status : "--");
        lv_label_set_text_fmt(lbl_vbus, "%.2f V", b.vbus_v);
        lv_label_set_text_fmt(lbl_vsys, "%.2f V", b.vsys_v);
        lv_label_set_text_fmt(lbl_vbat, "%.2f V", b.vbat_v);
        if (lbl_chg_curr) lv_label_set_text_fmt(lbl_chg_curr, "%.0f mA", b.charge_current_ma);
    }
}

static void create(lv_obj_t* parent) {
    scr = parent;

    lv_obj_t* list = ui::nav::scroll_list(parent);

    section_header(list, "Battery");
    lbl_percent  = info_row(list, "Charge");
    lbl_voltage  = info_row(list, "Voltage");
    lbl_current  = info_row(list, "Current");
    if (board::peri_status[E_PERI_BQ27220]) {
        lbl_temp     = info_row(list, "Temp");
        lbl_remain   = info_row(list, "Remain");
        lbl_full     = info_row(list, "Full Cap");
        lbl_design   = info_row(list, "Design");
        lbl_health   = info_row(list, "Health");
    }

    if (board::peri_status[E_PERI_BQ25896]) {
        section_header(list, "Charger");
        lbl_chg_status = info_row(list, "Status");
        lbl_bus_status = info_row(list, "Bus");
        if (board::peri_status[E_PERI_BQ25896]) {
            lbl_ntc    = info_row(list, "NTC");
        }
        lbl_vbus       = info_row(list, "VBUS");
        lbl_vsys       = info_row(list, "VSYS");
        lbl_vbat       = info_row(list, "VBAT");
        if (board::peri_status[E_PERI_BQ25896]) {
            lbl_chg_curr = info_row(list, "Chg Curr");
        }
    }
}

static void entry() {
    update_timer = lv_timer_create(update_cb, 3000, NULL);
    update_cb(NULL);
}

static void exit_fn() {
    if (update_timer) { lv_timer_del(update_timer); update_timer = NULL; }
}

static void destroy() {
    scr = NULL;
    lbl_percent = lbl_voltage = lbl_current = lbl_temp = NULL;
    lbl_remain = lbl_full = lbl_design = lbl_health = NULL;
    lbl_chg_status = lbl_bus_status = lbl_ntc = NULL;
    lbl_vbus = lbl_vsys = lbl_vbat = lbl_chg_curr = NULL;
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::battery
