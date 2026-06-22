#include <cstdio>
#include <cstring>
#include "chat.h"
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../i18n.h"
#include "../../model.h"
#ifdef BOARD_WIO_L1
  #include "../screen_ids.h"
  #include "../../mesh/mesh_task.h"   // active-channel selection + enumeration
#endif

// Messages (chat) — ported to the ui::kit facade.
//
// On the Wio mono build the list is filtered to a single selected channel
// (chosen from the MeshCore channel list under Mesh Settings); the other builds
// show every message.

namespace ui::screen::chat {

using namespace ui::kit;

static Handle msg_container = nullptr;
static int last_displayed = 0;

#ifdef BOARD_WIO_L1
static uint8_t active_channel = 0;

static void on_reply(void*) { ui::screen_mgr::push(SCREEN_QUICKREPLY, true); }

// Whether a stored message belongs to the channel the user is currently viewing.
static bool msg_visible(const model::StoredMessage& m) {
    return m.channel_idx == active_channel;
}
#else
static bool msg_visible(const model::StoredMessage&) { return true; }
#endif

// Build (or rebuild) all message rows into msg_container. On the button-driven
// Wio the newest message goes on TOP, so opening Messages shows the latest info
// without scrolling down; touch builds keep the conventional newest-at-bottom
// order and auto-scroll to it.
static void populate() {
    msg_clear(msg_container);

    int visible = 0;
    for (int i = 0; i < model::message_count; i++) {
        if (msg_visible(model::messages[i])) visible++;
    }

    if (visible == 0) {
        static char emptybuf[40];
        snprintf(emptybuf, sizeof(emptybuf), "\n\n\n%s", i18n::t(i18n::T_NO_MESSAGES));
        Handle empty = label(msg_container, emptybuf);
        size(empty, pct(100), CONTENT);
        grow(empty, 1);
        font(empty, Font::Title);
        text_center(empty);
    } else {
#ifdef BOARD_WIO_L1
        for (int i = model::message_count - 1; i >= 0; i--) {   // newest first → top
#else
        for (int i = 0; i < model::message_count; i++) {        // oldest first → newest at bottom
#endif
            auto& msg = model::messages[i];
            if (!msg_visible(msg)) continue;
            msg_append(msg_container, msg.sender, msg.text, msg.is_self, i);
        }
        msg_scroll_bottom(msg_container);   // touch: jump to newest; mono: no-op (top is newest)
    }
    last_displayed = model::message_count;
}

void process_events() {
    if (!msg_container) return;
    if (last_displayed == model::message_count) return;   // count unchanged → nothing to redraw
    populate();                                           // add/delete both reorder → full rebuild
    focus_refresh();
}

static void create(Handle parent) {
#ifdef BOARD_WIO_L1
    active_channel = mesh::task::get_msg_channel();
#endif

    msg_container = msglist(parent);
    size(msg_container, pct(100), pct(100));
#ifdef BOARD_WIO_L1
    grow(msg_container, 1);   // fill the space between the header and Reply button
#endif

    populate();

#ifdef BOARD_WIO_L1
    // Keyboard-less reply: a focusable button that opens the quick-reply menu.
    Handle reply = button(parent, i18n::t(i18n::T_REPLY), on_reply, nullptr);
    size(reply, pct(100), CONTENT);
#endif

    model::clear_unread_messages();
}

static void entry() { process_events(); }
static void exit_fn() {}
static void destroy() { msg_container = nullptr; last_displayed = 0; }

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::chat
