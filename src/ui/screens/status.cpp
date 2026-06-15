#include "status.h"
#include "../screen_ids.h"      // SCREEN_* ids (lvgl-free, shared with mono)
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../i18n.h"

// Status menu — ported to the ui::kit facade (no direct lv_* use).

namespace ui::screen::status {

using namespace ui::kit;

static void on_mesh(void*)    { ui::screen_mgr::push(SCREEN_MESH, true); }
static void on_gps(void*)     { ui::screen_mgr::push(SCREEN_GPS, true); }
static void on_battery(void*) { ui::screen_mgr::push(SCREEN_BATTERY, true); }

static void create(Handle parent) {
    Handle menu = list(parent);
    menu_row(menu, i18n::t(i18n::T_BATTERY),   on_battery, nullptr);
    menu_row(menu, i18n::t(i18n::T_GPS_INFO),  on_gps,     nullptr);
    menu_row(menu, i18n::t(i18n::T_MESH_INFO), on_mesh,    nullptr);
}

static void entry() {}
static void exit_fn() {}
static void destroy() {}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::status
