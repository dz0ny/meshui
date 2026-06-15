#include <cstdio>
#include "gps.h"
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../i18n.h"
#include "../../model.h"

// GPS screen — first screen ported onto the ui::kit facade (no direct lv_* use),
// so it compiles unchanged against both the LVGL backend (ESP32) and the future
// mono backend (nRF52 tracker).

namespace ui::screen::gps {

using namespace ui::kit;

static Timer  update_timer = nullptr;
static Handle lbl_status   = nullptr;
static Handle lbl_lat      = nullptr;
static Handle lbl_lng      = nullptr;
static Handle lbl_sats     = nullptr;
static Handle lbl_altitude = nullptr;
static Handle lbl_speed    = nullptr;

// Uses the facade's ui::kit::info_row (label-left, value-right; returns the
// value handle) so the row styling/sizing comes from the active backend.

static void update_cb(void*) {
    auto& g = model::gps;
    set_text(lbl_status, g.status_text ? g.status_text : "--");
    if (g.has_fix) {
        static char buf[24];
        snprintf(buf, sizeof(buf), "%.6f", g.lat);   set_text(lbl_lat, buf);
        snprintf(buf, sizeof(buf), "%.6f", g.lng);   set_text(lbl_lng, buf);
        snprintf(buf, sizeof(buf), "%.0f m", g.altitude_m); set_text(lbl_altitude, buf);
        snprintf(buf, sizeof(buf), "%.1f km/h", g.speed_kmh); set_text(lbl_speed, buf);
    } else {
        set_text(lbl_lat, "--");
        set_text(lbl_lng, "--");
        set_text(lbl_altitude, "--");
        set_text(lbl_speed, "--");
    }
    static char sbuf[12];
    snprintf(sbuf, sizeof(sbuf), "%lu", (unsigned long)g.satellites);
    set_text(lbl_sats, sbuf);
}

static void create(Handle parent) {
    Handle lst = list(parent);

    lbl_status   = info_row(lst, i18n::t(i18n::T_GPS_STATUS));
    lbl_lat      = info_row(lst, i18n::t(i18n::T_LATITUDE));
    lbl_lng      = info_row(lst, i18n::t(i18n::T_LONGITUDE));
    lbl_sats     = info_row(lst, i18n::t(i18n::T_SATELLITES));
    lbl_altitude = info_row(lst, i18n::t(i18n::T_ALTITUDE));
    lbl_speed    = info_row(lst, i18n::t(i18n::T_SPEED));
}

static void entry() {
    update_timer = every(2000, update_cb, nullptr);
    update_cb(nullptr);
}

static void exit_fn() {
    if (update_timer) { stop(update_timer); update_timer = nullptr; }
}

static void destroy() {
    lbl_status = lbl_lat = lbl_lng = lbl_sats = lbl_altitude = lbl_speed = nullptr;
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::gps
