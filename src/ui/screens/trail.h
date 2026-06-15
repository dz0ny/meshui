#pragma once

#include "../ui_screen_mgr.h"

// GPS breadcrumb trail viewer. Home > Trail.
// Canvas map that auto-fits the recorded polyline + live position, a stats
// overlay (status / points / distance / time / speed), and Start-Stop / Clear
// touch controls. Recording itself runs in the background (model::update_gps).

namespace ui::screen::trail {

extern screen_lifecycle_t lifecycle;

} // namespace ui::screen::trail
