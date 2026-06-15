#pragma once
#include "../ui_screen_mgr.h"   // screen_lifecycle_t

// Privacy settings (Wio mono). Currently hosts the "share my GPS location in
// the advert packet" toggle — moved here out of Mesh settings so radio config
// and privacy choices live in separate places.

namespace ui::screen::set_privacy {
extern screen_lifecycle_t lifecycle;
}
