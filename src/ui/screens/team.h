#pragma once

#include "../ui_screen_mgr.h"

// Team screen — lists the user's favorited chat-type contacts ("team members")
// with last-heard age, distance/bearing, and an unread-message badge. Portable
// across the LVGL and mono backends (built against ui::kit).

namespace ui::screen::team {

extern screen_lifecycle_t lifecycle;

} // namespace ui::screen::team
