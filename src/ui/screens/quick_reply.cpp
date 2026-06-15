#include <cstdio>
#include <cstring>
#include <cstdint>
#include "quick_reply.h"
#include "../screen_ids.h"
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../i18n.h"
#include "../components/toast.h"
#include "../../model.h"
#include "../../mesh/mesh_task.h"

// Quick-reply screen (Wio mono, keyboard-less). A short menu of one-tap messages
// sent to the channel the Messages screen is currently showing:
//   - GPS location (a geo: URI built from the current fix)
//   - a handful of predefined phrases
// "Type message" (an on-screen char picker) is a future addition.

namespace ui::screen::quick_reply {

using namespace ui::kit;

// Predefined phrases, by i18n id. The displayed text IS the transmitted text, so
// it goes out in the user's selected language (ASCII-safe Slovenian on mono).
static const i18n::Str phrase_ids[] = {
    i18n::T_OK, i18n::T_QR_OMW, i18n::T_QR_YES,
    i18n::T_QR_NO, i18n::T_QR_HELP, i18n::T_QR_ARRIVED,
};
static const int n_phrases = (int)(sizeof(phrase_ids) / sizeof(phrase_ids[0]));

// Send `text` to the active Messages channel, echo it locally as a self-message
// (group sends don't loop back), toast the result, and return to the chat.
static void send_quick(const char* text) {
    if (!text || !text[0]) return;
    uint8_t ch = mesh::task::get_msg_channel();
    bool ok = mesh::task::send_channel(ch, text);

    if (ok && model::messages && model::message_count < MAX_STORED_MESSAGES) {
        model::StoredMessage& m = model::messages[model::message_count];
        memset(&m, 0, sizeof(m));
        const char* me = mesh::task::node_name();
        if (me) strncpy(m.sender, me, sizeof(m.sender) - 1);
        strncpy(m.text, text, sizeof(m.text) - 1);
        m.hour = model::clock.hour;
        m.minute = model::clock.minute;
        m.is_self = true;
        m.channel_idx = ch;
        model::message_count++;
    }

    ui::toast::show(i18n::t(ok ? i18n::T_SENT : i18n::T_SEND_FAILED));
    ui::screen_mgr::pop(true);   // back to chat — rebuild shows the echoed message
}

static void on_gps_loc(void*) {
    if (!model::gps.has_fix) {
        ui::toast::show(i18n::t(i18n::T_NO_GPS_FIX));
        return;
    }
    char buf[48];
    snprintf(buf, sizeof(buf), "geo:%.6f,%.6f", model::gps.lat, model::gps.lng);
    send_quick(buf);
}

static void on_phrase(void* user) {
    int idx = (int)(intptr_t)user;
    if (idx < 0 || idx >= n_phrases) return;
    send_quick(i18n::t(phrase_ids[idx]));
}

static void create(Handle parent) {
    Handle lst = list(parent);
    menu_row(lst, i18n::t(i18n::T_QR_GPS_LOC), on_gps_loc, nullptr);
    for (int i = 0; i < n_phrases; i++) {
        menu_row(lst, i18n::t(phrase_ids[i]), on_phrase, (void*)(intptr_t)i);
    }
}

static void entry() {}
static void exit_fn() {}
static void destroy() {}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::quick_reply
