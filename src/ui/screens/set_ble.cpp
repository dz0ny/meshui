#include "set_ble.h"
#ifdef BOARD_WIO_L1
// ---------------------------------------------------------------------------
// Wio mono build: Bluetooth = BLE companion on/off + pairing PIN. The enabled
// state and PIN persist via mesh::task (LittleFS / NodePrefs); no NVS here.
// ---------------------------------------------------------------------------
#include <cstdio>
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../i18n.h"
#include "../components/toast.h"
#include "../../mesh/mesh_task.h"

namespace ui::screen::set_ble {

using namespace ui::kit;

static Handle lbl_ble = nullptr;
static Handle lbl_pin = nullptr;

static void on_ble_toggle(void*) {
    if (mesh::task::ble_is_enabled()) {
        mesh::task::ble_disable();
        if (lbl_ble) set_text(lbl_ble, i18n::t(i18n::T_OFF));
        ui::toast::show(i18n::t(i18n::T_OFF));
    } else {
        mesh::task::ble_enable();
        if (lbl_ble) set_text(lbl_ble, i18n::t(i18n::T_ON));
        ui::toast::show(i18n::t(i18n::T_ON));
    }
}

static void on_pin_regen(void*) {
    uint32_t pin = mesh::task::regen_ble_pin();
    if (lbl_pin) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)pin);
        set_text(lbl_pin, buf);
    }
}

static void create(Handle parent) {
    Handle lst = list(parent);

    lbl_ble = toggle_item(lst, "BLE", i18n::t(mesh::task::ble_is_enabled() ? i18n::T_ON : i18n::T_OFF),
                          on_ble_toggle, nullptr);

    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)mesh::task::get_ble_pin());
    lbl_pin = toggle_item(lst, "PIN", buf, on_pin_regen, nullptr);
}

static void entry() {}
static void exit_fn() {}
static void destroy() { lbl_ble = lbl_pin = nullptr; }

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::set_ble

#else  // !BOARD_WIO_L1 — full ESP/LVGL Bluetooth settings

#include <cstdio>
#include <esp_random.h>
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../../mesh/mesh_task.h"
#include "../../nvs_param.h"

// Bluetooth settings — ported to the ui::kit facade.

namespace ui::screen::set_ble {

using namespace ui::kit;

static Handle lbl_ble = nullptr;
static Handle lbl_pin = nullptr;

static void on_ble_toggle(void*) {
    if (mesh::task::ble_is_enabled()) {
        mesh::task::ble_disable();
        nvs_param_set_u8(NVS_ID_BLE_ENABLED, 0);
        if (lbl_ble) set_text(lbl_ble, "Off");
    } else {
        mesh::task::ble_enable();
        nvs_param_set_u8(NVS_ID_BLE_ENABLED, 1);
        if (lbl_ble) set_text(lbl_ble, "On");
    }
}

static void on_pin_regen(void*) {
    uint32_t pin = esp_random() % 900000 + 100000;
    mesh::task::set_ble_pin(pin);
    if (lbl_pin) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)pin);
        set_text(lbl_pin, buf);
    }
}

static void create(Handle parent) {
    Handle lst = list(parent);

    lbl_ble = toggle_item(lst, "BLE", mesh::task::ble_is_enabled() ? "On" : "Off", on_ble_toggle, nullptr);

    char buf[32];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)mesh::task::get_ble_pin());
    lbl_pin = toggle_item(lst, "PIN", buf, on_pin_regen, nullptr);
}

static void entry() {}
static void exit_fn() {}
static void destroy() { lbl_ble = lbl_pin = nullptr; }

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::set_ble
#endif // BOARD_WIO_L1
