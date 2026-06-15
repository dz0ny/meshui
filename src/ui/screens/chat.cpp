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

// Human-readable name of the active channel (falls back to a numbered label).
static const char* active_channel_name() {
    static char buf[24];
    mesh::task::ChannelEntry chans[16];
    int n = mesh::task::get_channels(chans, 16);
    for (int i = 0; i < n; i++) {
        if (chans[i].idx == active_channel) return chans[i].name;
    }
    snprintf(buf, sizeof(buf), "Ch %d", (int)active_channel);
    return buf;
}
#else
static bool msg_visible(const model::StoredMessage&) { return true; }
#endif

void process_events() {
    if (!msg_container) return;

    // Rebuild if a message was deleted (indices shifted).
    if (last_displayed > model::message_count) {
        msg_clear(msg_container);
        for (int i = 0; i < model::message_count; i++) {
            auto& msg = model::messages[i];
            if (!msg_visible(msg)) continue;
            msg_append(msg_container, msg.sender, msg.text, msg.is_self, i);
        }
        last_displayed = model::message_count;
        msg_scroll_bottom(msg_container);
        focus_refresh();
        return;
    }

    bool changed = false;
    while (last_displayed < model::message_count) {
        auto& msg = model::messages[last_displayed];
        if (msg_visible(msg)) {
            msg_append(msg_container, msg.sender, msg.text, msg.is_self, last_displayed);
            changed = true;
        }
        last_displayed++;
    }
    if (changed) {
        msg_scroll_bottom(msg_container);
        focus_refresh();
    }
}

static void create(Handle parent) {
#ifdef BOARD_WIO_L1
    active_channel = mesh::task::get_msg_channel();
    // One-line header so the user knows which channel they are viewing.
    Handle hdr = label(parent, active_channel_name());
    size(hdr, pct(100), CONTENT);
    font(hdr, Font::Small);
    text_center(hdr);
#endif

    msg_container = msglist(parent);
    size(msg_container, pct(100), pct(100));
#ifdef BOARD_WIO_L1
    grow(msg_container, 1);   // fill the space between the header and Reply button
#endif

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
        for (int i = 0; i < model::message_count; i++) {
            auto& msg = model::messages[i];
            if (!msg_visible(msg)) continue;
            msg_append(msg_container, msg.sender, msg.text, msg.is_self, i);
        }
        msg_scroll_bottom(msg_container);
    }
    last_displayed = model::message_count;

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
