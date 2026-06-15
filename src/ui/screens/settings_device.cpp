#include <Arduino.h>
#include "settings_device.h"
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../../board.h"
#include "../../mesh/mesh_task.h"

extern void do_power_off();

namespace ui::screen::settings_device {

using namespace ui::kit;

static void on_reboot(void*) {
    mesh::task::flush_storage();
    ESP.restart();
}

static void on_power_off(void*) { do_power_off(); }

static void create(Handle parent) {
    Handle lst = list(parent);

    menu_row(lst, "Reboot",    on_reboot,    nullptr);
    menu_row(lst, "Power Off", on_power_off, nullptr);

    char buf[64];
    snprintf(buf, sizeof(buf), "FW: %s  HW: %s", T_PAPER_FW_VERSION, T_PAPER_HW_VERSION);
    Handle ver = label(lst, buf);
    font(ver, Font::Small);
}

static void entry() {}
static void exit_fn() {}
static void destroy() {}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::settings_device
