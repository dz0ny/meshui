#include <cstdio>
#include "waypoints.h"
#include "waypoint_detail.h"
#include "../screen_ids.h"
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../components/toast.h"
#include "../../model.h"
#include "../../waypoint_store.h"
// Arduino.h's DEG_TO_RAD macro would clobber ui::geo's; drop it first.
#ifdef DEG_TO_RAD
#undef DEG_TO_RAD
#endif
#include "../components/geo_utils.h"

// Waypoints list — "Mark here" plus one tappable row per saved point.

namespace ui::screen::waypoints {

using namespace ui::kit;

static void on_mark_here(void*) {
    if (!model::gps.has_fix) { ui::toast::show("No GPS fix"); return; }
    if (model::waypoints.full()) { ui::toast::show("Waypoints full"); return; }
    bool ok = model::waypoints.add((int32_t)(model::gps.lat * 1e6),
                                   (int32_t)(model::gps.lng * 1e6),
                                   model::epoch_now, nullptr);
    ui::toast::show(ok ? "Waypoint marked" : "Waypoints full");
    ui::screen_mgr::reload_stack();   // rebuild so the new row appears
}

static void on_row(void* user) {
    int idx = (int)(intptr_t)user;
    ui::screen::waypoint_detail::set_index(idx);
    ui::screen_mgr::push(SCREEN_WAYPOINT_DETAIL, true);
}

static void create(Handle parent) {
    Handle lst = list(parent);
    gap(lst, 2);

    menu_row(lst, "+ Mark here", on_mark_here, nullptr);

    int n = model::waypoints.count();
    if (n == 0) {
        Handle empty = label(lst, "\n\n\nNo waypoints yet");
        size(empty, pct(100), CONTENT);
        grow(empty, 1);
        font(empty, Font::Title);
        text_center(empty);
        return;
    }

    bool have_gps = model::gps.has_fix;
    for (int i = 0; i < n; i++) {
        const Waypoint& w = model::waypoints.at(i);
        char row[48];
        if (have_gps) {
            double dm = ui::geo::distance_km(model::gps.lat, model::gps.lng,
                                             w.lat_1e6 / 1e6, w.lon_1e6 / 1e6) * 1000.0;
            if (dm < 1000.0) snprintf(row, sizeof(row), "%s  %dm", w.label, (int)(dm + 0.5));
            else             snprintf(row, sizeof(row), "%s  %.1fkm", w.label, dm / 1000.0);
        } else {
            snprintf(row, sizeof(row), "%s", w.label);
        }
        menu_row(lst, row, on_row, (void*)(intptr_t)i);
    }
}

static void entry() {}
static void exit_fn() {}
static void destroy() {}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::waypoints
