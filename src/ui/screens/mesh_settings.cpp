#include "mesh_settings.h"
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../i18n.h"
#include "../../model.h"

// Mesh info (read-only radio + stats) — ported to the ui::kit facade.

namespace ui::screen::mesh_settings {

using namespace ui::kit;

static Timer  update_timer = nullptr;
static Handle lbl_name, lbl_freq, lbl_bw, lbl_sf, lbl_cr, lbl_txpow;
static Handle lbl_peers, lbl_rx, lbl_tx, lbl_rssi, lbl_snr;

static void update_cb(void*) {
    auto& m = model::mesh;
    set_text (lbl_name,  m.node_name ? m.node_name : "--");
    set_textf(lbl_freq,  "%.3f MHz", m.freq_mhz);
    set_textf(lbl_bw,    "%.1f kHz", m.bw_khz);
    set_textf(lbl_sf,    "%d", m.sf);
    set_textf(lbl_cr,    "4/%d", m.cr);
    set_textf(lbl_txpow, "%d dBm", m.tx_power_dbm);
    set_textf(lbl_peers, "%d", m.peer_count);
    set_textf(lbl_rx,    "%lu", (unsigned long)m.rx_packets);
    set_textf(lbl_tx,    "%lu", (unsigned long)m.tx_packets);
    set_textf(lbl_rssi,  "%.0f dBm", m.last_rssi);
    set_textf(lbl_snr,   "%.1f dB", m.last_snr);
}

static void create(Handle parent) {
    Handle lst = list(parent);

    section_header(lst, i18n::t(i18n::T_RADIO));
    lbl_name  = info_row(lst, i18n::t(i18n::T_NODE));
    lbl_freq  = info_row(lst, "Freq");
    lbl_bw    = info_row(lst, "BW");
    lbl_sf    = info_row(lst, "SF");
    lbl_cr    = info_row(lst, "CR");
    lbl_txpow = info_row(lst, i18n::t(i18n::T_TX_POWER));

    section_header(lst, i18n::t(i18n::T_STATS));
    lbl_peers = info_row(lst, i18n::t(i18n::T_PEERS));
    lbl_rx    = info_row(lst, "RX Pkts");
    lbl_tx    = info_row(lst, "TX Pkts");
    lbl_rssi  = info_row(lst, "RSSI");
    lbl_snr   = info_row(lst, "SNR");
}

static void entry() {
    update_timer = every(3000, update_cb, nullptr);
    update_cb(nullptr);
}

static void exit_fn() {
    if (update_timer) { stop(update_timer); update_timer = nullptr; }
}

static void destroy() {
    lbl_name = lbl_freq = lbl_bw = lbl_sf = lbl_cr = lbl_txpow = nullptr;
    lbl_peers = lbl_rx = lbl_tx = lbl_rssi = lbl_snr = nullptr;
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::mesh_settings
