#include "set_privacy.h"
#ifdef BOARD_WIO_L1
// ---------------------------------------------------------------------------
// Privacy settings (Wio mono). Hosts the "share my GPS location in the advert
// packet" toggle, moved here out of Mesh settings: Mesh keeps the radio config
// and the fast-GPS channel picker, while this screen owns the privacy choice
// of whether the node's coarse location rides along in its adverts.
// ---------------------------------------------------------------------------
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../i18n.h"
#include "../../mesh/mesh_task.h"

namespace ui::screen::set_privacy {

using namespace ui::kit;

static Handle lbl_advert_gps = nullptr;

static void on_advert_gps(void*) {
    bool cur = mesh::task::get_advert_location();
    mesh::task::set_advert_location(!cur);
    if (lbl_advert_gps) set_text(lbl_advert_gps, i18n::t(!cur ? i18n::T_ON : i18n::T_OFF));
}

static void create(Handle parent) {
    Handle lst = list(parent);
    lbl_advert_gps = toggle_item(lst, i18n::t(i18n::T_ADVERT_GPS),
                                 i18n::t(mesh::task::get_advert_location() ? i18n::T_ON : i18n::T_OFF),
                                 on_advert_gps, nullptr);
}

static void entry() {}
static void exit_fn() {}
static void destroy() { lbl_advert_gps = nullptr; }

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::set_privacy

#endif // BOARD_WIO_L1
