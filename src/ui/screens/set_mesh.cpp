#include <cstdio>
#include "set_mesh.h"
#ifndef BOARD_WIO_L1
#include "../ui_theme.h"        // LVGL boards only; mono needs nothing from it here
#endif
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../i18n.h"
#include "../../model.h"
#include "../../mesh/mesh_task.h"

namespace ui::screen::set_mesh {

using namespace ui::kit;

static ui::kit::Handle lbl_freq = NULL;
static ui::kit::Handle lbl_bw = NULL;
static ui::kit::Handle lbl_sf = NULL;
static ui::kit::Handle lbl_cr = NULL;
static ui::kit::Handle lbl_txpow = NULL;
static ui::kit::Handle lbl_gps_share = NULL;
static ui::kit::Handle lbl_repeat = NULL;

static const float freqs[] = {868.0, 869.525, 869.618, 910.525, 915.0, 433.0};
static const int n_freqs = 6;
static const float bws[] = {31.25, 62.5, 125.0, 250.0, 500.0};
static const int n_bws = 5;
static const uint8_t sfs[] = {7, 8, 9, 10, 11, 12};
static const int n_sfs = 6;
static const uint8_t crs[] = {5, 6, 7, 8};
static const int n_crs = 4;
static const int8_t powers[] = {2, 5, 10, 14, 17, 20, 22};
static const int n_powers = 7;

static int find_f(const float* a, int n, float v) { for (int i=0;i<n;i++) if(a[i]==v) return i; return 0; }
static int find_u8(const uint8_t* a, int n, uint8_t v) { for (int i=0;i<n;i++) if(a[i]==v) return i; return 0; }
static int find_i8(const int8_t* a, int n, int8_t v) { for (int i=0;i<n;i++) if(a[i]==v) return i; return 0; }

static char buf[32];

static void on_back(void*) { ui::screen_mgr::pop(true); }

static void on_freq(void*) {
    int i = (find_f(freqs, n_freqs, mesh::task::get_freq()) + 1) % n_freqs;
    mesh::task::set_freq(freqs[i]);
    snprintf(buf, sizeof(buf), "%.3f", freqs[i]);
    if (lbl_freq) set_text(lbl_freq, buf);
}
static void on_bw(void*) {
    int i = (find_f(bws, n_bws, mesh::task::get_bw()) + 1) % n_bws;
    mesh::task::set_bw(bws[i]);
    snprintf(buf, sizeof(buf), "%.1f kHz", bws[i]);
    if (lbl_bw) set_text(lbl_bw, buf);
}
static void on_sf(void*) {
    int i = (find_u8(sfs, n_sfs, mesh::task::get_sf()) + 1) % n_sfs;
    mesh::task::set_sf(sfs[i]);
    snprintf(buf, sizeof(buf), "%d", sfs[i]);
    if (lbl_sf) set_text(lbl_sf, buf);
}
static void on_cr(void*) {
    int i = (find_u8(crs, n_crs, mesh::task::get_cr()) + 1) % n_crs;
    mesh::task::set_cr(crs[i]);
    snprintf(buf, sizeof(buf), "4/%d", crs[i]);
    if (lbl_cr) set_text(lbl_cr, buf);
}
static void on_txpow(void*) {
    int i = (find_i8(powers, n_powers, mesh::task::get_tx_power()) + 1) % n_powers;
    mesh::task::set_tx_power(powers[i]);
    snprintf(buf, sizeof(buf), "%d dBm", powers[i]);
    if (lbl_txpow) set_text(lbl_txpow, buf);
}
#ifndef BOARD_WIO_L1
static void on_gps_share(void*) {
    bool cur = mesh::task::get_advert_location();
    mesh::task::set_advert_location(!cur);
    if (lbl_gps_share) set_text(lbl_gps_share, i18n::t(!cur ? i18n::T_ON : i18n::T_OFF));
}
#endif
static void on_repeat(void*) {
    bool cur = mesh::task::get_client_repeat();
    mesh::task::set_client_repeat(!cur);
    if (lbl_repeat) set_text(lbl_repeat, i18n::t(!cur ? i18n::T_ON : i18n::T_OFF));
}

static void create(Handle parent) {
    Handle lst = list(parent);

    auto& m = model::mesh;

    // Read-only node name
    toggle_item(lst, i18n::t(i18n::T_NODE), m.node_name ? m.node_name : "--", nullptr, nullptr);

    snprintf(buf, sizeof(buf), "%.3f", m.freq_mhz);
    lbl_freq = toggle_item(lst, "Freq", buf, on_freq, nullptr);
    snprintf(buf, sizeof(buf), "%.1f kHz", m.bw_khz);
    lbl_bw = toggle_item(lst, "BW", buf, on_bw, nullptr);
    snprintf(buf, sizeof(buf), "%d", m.sf);
    lbl_sf = toggle_item(lst, "SF", buf, on_sf, nullptr);
    snprintf(buf, sizeof(buf), "4/%d", m.cr);
    lbl_cr = toggle_item(lst, "CR", buf, on_cr, nullptr);
    snprintf(buf, sizeof(buf), "%d dBm", m.tx_power_dbm);
    lbl_txpow = toggle_item(lst, i18n::t(i18n::T_TX_POWER), buf, on_txpow, nullptr);
#ifndef BOARD_WIO_L1
    // On Wio this lives under Settings > Privacy (see set_privacy.cpp); Mesh
    // keeps only radio config + the fast-GPS channel picker.
    lbl_gps_share = toggle_item(lst, i18n::t(i18n::T_GPS_SHARE), i18n::t(mesh::task::get_advert_location() ? i18n::T_ON : i18n::T_OFF), on_gps_share, nullptr);
#endif
    lbl_repeat = toggle_item(lst, i18n::t(i18n::T_REPEAT), i18n::t(mesh::task::get_client_repeat() ? i18n::T_ON : i18n::T_OFF), on_repeat, nullptr);
}

static void entry() {}
static void exit_fn() {}
static void destroy() {
    lbl_freq = lbl_bw = lbl_sf = lbl_cr = lbl_txpow = lbl_gps_share = lbl_repeat = nullptr;
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::set_mesh
