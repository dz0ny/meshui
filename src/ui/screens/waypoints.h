#pragma once
#include "../ui_screen_mgr.h"   // screen_lifecycle_t

// Waypoints list — user-marked GPS points. "Mark here" snapshots the current
// fix; tapping a row opens its detail (navigate / send / delete).

namespace ui::screen::waypoints {
extern screen_lifecycle_t lifecycle;
}
