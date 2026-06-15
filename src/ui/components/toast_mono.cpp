// Mono backend for ui::toast — the LVGL toast.cpp is excluded from the nRF52
// build, so this forwards show()/destroy() to the mono kit's banner overlay.
#ifdef BOARD_WIO_L1

#include "toast.h"
#include "../kit/ui_kit_mono.h"

namespace ui::toast {

void show(const char* text, uint32_t timeout_ms) {
    ui::kit::mono::toast(text, timeout_ms);
}

void destroy() {}   // mono toast holds no retained objects

} // namespace ui::toast

#endif // BOARD_WIO_L1
