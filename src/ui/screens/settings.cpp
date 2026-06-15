#include "settings.h"
#include "../screen_ids.h"      // SCREEN_* ids (lvgl-free, shared with mono)
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../i18n.h"

// Settings menu — ported to the ui::kit facade (no direct lv_* use).

namespace ui::screen::settings {

using namespace ui::kit;

static void on_gps(void*)     { ui::screen_mgr::push(SCREEN_SET_GPS, true); }
static void on_mesh(void*)    { ui::screen_mgr::push(SCREEN_SET_MESH, true); }
static void on_display(void*) { ui::screen_mgr::push(SCREEN_SET_DISPLAY, true); }
static void on_ble(void*)     { ui::screen_mgr::push(SCREEN_SET_BLE, true); }
#ifdef BOARD_WIO_L1
static void on_privacy(void*) { ui::screen_mgr::push(SCREEN_PRIVACY, true); }
#endif
#ifndef BOARD_WIO_L1
static void on_storage(void*) { ui::screen_mgr::push(SCREEN_SET_STORAGE, true); }
static void on_debug(void*)   { ui::screen_mgr::push(SCREEN_SETTINGS_DEBUG, true); }
static void on_device(void*)  { ui::screen_mgr::push(SCREEN_SETTINGS_DEVICE, true); }
#endif

static void create(Handle parent) {
    Handle menu = list(parent);
#ifdef BOARD_WIO_L1
    // Wio mono build: only the radio-relevant, fully-portable settings screens.
    menu_row(menu, i18n::t(i18n::T_DISPLAY),       on_display, nullptr);
    menu_row(menu, i18n::t(i18n::T_BLUETOOTH),     on_ble,     nullptr);
    menu_row(menu, i18n::t(i18n::T_GPS_SETTINGS),  on_gps,     nullptr);
    menu_row(menu, i18n::t(i18n::T_MESH_SETTINGS), on_mesh,    nullptr);
    menu_row(menu, i18n::t(i18n::T_PRIVACY),       on_privacy, nullptr);
#else
    menu_row(menu, "Display",       on_display, nullptr);
    menu_row(menu, "Bluetooth",     on_ble,     nullptr);
    menu_row(menu, "GPS Settings",  on_gps,     nullptr);
    menu_row(menu, "Mesh Settings", on_mesh,    nullptr);
    menu_row(menu, "Storage",       on_storage, nullptr);
    menu_row(menu, "Debug",         on_debug,   nullptr);
    menu_row(menu, "Device",        on_device,  nullptr);
#endif
}

static void entry() {}
static void exit_fn() {}
static void destroy() {}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::settings
