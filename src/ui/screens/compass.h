#pragma once
#include <stdint.h>
#include "../ui_screen_mgr.h"   // screen_lifecycle_t

// Compass screen (Wio mono). A dead-reckoning needle pointing toward a selected
// peer. The target is chosen from the Team screen (set_target before push).

namespace ui::screen::compass {
extern screen_lifecycle_t lifecycle;

// Pick the peer the compass should point at (called from the Team screen before push).
// Copies the prefix + last-known position; the compass re-looks-up the live
// position by prefix on each tick so it tracks fresh beacons.
void set_target(const uint8_t* prefix6, const char* name,
                int32_t lat_e6, int32_t lon_e6);

// Point at a fixed coordinate (a waypoint or a location shared in a message).
// Unlike set_target() this isn't tied to a node, so the position is static.
void set_target_pos(const char* name, int32_t lat_e6, int32_t lon_e6);
}
