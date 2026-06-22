#include <cstdio>
#include <cstring>
#include "home.h"
#include "compose.h"
#include "../ui_theme.h"          // SCREEN_* ids + UI_HOME_* layout
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../../model.h"
#include "../../board.h"

// Home screen — ported to the ui::kit facade.

namespace ui::screen::home {

using namespace ui::kit;

static constexpr int HOME_NODE_NAME_Y = UI_HOME_NODE_Y;
static constexpr int HOME_CLOCK_Y = UI_HOME_CLOCK_Y;
static constexpr int HOME_DATE_Y = UI_HOME_DATE_Y;
static constexpr int HOME_MENU_Y = UI_HOME_MENU_Y;
static constexpr int HOME_MENU_TOP_INSET = 10;
static constexpr int HOME_MENU_BOTTOM_INSET = 10;

static Handle lbl_node_name = nullptr;
static Handle lbl_clock = nullptr;
static Handle lbl_date = nullptr;
static Handle lbl_msg_badge = nullptr;
static char cached_node_name[64] = {};
static char cached_clock[24] = {};
static char cached_date[16] = {};
static char cached_msg_badge[16] = {};

static void on_chat_click(void*)     { ui::screen_mgr::push(SCREEN_CHAT, true); }
static void on_team_click(void*)     { ui::screen_mgr::push(SCREEN_TEAM, true); }
static void on_compose_click(void*)  { ui::screen::compose::set_recipient(NULL); ui::screen_mgr::push(SCREEN_COMPOSE, true); }
static void on_contacts_click(void*) { ui::screen_mgr::push(SCREEN_CONTACTS, true); }
static void on_sensors_click(void*)  { ui::screen_mgr::push(SCREEN_SENSORS, true); }
static void on_map_click(void*)      { ui::screen_mgr::push(SCREEN_MAP, true); }
static void on_trail_click(void*)    { ui::screen_mgr::push(SCREEN_TRAIL, true); }
static void on_waypoints_click(void*){ ui::screen_mgr::push(SCREEN_WAYPOINTS, true); }
static void on_settings_click(void*) { ui::screen_mgr::push(SCREEN_SETTINGS, true); }

static void create(Handle parent) {
    free_layout(parent);   // home positions absolutely

#if UI_HOME_SHOW_NODE
    lbl_node_name = label(parent, model::mesh.node_name ? model::mesh.node_name : T_PAPER_HW_VERSION);
    font(lbl_node_name, Font::Title);
    align(lbl_node_name, Align::TopMid, 0, HOME_NODE_NAME_Y);
#endif

    lbl_clock = label(parent, "");
    align(lbl_clock, Align::TopMid, 0, HOME_CLOCK_Y);
#if UI_HOME_CLOCK_INLINE
    font(lbl_clock, Font::Title);
    set_textf(lbl_clock, "%02d:%02d  %02d/%02d/20%02d",
        model::clock.hour, model::clock.minute,
        model::clock.day, model::clock.month, model::clock.year);
#else
    font(lbl_clock, Font::ClockLg);
    set_textf(lbl_clock, "%02d:%02d", model::clock.hour, model::clock.minute);

    lbl_date = label(parent, "");
    font(lbl_date, Font::Title);
    align(lbl_date, Align::TopMid, 0, HOME_DATE_Y);
    set_textf(lbl_date, "%02d/%02d/20%02d",
        model::clock.day, model::clock.month, model::clock.year);
#endif

    Handle menu = column(parent);
#if UI_HOME_MENU_SCROLL
    size(menu, pct(95), SCREEN_HEIGHT - HOME_MENU_Y - HOME_MENU_TOP_INSET - HOME_MENU_BOTTOM_INSET);
    align(menu, Align::TopMid, 0, HOME_MENU_Y + HOME_MENU_TOP_INSET);
    scrollable(menu, true);
#else
    size(menu, pct(90), CONTENT);
    align(menu, Align::TopMid, 0, HOME_MENU_Y);
    scrollable(menu, false);
#endif
    gap(menu, 0);

    lbl_msg_badge = toggle_item(menu, "Messages", "", on_chat_click, nullptr);
    menu_row(menu, "Compose",  on_compose_click,  nullptr);
    menu_row(menu, "Contacts", on_contacts_click, nullptr);
    // "Team" only when at least one favorited chat contact exists.
    model::refresh_contacts();
    if (model::team_count() > 0)
        menu_row(menu, "Team", on_team_click, nullptr);
    menu_row(menu, "Sensors",  on_sensors_click,  nullptr);
    menu_row(menu, "Map",      on_map_click,      nullptr);
    menu_row(menu, "Trail",    on_trail_click,    nullptr);
    menu_row(menu, "Waypoints", on_waypoints_click, nullptr);
    menu_row(menu, "Settings", on_settings_click, nullptr);
}

// Called by the model update cycle (every 2s) — just refresh the cached labels.
void update(uint32_t flags) {
    char buf[64];
    if ((flags & model::DIRTY_CLOCK) && lbl_clock) {
#if UI_HOME_CLOCK_INLINE
        snprintf(buf, sizeof(buf), "%02d:%02d  %02d/%02d/20%02d",
            model::clock.hour, model::clock.minute,
            model::clock.day, model::clock.month, model::clock.year);
#else
        snprintf(buf, sizeof(buf), "%02d:%02d", model::clock.hour, model::clock.minute);
#endif
        if (strcmp(cached_clock, buf) != 0) {
            strncpy(cached_clock, buf, sizeof(cached_clock) - 1);
            cached_clock[sizeof(cached_clock) - 1] = 0;
            set_text(lbl_clock, cached_clock);
        }
    }
    if ((flags & model::DIRTY_CLOCK) && lbl_date) {
        snprintf(buf, sizeof(buf), "%02d/%02d/20%02d",
            model::clock.day, model::clock.month, model::clock.year);
        if (strcmp(cached_date, buf) != 0) {
            strncpy(cached_date, buf, sizeof(cached_date) - 1);
            cached_date[sizeof(cached_date) - 1] = 0;
            set_text(lbl_date, cached_date);
        }
    }
    if ((flags & model::DIRTY_MESH) && lbl_node_name && model::mesh.node_name) {
        if (strcmp(cached_node_name, model::mesh.node_name) != 0) {
            strncpy(cached_node_name, model::mesh.node_name, sizeof(cached_node_name) - 1);
            cached_node_name[sizeof(cached_node_name) - 1] = 0;
            set_text(lbl_node_name, cached_node_name);
        }
    }
    if ((flags & model::DIRTY_SLEEP) && lbl_msg_badge) {
        int unread = model::sleep_cfg.unread_messages;
        if (unread > 0) snprintf(buf, sizeof(buf), "(%d)", unread);
        else            buf[0] = 0;
        if (strcmp(cached_msg_badge, buf) != 0) {
            strncpy(cached_msg_badge, buf, sizeof(cached_msg_badge) - 1);
            cached_msg_badge[sizeof(cached_msg_badge) - 1] = 0;
            set_text(lbl_msg_badge, cached_msg_badge);
        }
    }
}

static void entry() {
    update(model::DIRTY_CLOCK | model::DIRTY_MESH | model::DIRTY_SLEEP);
}

static void exit_fn() {}

static void destroy() {
    lbl_node_name = nullptr;
    lbl_clock = nullptr;
    lbl_msg_badge = nullptr;
    lbl_date = nullptr;
    cached_node_name[0] = 0;
    cached_clock[0] = 0;
    cached_date[0] = 0;
    cached_msg_badge[0] = 0;
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::home
