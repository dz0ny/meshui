#include "set_sound.h"
#ifdef BOARD_WIO_L1
// ---------------------------------------------------------------------------
// Sound settings (Wio mono build). Currently just the buzzer toggle, split out
// of the Display screen so audio settings have their own home.
// ---------------------------------------------------------------------------
#include "../kit/ui_kit.h"
#include "../i18n.h"
#include "../../mesh/mesh_task.h"

namespace ui::screen::set_sound {

using namespace ui::kit;

static Handle lbl_buzzer = nullptr;

static void on_buzzer(void*) {
    bool en = !mesh::task::get_buzzer_enabled();
    mesh::task::set_buzzer_enabled(en);
    if (lbl_buzzer) set_text(lbl_buzzer, i18n::t(en ? i18n::T_ON : i18n::T_OFF));
}

static void create(Handle parent) {
    Handle lst = list(parent);
    lbl_buzzer = toggle_item(lst, i18n::t(i18n::T_BUZZER),
                             i18n::t(mesh::task::get_buzzer_enabled() ? i18n::T_ON : i18n::T_OFF),
                             on_buzzer, nullptr);
}

static void entry() {}
static void exit_fn() {}
static void destroy() { lbl_buzzer = nullptr; }

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::set_sound
#endif // BOARD_WIO_L1
