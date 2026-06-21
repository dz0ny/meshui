#include "provision.h"
#ifdef BOARD_WIO_L1
// ---------------------------------------------------------------------------
// Provision (Wio mono build): clone this device's config onto another over BLE.
// The menu screen kicks off a reboot-into-provisioning mode; the run screen
// shows transfer status while that mode is active.
// ---------------------------------------------------------------------------
#include "../kit/ui_kit.h"
#include "../i18n.h"
#include "../ui_screen_mgr.h"
#include "../screen_ids.h"
#include "../../mesh/provision.h"
#include "../../mesh/mesh_task.h"
#include <stdint.h>

namespace ui::screen::provision {

using namespace ui::kit;

// ---- menu (Share / Receive) ------------------------------------------------
static void on_share(void*)   { ::provision::request(::provision::Mode::Share); }
static void on_receive(void*) { ::provision::request(::provision::Mode::Receive); }

static void create(Handle parent) {
    Handle lst = list(parent);
    menu_row(lst, i18n::t(i18n::T_SHARE_PROFILE),   on_share,   nullptr);
    menu_row(lst, i18n::t(i18n::T_RECEIVE_PROFILE), on_receive, nullptr);
}

static void entry() {}
static void exit_fn() {}
static void destroy() {}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

// ---- run / status ----------------------------------------------------------
static Handle lbl = nullptr;
static Timer  timer = nullptr;

static void tick(void*) {
    if (!lbl) return;
    switch (::provision::state()) {
        case ::provision::State::Scanning:
            set_text(lbl, i18n::t(i18n::T_PROV_SEARCH)); break;
        case ::provision::State::Sending:
        case ::provision::State::Receiving:
            // Percent plus raw byte progress so the user sees the transfer move.
            set_textf(lbl, "%s %d%%\n%d/%d B", i18n::t(i18n::T_PROV_TRANSFER),
                      ::provision::progress(),
                      ::provision::bytes_done(), ::provision::bytes_total());
            break;
        case ::provision::State::Done: {
            // On the receiver, profile_import recorded what it applied (-1 on the
            // sharer, which never imports) — show the channel/contact tally.
            int ch  = mesh::task::last_import_channels();
            int con = mesh::task::last_import_contacts();
            if (ch >= 0)
                set_textf(lbl, "%s\nch:%d con:%d", i18n::t(i18n::T_PROV_DONE), ch, con);
            else
                set_text(lbl, i18n::t(i18n::T_PROV_DONE));
            break;
        }
        case ::provision::State::Error:
            set_text(lbl, i18n::t(i18n::T_PROV_FAILED)); break;
        default:
            set_text(lbl, i18n::t(i18n::T_PROV_WAIT)); break;
    }
}

static void run_create(Handle parent) {
    lbl = label(parent, i18n::t(i18n::T_PROV_WAIT));
    font(lbl, Font::Title);
    align(lbl, Align::Center);
    tick(nullptr);
}

static void run_entry()  { timer = every(500, tick, nullptr); }
static void run_exit()   { if (timer) { stop(timer); timer = nullptr; } }
static void run_destroy(){ lbl = nullptr; }

screen_lifecycle_t run_lifecycle = { run_create, run_entry, run_exit, run_destroy };

// ---- receiver device picker -------------------------------------------------
// Lists nearby advertisers (a sharer shows up as "MeshCore-Provision") so the
// user picks one instead of blindly auto-connecting — and can see whether the
// sharer is heard at all. Rows are pre-created and shown/hidden as devices come
// and go, so the focus cursor stays put across refreshes.
static const int PICK_MAX = 8;
static Handle pick_status = nullptr;
static Handle pick_rows[PICK_MAX] = {};
static Timer  pick_timer = nullptr;
static int    pick_shown = -1;   // device count last reflected in the rows

static void on_pick(void* user) {
    int idx = (int)(intptr_t)user;
    ::provision::connect_to(idx);
    ui::screen_mgr::push(SCREEN_PROVISION_RUN, true);   // show transfer status
}

static void pick_refresh(void*) {
    int n = ::provision::device_count();
    if (n > PICK_MAX) n = PICK_MAX;
    if (n == pick_shown) return;     // nothing new — skip the e-ink redraw
    pick_shown = n;

    if (pick_status)
        set_text(pick_status, i18n::t(n == 0 ? i18n::T_PROV_SEARCH : i18n::T_PROV_SELECT));

    for (int i = 0; i < PICK_MAX; i++) {
        if (!pick_rows[i]) continue;
        if (i < n) {
            set_textf(pick_rows[i], "%s  %ddBm",
                      ::provision::device_name(i), ::provision::device_rssi(i));
            hidden(pick_rows[i], false);
        } else {
            hidden(pick_rows[i], true);
        }
    }
}

static void pick_create(Handle parent) {
    Handle lst = list(parent);
    pick_status = label(lst, i18n::t(i18n::T_PROV_SEARCH));
    font(pick_status, Font::Title);
    for (int i = 0; i < PICK_MAX; i++) {
        pick_rows[i] = menu_row(lst, "", on_pick, (void*)(intptr_t)i);
        hidden(pick_rows[i], true);
    }
    pick_shown = -1;
    pick_refresh(nullptr);
}

static void pick_entry()   { pick_timer = every(700, pick_refresh, nullptr); }
static void pick_exit()    { if (pick_timer) { stop(pick_timer); pick_timer = nullptr; } }
static void pick_destroy() { pick_status = nullptr; for (int i = 0; i < PICK_MAX; i++) pick_rows[i] = nullptr; }

screen_lifecycle_t pick_lifecycle = { pick_create, pick_entry, pick_exit, pick_destroy };

} // namespace ui::screen::provision
#endif // BOARD_WIO_L1
