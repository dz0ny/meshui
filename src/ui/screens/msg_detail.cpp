#include "msg_detail.h"
#include "compose.h"
#include "../ui_theme.h"          // SCREEN_* ids + UI_* layout
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../../model.h"
#include "../../sd_log.h"

// Single message + reply/delete actions — ported to the ui::kit facade.

namespace ui::screen::msg_detail {

using namespace ui::kit;

static int msg_idx = -1;

void set_message(int idx) { msg_idx = idx; }

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

    Handle actions = row(content);
    size(actions, pct(100), UI_TEXT_BTN_HEIGHT);
    gap(actions, UI_MENU_ITEM_PAD);

    if (!msg.is_self) {
        Handle reply_btn = button(actions, "Reply", on_reply, nullptr);
        size(reply_btn, pct(50), UI_TEXT_BTN_HEIGHT);
        grow(reply_btn, 1);
    }
    Handle del_btn = button(actions, "Delete", on_delete, nullptr);
    size(del_btn, pct(msg.is_self ? 100 : 50), UI_TEXT_BTN_HEIGHT);
    grow(del_btn, 1);
}

static void entry() {}
static void exit_fn() {}
static void destroy() { msg_idx = -1; }

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::msg_detail
