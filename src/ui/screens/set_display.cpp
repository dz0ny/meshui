#include "set_display.h"
#ifdef BOARD_WIO_L1
// ---------------------------------------------------------------------------
// Wio mono build: the Display screen is just a Language picker. The full ESP
// display screen below (refresh mode, backlight, brightness, themes, sleep)
// depends on the LVGL port / NVS / theme engine which the mono build lacks.
// ---------------------------------------------------------------------------
#include "../ui_screen_mgr.h"
#include "../kit/ui_kit.h"
#include "../i18n.h"
#include "../components/toast.h"
#include "../../model.h"
#include "../../mesh/mesh_task.h"
#include <InternalFileSystem.h>
#include <cstdio>

namespace ui::screen::set_display {

using namespace ui::kit;

static Handle lbl_lang    = nullptr;
static Handle lbl_tz      = nullptr;
static Handle lbl_msgchan = nullptr;
static Handle lbl_buzzer  = nullptr;

static void save_language(uint8_t l) {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.remove("/lang");
    File f = InternalFS.open("/lang", FILE_O_WRITE);
    if (f) { f.write(&l, 1); f.close(); }
}

static void save_timezone(int8_t tz) {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.remove("/tz");
    File f = InternalFS.open("/tz", FILE_O_WRITE);
    if (f) { f.write((const uint8_t*)&tz, 1); f.close(); }
}

// "UTC", "UTC+1", "UTC-5" — clock.tz_offset_hours is the single source of truth.
static void fmt_tz(char* buf, size_t n, int8_t tz) {
    if (tz == 0)      snprintf(buf, n, "UTC");
    else if (tz > 0)  snprintf(buf, n, "UTC+%d", tz);
    else              snprintf(buf, n, "UTC%d", tz);
}

static void on_tz(void*) {
    int8_t tz = model::clock.tz_offset_hours;
    tz++;
    if (tz > 14) tz = -12;          // wrap across the usable UTC-12 .. UTC+14 range
    model::clock.tz_offset_hours = tz;
    save_timezone(tz);
    if (lbl_tz) { char b[12]; fmt_tz(b, sizeof(b), tz); set_text(lbl_tz, b); }
}

// ---- Messages channel picker ----------------------------------------------
// Selects which channel the chat screen shows. Cycles through the full channel
// list (Public included). Persisted as a byte in LittleFS by mesh::task.
static char chanbuf[24];

static const char* msgchan_label() {
    uint8_t sel = mesh::task::get_msg_channel();
    mesh::task::ChannelEntry chans[16];
    int n = mesh::task::get_channels(chans, 16);
    // Copy into the persistent buffer — chans[] is a stack local, so returning
    // chans[i].name directly would dangle by the time the caller reads it.
    for (int i = 0; i < n; i++)
        if (chans[i].idx == sel) { snprintf(chanbuf, sizeof(chanbuf), "%s", chans[i].name); return chanbuf; }
    snprintf(chanbuf, sizeof(chanbuf), "Ch %d", (int)sel);
    return chanbuf;
}
static void on_msgchan(void*) {
    mesh::task::ChannelEntry chans[16];
    int n = mesh::task::get_channels(chans, 16);
    if (n == 0) return;
    uint8_t sel = mesh::task::get_msg_channel();
    int cur = -1;
    for (int i = 0; i < n; i++) if (chans[i].idx == sel) { cur = i; break; }
    int next = (cur + 1) % n;
    mesh::task::set_msg_channel(chans[next].idx);
    if (lbl_msgchan) set_text(lbl_msgchan, chans[next].name);
}

static void on_buzzer(void*) {
    bool en = !mesh::task::get_buzzer_enabled();
    mesh::task::set_buzzer_enabled(en);
    if (lbl_buzzer) set_text(lbl_buzzer, i18n::t(en ? i18n::T_ON : i18n::T_OFF));
}

static void on_lang(void*) {
    uint8_t next = (i18n::get_lang() + 1) % i18n::LANG_COUNT;
    i18n::set_lang(next);
    save_language(next);
    if (lbl_lang) set_text(lbl_lang, i18n::t(next == i18n::SL ? i18n::T_LANG_SL : i18n::T_LANG_EN));
    // Rebuild the whole stack so every open screen re-reads its strings.
    ui::screen_mgr::reload_stack();
}

static void create(Handle parent) {
    Handle lst = list(parent);
    lbl_lang = toggle_item(lst, i18n::t(i18n::T_LANGUAGE),
                           i18n::t(i18n::get_lang() == i18n::SL ? i18n::T_LANG_SL : i18n::T_LANG_EN),
                           on_lang, nullptr);
    char tzb[12]; fmt_tz(tzb, sizeof(tzb), model::clock.tz_offset_hours);
    lbl_tz = toggle_item(lst, i18n::t(i18n::T_TIMEZONE), tzb, on_tz, nullptr);
    lbl_msgchan = toggle_item(lst, i18n::t(i18n::T_MSG_CHANNEL), msgchan_label(), on_msgchan, nullptr);
    lbl_buzzer = toggle_item(lst, "Buzzer",
                             i18n::t(mesh::task::get_buzzer_enabled() ? i18n::T_ON : i18n::T_OFF),
                             on_buzzer, nullptr);
}

static void entry() {}
static void exit_fn() {}
static void destroy() { lbl_lang = nullptr; lbl_tz = nullptr; lbl_msgchan = nullptr; lbl_buzzer = nullptr; }

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::set_display

#else  // !BOARD_WIO_L1 — full ESP/LVGL display settings

#include "../ui_theme.h"
#include "../ui_screen_mgr.h"
#include "../ui_port.h"
#include "../kit/ui_kit.h"
#include "../components/statusbar.h"
#include "../components/toast.h"
#include "../../nvs_param.h"
#include "../../model.h"

namespace ui::screen::set_display {

using namespace ui::kit;

static Handle lbl_refresh_val = nullptr;
static Handle lbl_backlight_val = nullptr;
static Handle lbl_brightness_val = nullptr;
static Handle lbl_sleep_val = nullptr;
static Handle lbl_theme_val = nullptr;
static const char* mode_names[] = {"Normal", "Fast"};
static const char* sleep_names[] = {"Off", "1 min", "2 min", "5 min", "15 min", "30 min"};
static const uint32_t sleep_ms[] = {0, 60000, 120000, 300000, 900000, 1800000};

static void apply_theme_async(void* data) {
    (void)data;
    ui::toast::destroy();
    ui::statusbar::recreate();
    ui::screen_mgr::reload_stack();
    ui::toast::show(ui::theme::current_name());
}

static void on_sleep_cycle(void*) {
    uint8_t idx = (model::sleep_cfg.timeout_idx + 1) % 6;
    model::sleep_cfg.timeout_idx = idx;
    model::sleep_cfg.timeout_ms = sleep_ms[idx];
    nvs_param_set_u8(NVS_ID_SLEEP_TIMEOUT, idx);
    model::touch_activity();
    if (lbl_sleep_val) set_text(lbl_sleep_val, sleep_names[idx]);
}

static void on_refresh_mode(void*) {
    int mode = ui::port::get_refresh_mode();
    mode = (mode + 1) % 2;
    ui::port::set_refresh_mode(mode);
    nvs_param_set_u8(NVS_ID_REFRESH_MODE, mode);
    if (lbl_refresh_val) set_text(lbl_refresh_val, mode_names[mode]);
}

static void on_backlight_cycle(void*) {
    int mode = (ui::port::get_backlight() + 1) % 3;
    ui::port::set_backlight(mode);
    nvs_param_set_u8(NVS_ID_BACKLIGHT, mode);
    if (lbl_backlight_val) set_text(lbl_backlight_val, ui::port::get_backlight_name());
}

static void on_brightness_cycle(void*) {
    int level = (ui::port::get_brightness() + 1) % 3;
    ui::port::set_brightness(level);
    nvs_param_set_u8(NVS_ID_BRIGHTNESS, level);
    // Re-apply if backlight is currently on
    if (ui::port::get_backlight() == 1) ui::port::apply_backlight();
    if (lbl_brightness_val) set_text(lbl_brightness_val, ui::port::get_brightness_name());
}

static void on_theme_cycle(void*) {
    ui::theme::theme_id next_theme = ui::theme::next();
    if (!ui::theme::set(next_theme)) return;

    nvs_param_set_u8(NVS_ID_UI_THEME, static_cast<uint8_t>(next_theme));
    defer(apply_theme_async, nullptr);
}

static void create(Handle parent) {
    Handle lst = list(parent);

#ifdef BOARD_EPAPER
    lbl_refresh_val = toggle_item(lst, "Refresh", mode_names[ui::port::get_refresh_mode()], on_refresh_mode, nullptr);
#endif
#ifdef BOARD_TDECK
    lbl_theme_val = toggle_item(lst, "Theme", ui::theme::current_name(), on_theme_cycle, nullptr);
#endif
#ifdef BOARD_EPAPER
    lbl_backlight_val = toggle_item(lst, "Light", ui::port::get_backlight_name(), on_backlight_cycle, nullptr);
#endif
    lbl_brightness_val = toggle_item(lst, "Brightness", ui::port::get_brightness_name(), on_brightness_cycle, nullptr);
    lbl_sleep_val = toggle_item(lst, "Sleep", sleep_names[model::sleep_cfg.timeout_idx], on_sleep_cycle, nullptr);
}

static void entry() {}
static void exit_fn() {}
static void destroy() {
    lbl_refresh_val = nullptr;
    lbl_backlight_val = nullptr;
    lbl_brightness_val = nullptr;
    lbl_sleep_val = nullptr;
    lbl_theme_val = nullptr;
}

screen_lifecycle_t lifecycle = { create, entry, exit_fn, destroy };

} // namespace ui::screen::set_display
#endif // BOARD_WIO_L1
