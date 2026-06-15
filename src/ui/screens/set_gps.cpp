#include "set_gps.h"
#include <cstdio>
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../i18n.h"
#include "../../model.h"
#include "../../mesh/mesh_task.h"

// GPS settings — ported to the ui::kit facade.

namespace ui::screen::set_gps {

using namespace ui::kit;

static Handle lbl_gps_enabled = nullptr;

static void on_gps_toggle(void*) {
    bool cur = mesh::task::get_gps_enabled();
    mesh::task::set_gps_enabled(!cur);
    if (lbl_gps_enabled) set_text(lbl_gps_enabled, i18n::t(!cur ? i18n::T_ON : i18n::T_OFF));
}

#ifdef BOARD_WIO_L1
// ---- fast-GPS channel picker (Wio mono) -----------------------------------
// Selects the channel fast-GPS position beacons are broadcast to (Off + any
// non-public channel). Lives here under GPS settings alongside the GPS toggle.
static Handle lbl_fastgps = nullptr;
static char chanbuf[24];

static const char* fastgps_label() {
    uint8_t sel = mesh::task::get_fast_gps_channel();
    if (sel == mesh::task::FAST_GPS_DISABLED || sel == 0) return i18n::t(i18n::T_OFF);
    mesh::task::ChannelEntry chans[16];
    int n = mesh::task::get_channels(chans, 16);
    // Copy into the persistent buffer — chans[] is a stack local, so returning
    // chans[i].name directly would dangle by the time the caller reads it.
    for (int i = 0; i < n; i++)
        if (chans[i].idx == sel) { snprintf(chanbuf, sizeof(chanbuf), "%s", chans[i].name); return chanbuf; }
    snprintf(chanbuf, sizeof(chanbuf), "Ch %d", (int)sel);
    return chanbuf;
}
static void on_fastgps(void*) {
    // Build the option ring: Off, then each non-public channel (idx != 0).
    mesh::task::ChannelEntry chans[16];
    int n = mesh::task::get_channels(chans, 16);
    uint8_t opts[17];
    int m = 0;
    opts[m++] = mesh::task::FAST_GPS_DISABLED;          // "Off"
    for (int i = 0; i < n; i++) if (chans[i].idx != 0) opts[m++] = chans[i].idx;
    uint8_t sel = mesh::task::get_fast_gps_channel();
    if (sel == 0) sel = mesh::task::FAST_GPS_DISABLED;  // public counts as disabled
    int cur = 0;
    for (int i = 0; i < m; i++) if (opts[i] == sel) { cur = i; break; }
    int next = (cur + 1) % m;
    mesh::task::set_fast_gps_channel(opts[next]);
    if (lbl_fastgps) set_text(lbl_fastgps, fastgps_label());
}

// ---- fast-GPS region picker (Wio mono) ------------------------------------
// Region scope tag for the beacon flood. "Unscoped" floods everywhere (legacy
// behaviour); named regions ride a public-hashtag transport code so only
// region-aware repeaters relay them, keeping scoped traffic off the wider mesh.
static Handle lbl_fastregion = nullptr;
static void on_fastregion(void*) {
    uint8_t count = mesh::task::fast_gps_region_count();
    if (count == 0) return;
    uint8_t cur = mesh::task::get_fast_gps_region();
    uint8_t next = (cur + 1) % count;
    mesh::task::set_fast_gps_region(next);
    if (lbl_fastregion) set_text(lbl_fastregion, mesh::task::fast_gps_region_label(next));
}
#endif // BOARD_WIO_L1

static void create(Handle parent) {
    Handle lst = list(parent);
    lbl_gps_enabled = toggle_item(lst, i18n::t(i18n::T_GPS), i18n::t(mesh::task::get_gps_enabled() ? i18n::T_ON : i18n::T_OFF), on_gps_toggle, nullptr);
#ifdef BOARD_WIO_L1
    lbl_fastgps = toggle_item(lst, i18n::t(i18n::T_GPS_CHANNEL), fastgps_label(), on_fastgps, nullptr);
    lbl_fastregion = toggle_item(lst, "Region", mesh::task::fast_gps_region_label(mesh::task::get_fast_gps_region()), on_fastregion, nullptr);
#endif
    toggle_item(lst, i18n::t(i18n::T_MODULE), i18n::t(model::gps.module_ok ? i18n::T_OK : i18n::T_NONE), nullptr, nullptr);
    toggle_item(lst, i18n::t(i18n::T_RTC_SYNC), i18n::t(i18n::T_AUTO), nullptr, nullptr);
}

static void entry() {}
static void exit_fn() {}
static void destroy() {
    lbl_gps_enabled = nullptr;
#ifdef BOARD_WIO_L1
    lbl_fastgps = nullptr;
    lbl_fastregion = nullptr;
#endif
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::set_gps
