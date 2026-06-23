#include <cstdio>
#include <cstring>
#include "waypoint_detail.h"
#include "compass.h"
#include "../screen_ids.h"
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../components/toast.h"
#include "../i18n.h"
#include "../../model.h"
#include "../../waypoint_store.h"
#include "../../mesh/mesh_task.h"
#ifdef BOARD_WIO_L1
  // The mono panel has no ui_theme.h (LVGL-only). Local metrics for the 250x122 e-ink.
  #define WPD_BTN_H 18
  #define WPD_GAP   3
#else
  #include "../ui_theme.h"
  #define WPD_BTN_H UI_TEXT_BTN_HEIGHT
  #define WPD_GAP   UI_MENU_ITEM_PAD
#endif

// Single-waypoint actions: navigate (compass), send over the active channel, delete.

namespace ui::screen::waypoint_detail {

using namespace ui::kit;

static int wp_idx = -1;

void set_index(int idx) { wp_idx = idx; }

static bool valid() { return wp_idx >= 0 && wp_idx < model::waypoints.count(); }

static void on_navigate(void*) {
    if (!valid()) return;
    const Waypoint& w = model::waypoints.at(wp_idx);
    ui::screen::compass::set_target_pos(w.label, w.lat_1e6, w.lon_1e6);
    ui::screen_mgr::push(SCREEN_COMPASS, true);
}

static void on_send(void*) {
    if (!valid()) return;
    const Waypoint& w = model::waypoints.at(wp_idx);
    char msg[80];
    // 6 decimals (~0.1 m) keeps the share meter-precise; matches the geo: scheme.
    snprintf(msg, sizeof(msg), "way:%.6f,%.6f %s",
             w.lat_1e6 / 1e6, w.lon_1e6 / 1e6, w.label);
#ifdef BOARD_WIO_L1
    bool ok = mesh::task::send_channel(mesh::task::get_msg_channel(), msg);
#else
    bool ok = mesh::task::send_public(msg);   // ESP32 build has no active-channel concept
#endif
    ui::toast::show(i18n::t(ok ? i18n::T_WAYPOINT_SENT : i18n::T_SEND_FAILED));
}

static void on_delete(void*) {
    if (valid()) model::waypoints.remove(wp_idx);
    wp_idx = -1;
    ui::screen_mgr::pop(true);
    ui::screen_mgr::reload_stack();   // rebuild the list without the deleted row
    ui::toast::show(i18n::t(i18n::T_WAYPOINT_DELETED));
}

static void create(Handle parent) {
    if (!valid()) return;
    const Waypoint& w = model::waypoints.at(wp_idx);

    Handle content = column(parent);
    size(content, pct(100), pct(100));
    gap(content, WPD_GAP);

    Handle info = label(content, "");
    size(info, pct(100), CONTENT);
    font(info, Font::Body);
    set_textf(info, "%s\n%.6f, %.6f", w.label, w.lat_1e6 / 1e6, w.lon_1e6 / 1e6);

    Handle spacer = label(content, "");
    grow(spacer, 1);   // push the buttons to the bottom

    Handle b_nav = button(content, i18n::t(i18n::T_NAVIGATE), on_navigate, nullptr);
    size(b_nav, pct(100), WPD_BTN_H);
    Handle b_send = button(content, i18n::t(i18n::T_SEND), on_send, nullptr);
    size(b_send, pct(100), WPD_BTN_H);
    Handle b_del = button(content, i18n::t(i18n::T_DELETE), on_delete, nullptr);
    size(b_del, pct(100), WPD_BTN_H);
}

static void entry() {}
static void exit_fn() {}
static void destroy() {}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::waypoint_detail
