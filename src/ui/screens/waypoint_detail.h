#pragma once
#include "../ui_screen_mgr.h"   // screen_lifecycle_t

// Actions for a single waypoint: navigate (compass), send over mesh, delete.
// The list screen calls set_index() before pushing.

namespace ui::screen::waypoint_detail {
extern screen_lifecycle_t lifecycle;
void set_index(int idx);
}
