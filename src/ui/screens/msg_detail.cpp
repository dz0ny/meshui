#include "msg_detail.h"
#include "compose.h"
#include "compass.h"
#include "../ui_theme.h"          // SCREEN_* ids + UI_* layout
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../components/toast.h"
#include "../i18n.h"
#include "../../model.h"
#include "../../waypoint_store.h"
#include "../../sd_log.h"
// Arduino.h's DEG_TO_RAD macro would clobber ui::geo's; drop it before geo_utils.
#ifdef DEG_TO_RAD
#undef DEG_TO_RAD
#endif
#include "../components/geo_utils.h"

// Single message + reply/delete actions — ported to the ui::kit facade. When the
// body carries a shared location (geo:/way:), Navigate and Save are offered too.

namespace ui::screen::msg_detail {

using namespace ui::kit;

static int msg_idx = -1;

void set_message(int idx) { msg_idx = idx; }

// Coordinates parsed from the current message body, in 1e6 fixed point.
static bool    has_loc = false;
static int32_t loc_lat_e6 = 0, loc_lon_e6 = 0;
static char    loc_label[24] = {};

static void on_navigate(void*) {
    if (!has_loc) return;
    ui::screen::compass::set_target_pos(loc_label[0] ? loc_label : "Shared",
                                        loc_lat_e6, loc_lon_e6);
    ui::screen_mgr::push(SCREEN_COMPASS, true);
}

static void on_save(void*) {
    if (!has_loc) return;
    if (model::waypoints.full()) { ui::toast::show(i18n::t(i18n::T_WAYPOINTS_FULL)); return; }
    bool ok = model::waypoints.add(loc_lat_e6, loc_lon_e6, model::epoch_now,
                                   loc_label[0] ? loc_label : nullptr);
    ui::toast::show(i18n::t(ok ? i18n::T_SAVED_WAYPOINT : i18n::T_WAYPOINTS_FULL));
}

static void on_delete(void*) {
    if (msg_idx >= 0 && msg_idx < model::message_count) {
        model::delete_message(msg_idx);
        sd_log::mark_dirty();
    }
    ui::screen_mgr::pop(true);
}

static void on_reply(void*) {
    if (msg_idx >= 0 && msg_idx < model::message_count) {
        ui::screen::compose::set_recipient(model::messages[msg_idx].sender);
        ui::screen_mgr::push(SCREEN_COMPOSE, true);
    }
}

static void create(Handle parent) {
    if (msg_idx < 0 || msg_idx >= model::message_count) return;
    auto& msg = model::messages[msg_idx];

    Handle content = column(parent);
    size(content, pct(100), pct(100));
    gap(content, UI_MENU_ITEM_PAD);

    // Reuse the bubble component for the message.
    Handle bubbles = msglist(content);
    size(bubbles, pct(100), CONTENT);
    grow(bubbles, 1);
    msg_append(bubbles, msg.sender, msg.text, msg.is_self, -1);

    // Detect a shared location anywhere in the body — offer Navigate / Save.
    double la = 0, lo = 0;
    has_loc = ui::geo::parse_location(msg.text, la, lo, loc_label, sizeof(loc_label));
    if (has_loc) {
        loc_lat_e6 = (int32_t)(la * 1e6);
        loc_lon_e6 = (int32_t)(lo * 1e6);
        Handle loc_actions = row(content);
        size(loc_actions, pct(100), UI_TEXT_BTN_HEIGHT);
        gap(loc_actions, UI_MENU_ITEM_PAD);
        Handle nav_btn = button(loc_actions, i18n::t(i18n::T_NAVIGATE), on_navigate, nullptr);
        size(nav_btn, pct(50), UI_TEXT_BTN_HEIGHT);
        grow(nav_btn, 1);
        Handle save_btn = button(loc_actions, i18n::t(i18n::T_SAVE), on_save, nullptr);
        size(save_btn, pct(50), UI_TEXT_BTN_HEIGHT);
        grow(save_btn, 1);
    }

    Handle actions = row(content);
    size(actions, pct(100), UI_TEXT_BTN_HEIGHT);
    gap(actions, UI_MENU_ITEM_PAD);

    if (!msg.is_self) {
        Handle reply_btn = button(actions, i18n::t(i18n::T_REPLY), on_reply, nullptr);
        size(reply_btn, pct(50), UI_TEXT_BTN_HEIGHT);
        grow(reply_btn, 1);
    }
    Handle del_btn = button(actions, i18n::t(i18n::T_DELETE), on_delete, nullptr);
    size(del_btn, pct(msg.is_self ? 100 : 50), UI_TEXT_BTN_HEIGHT);
    grow(del_btn, 1);
}

static void entry() {}
static void exit_fn() {}
static void destroy() { msg_idx = -1; has_loc = false; loc_label[0] = 0; }

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::msg_detail
