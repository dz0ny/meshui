#include "battery.h"
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../i18n.h"
#include "../../model.h"
#ifndef BOARD_WIO_L1
#include "../../board.h"        // board::peri_status / E_PERI_* (LVGL boards only)
#endif

// Battery + charger info — ported to the ui::kit facade.

namespace ui::screen::battery {

using namespace ui::kit;

static Timer  update_timer = nullptr;
static Handle lbl_percent, lbl_voltage, lbl_current, lbl_temp;
static Handle lbl_remain, lbl_full, lbl_design, lbl_health;
static Handle lbl_chg_status, lbl_bus_status, lbl_ntc;
static Handle lbl_vbus, lbl_vsys, lbl_vbat, lbl_chg_curr;

static void update_cb(void*) {
    model::update_battery();
    auto& b = model::battery;
    set_textf(lbl_percent, "%d%%", b.percent);
    set_textf(lbl_voltage, "%d mV", b.voltage_mv);
    set_textf(lbl_current, "%d mA", b.current_ma);
    if (lbl_temp)   set_textf(lbl_temp,   "%.1f C", b.temperature_c);
    if (lbl_remain) set_textf(lbl_remain, "%d mAh", b.remain_mah);
    if (lbl_full)   set_textf(lbl_full,   "%d mAh", b.full_mah);
    if (lbl_design) set_textf(lbl_design, "%d mAh", b.design_mah);
    if (lbl_health) set_textf(lbl_health, "%d%%", b.health_pct);

    if (b.charger_ok && lbl_chg_status) {
        set_text(lbl_chg_status, b.charge_status ? b.charge_status : "--");
        set_text(lbl_bus_status, b.bus_status ? b.bus_status : "--");
        if (lbl_ntc) set_text(lbl_ntc, b.ntc_status ? b.ntc_status : "--");
        set_textf(lbl_vbus, "%.2f V", b.vbus_v);
        set_textf(lbl_vsys, "%.2f V", b.vsys_v);
        set_textf(lbl_vbat, "%.2f V", b.vbat_v);
        if (lbl_chg_curr) set_textf(lbl_chg_curr, "%.0f mA", b.charge_current_ma);
    }
}

static void create(Handle parent) {
    Handle lst = list(parent);

    section_header(lst, i18n::t(i18n::T_BATTERY));
    lbl_percent = info_row(lst, i18n::t(i18n::T_CHARGE));
    lbl_voltage = info_row(lst, i18n::t(i18n::T_VOLTAGE));
    lbl_current = info_row(lst, i18n::t(i18n::T_CURRENT));
#ifndef BOARD_WIO_L1
    if (board::peri_status[E_PERI_BQ27220]) {
        lbl_temp   = info_row(lst, "Temp");
        lbl_remain = info_row(lst, "Remain");
        lbl_full   = info_row(lst, "Full Cap");
        lbl_design = info_row(lst, "Design");
        lbl_health = info_row(lst, "Health");
    }

    if (board::peri_status[E_PERI_BQ25896]) {
        section_header(lst, "Charger");
        lbl_chg_status = info_row(lst, "Status");
        lbl_bus_status = info_row(lst, "Bus");
        lbl_ntc        = info_row(lst, "NTC");
        lbl_vbus       = info_row(lst, "VBUS");
        lbl_vsys       = info_row(lst, "VSYS");
        lbl_vbat       = info_row(lst, "VBAT");
        lbl_chg_curr   = info_row(lst, "Chg Curr");
    }
#endif // !BOARD_WIO_L1
}

static void entry() {
    update_timer = every(3000, update_cb, nullptr);
    update_cb(nullptr);
}

static void exit_fn() {
    if (update_timer) { stop(update_timer); update_timer = nullptr; }
}

static void destroy() {
    lbl_percent = lbl_voltage = lbl_current = lbl_temp = nullptr;
    lbl_remain = lbl_full = lbl_design = lbl_health = nullptr;
    lbl_chg_status = lbl_bus_status = lbl_ntc = nullptr;
    lbl_vbus = lbl_vsys = lbl_vbat = lbl_chg_curr = nullptr;
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::battery
