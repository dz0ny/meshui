#pragma once

#include <stdint.h>

namespace ui::toast {

// Show a toast message overlay. Auto-disappears after timeout_ms.
void show(const char* text, uint32_t timeout_ms = 2000);

// Drop the cached toast object. Call after a theme change so the next show()
// rebuilds it with the new palette.
void destroy();

} // namespace ui::toast
