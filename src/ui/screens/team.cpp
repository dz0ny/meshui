#include <cstdio>
#include <cstring>
#include <cstdint>
#include "team.h"
#include "../screen_ids.h"
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../../model.h"
#include "../../util/text_filter.h"
#include "compass.h"   // mono: tapping a member aims the compass needle at them
// geo_utils defines ui::geo::DEG_TO_RAD; make sure no stray Arduino macro shadows it.
#ifdef DEG_TO_RAD
#undef DEG_TO_RAD
#endif
#include "../components/geo_utils.h"

// Team screen — favorited chat-type contacts. Each row: member name on the left,
// and on the right a compact status: "(unread) <dist> <cardinal> <age>".
// Tapping a member clears its unread tally and opens Messages.

namespace ui::screen::team {

using namespace ui::kit;

// Live beacon position for a contact, matched by the first 6 pub-key bytes.
static const model::LivePosition* find_live(const uint8_t* pub_key) {
    for (int i = 0; i < model::live_position_count; i++) {
        const model::LivePosition& p = model::live_positions[i];
        if (p.valid && memcmp(p.pub_key_prefix, pub_key, 6) == 0) return &p;
    }
    return nullptr;
}

// "now" / "5m" / "2h" / "3d" — relative to model::epoch_now. Empty if unknown.
static void fmt_age(char* buf, size_t n, uint32_t ts) {
    if (ts == 0 || model::epoch_now == 0 || model::epoch_now < ts) { buf[0] = 0; return; }
    uint32_t s = model::epoch_now - ts;
    if (s < 60)         snprintf(buf, n, "now");
    else if (s < 3600)  snprintf(buf, n, "%lum", (unsigned long)(s / 60));
    else if (s < 86400) snprintf(buf, n, "%luh", (unsigned long)(s / 3600));
    else                snprintf(buf, n, "%lud", (unsigned long)(s / 86400));
}

// Compose the right-hand status string for one member.
static void build_status(const model::ContactEntry& c, char* out, size_t n) {
    out[0] = 0;
    size_t off = 0;

    uint16_t unread = model::contact_unread(c.name);
    if (unread > 0)
        off += snprintf(out + off, n - off, "(%u) ", (unsigned)unread);

    // Prefer a fresh fast-GPS beacon; fall back to the contact's advertised fix.
    const model::LivePosition* lp = find_live(c.pub_key);
    double plat = 0, plon = 0;
    bool have_pos = false;
    uint32_t ts = 0;
    if (lp && (lp->lat_e6 || lp->lon_e6)) {
        plat = lp->lat_e6 / 1e6; plon = lp->lon_e6 / 1e6; have_pos = true; ts = lp->timestamp;
    } else if (c.gps_lat || c.gps_lon) {
        plat = c.gps_lat / 1e6;  plon = c.gps_lon / 1e6;  have_pos = true;
    }

    bool have_self = model::gps.has_fix && (model::gps.lat != 0.0 || model::gps.lng != 0.0);
    if (have_pos && have_self) {
        double dist = ui::geo::distance_km(model::gps.lat, model::gps.lng, plat, plon);
        double bear = ui::geo::bearing(model::gps.lat, model::gps.lng, plat, plon);
        const char* card = ui::geo::bearing_to_cardinal(bear);
        if (dist < 1.0) off += snprintf(out + off, n - off, "%dm %s", (int)(dist * 1000), card);
        else            off += snprintf(out + off, n - off, "%.1fkm %s", dist, card);
    }

    char age[16];
    fmt_age(age, sizeof(age), ts);
    if (age[0])
        off += snprintf(out + off, n - off, "%s%s", off > 0 ? " " : "", age);
}

static void on_member(void* u) {
    int idx = (int)(intptr_t)u;
    if (idx < 0 || idx >= model::contact_count) return;
    const model::ContactEntry& c = model::contacts[idx];
    model::clear_contact_unread(c.name);
#if UI_DISPLAY_MONO
    // Mono (Wio): point the dead-reckoning compass at this member's last-known
    // position — prefer a fresh fast-GPS beacon, fall back to the advertised fix.
    const model::LivePosition* lp = find_live(c.pub_key);
    int32_t lat_e6 = (lp && (lp->lat_e6 || lp->lon_e6)) ? lp->lat_e6 : c.gps_lat;
    int32_t lon_e6 = (lp && (lp->lat_e6 || lp->lon_e6)) ? lp->lon_e6 : c.gps_lon;
    ui::screen::compass::set_target(c.pub_key, c.name, lat_e6, lon_e6);
    ui::screen_mgr::push(SCREEN_COMPASS, true);
#else
    ui::screen_mgr::push(SCREEN_CHAT, true);
#endif
}

static void create(Handle parent) {
    model::refresh_contacts();   // pull the latest favorites / flags from the radio

    Handle lst = list(parent);
    int shown = 0;
    for (int i = 0; i < model::contact_count; i++) {
        const model::ContactEntry& c = model::contacts[i];
        if (!model::is_team_member(c)) continue;

        char name[32];
        strncpy(name, c.name, sizeof(name) - 1);
        name[sizeof(name) - 1] = 0;
        util::strip_emoji_inplace(name);   // e-ink fonts have no emoji glyphs

        char status[48];
        build_status(c, status, sizeof(status));

        toggle_item(lst, name, status, on_member, (void*)(intptr_t)i);
        shown++;
    }

    if (shown == 0) {
        Handle empty = label(lst, "\n\n\nNo team members");
        size(empty, pct(100), CONTENT);
        grow(empty, 1);
        font(empty, Font::Title);
        text_center(empty);
    }
}

static void entry() {}
static void exit_fn() {}
static void destroy() {}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::team
