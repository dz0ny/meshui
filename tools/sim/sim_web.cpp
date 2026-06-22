// Interactive WASM harness for the mono (Wio) UI.
//
// Where sim_main.cpp renders ONE screen to a PNG, this drives the *real* mono
// screen manager (ui_screen_mgr_mono.cpp) with the real screen lifecycles, so
// you can navigate the whole UI in a browser with the joystick keys. It mirrors
// the navigation glue from src/main_wio.cpp (home menu, dashboard, dispatch_key,
// status bar, title_for) — the parts that live in the device's setup()/loop()
// instead of in a screen .cpp.
//
// Built by build_web.sh into web/sim.js + web/sim.wasm. The page reads the
// grayscale framebuffer (sim_pixels/sim_w/sim_h) into a <canvas> after each
// sim_key()/sim_tick().
#include "sim_arduino.h"
#include "../../src/ui/kit/ui_kit.h"
#include "../../src/ui/kit/ui_kit_mono.h"
#include "../../src/ui/ui_screen_mgr.h"
#include "../../src/ui/screen_ids.h"
#include "../../src/model.h"
#include "../../src/mesh/mesh_task.h"
#include "../../src/ui/i18n.h"
#include <cstdio>
#include <cstring>
#include <emscripten.h>

namespace model { void sim_seed(); }
using namespace ui::kit;

// ---- screen lifecycles (defined in each screen's own .cpp) ------------------
namespace ui { namespace screen {
#define SIM_SCREEN(ns) namespace ns { extern screen_lifecycle_t lifecycle; }
    SIM_SCREEN(chat) SIM_SCREEN(quick_reply) SIM_SCREEN(status) SIM_SCREEN(settings)
    SIM_SCREEN(gps) SIM_SCREEN(mesh_settings) SIM_SCREEN(set_gps) SIM_SCREEN(set_mesh)
    SIM_SCREEN(set_display) SIM_SCREEN(set_sound) SIM_SCREEN(set_privacy) SIM_SCREEN(set_ble)
    SIM_SCREEN(compass) SIM_SCREEN(trail) SIM_SCREEN(battery) SIM_SCREEN(team)
    SIM_SCREEN(waypoints) SIM_SCREEN(waypoint_detail) SIM_SCREEN(provision)
#undef SIM_SCREEN
}}

// ---- real toast (route ui::toast::show to the mono banner) ------------------
namespace ui { namespace toast {
    void show(const char* msg, uint32_t ms) { ui::kit::mono::toast(msg, ms ? ms : 2000); }
}}

// ============================================================================
//  Navigation glue mirrored from src/main_wio.cpp
// ============================================================================
namespace {

int g_sb_scale = 1;   // status-bar text scale (1 = Wio, 3 = T5 preview)

// Title shown centered in the status bar — copied from main_wio::title_for().
const char* title_for(int id) {
    switch (id) {
        case SCREEN_HOME:       return mesh::task::node_name();
        case SCREEN_CHAT:       return i18n::t(i18n::T_MESSAGES);
        case SCREEN_QUICKREPLY: return i18n::t(i18n::T_REPLY);
        case SCREEN_SETTINGS:   return i18n::t(i18n::T_SETTINGS);
        case SCREEN_GPS:        return i18n::t(i18n::T_GPS_INFO);
        case SCREEN_MESH:       return i18n::t(i18n::T_MESH_INFO);
        case SCREEN_SET_GPS:    return i18n::t(i18n::T_GPS_SETTINGS);
        case SCREEN_SET_MESH:   return i18n::t(i18n::T_MESH_SETTINGS);
        case SCREEN_SET_DISPLAY:return i18n::t(i18n::T_DISPLAY);
        case SCREEN_SET_BLE:    return i18n::t(i18n::T_BLUETOOTH);
        case SCREEN_BATTERY:    return i18n::t(i18n::T_BATTERY);
        case SCREEN_TRAIL:      return i18n::t(i18n::T_TRAIL);
        case SCREEN_TEAM:       return i18n::t(i18n::T_TEAM);
        case SCREEN_WAYPOINTS:  return i18n::t(i18n::T_WAYPOINTS);
        case SCREEN_WAYPOINT_DETAIL: return i18n::t(i18n::T_WAYPOINTS);
        case SCREEN_PROVISION:
        case SCREEN_PROVISION_RUN:
        case SCREEN_PROVISION_PICK:  return i18n::t(i18n::T_PROVISION);
        default:                return nullptr;   // e.g. compass — fills the screen itself
    }
}

// Scale-aware mirror of main_wio::draw_statusbar (so it reads on the big T5 panel).
void draw_statusbar(int w, int h) {
    int cs = g_sb_scale, cw = 6 * cs;
    int y = (h - 8 * cs) / 2; if (y < 0) y = 0;
    display.setFont(nullptr);
    display.setTextSize(cs);
    display.setTextColor(mono::fg());

    char t[8];
    snprintf(t, sizeof(t), "%02d:%02d", model::clock.hour, model::clock.minute);
    display.setCursor(2, y); display.print(t);

    char g[12];
    if (model::gps.has_fix) snprintf(g, sizeof(g), "GPS %d", (int)model::gps.satellites);
    else                    snprintf(g, sizeof(g), "GPS --");
    char r[24];
    snprintf(r, sizeof(r), "%s  %d%%", g, model::battery.percent);
    int r_x = w - (int)strlen(r) * cw - 2;
    display.setCursor(r_x, y); display.print(r);

    const char* title = title_for(ui::screen_mgr::top_id());
    if (title && title[0]) {
        int clock_end = 2 + (int)strlen(t) * cw;
        int tw = mono::text_width(title, ui::kit::Font::Small);
        int tx = (w - tw) / 2;
        if (tx < clock_end + 4) tx = clock_end + 4;
        if (tx + tw > r_x - 4) tx = r_x - 4 - tw;
        if (tx >= clock_end + 4) mono::text(tx, y, title, ui::kit::Font::Small);
    }
}

// ---- home menu (main_wio::home_*) ------------------------------------------
void home_messages(void*) { ui::screen_mgr::push(SCREEN_CHAT, true); }
void home_team(void*)     { ui::screen_mgr::push(SCREEN_TEAM, true); }
void home_trail(void*)    { ui::screen_mgr::push(SCREEN_TRAIL, true); }
void home_waypoints(void*){ ui::screen_mgr::push(SCREEN_WAYPOINTS, true); }
void home_settings(void*) { ui::screen_mgr::push(SCREEN_SETTINGS, true); }

void home_create(Handle root) {
    Handle lst = list(root);
    gap(lst, 2);
    menu_row(lst, i18n::t(i18n::T_MESSAGES), home_messages, nullptr);
    model::refresh_contacts();
    if (model::team_count() > 0)
        menu_row(lst, i18n::t(i18n::T_TEAM), home_team, nullptr);
    menu_row(lst, i18n::t(i18n::T_TRAIL),    home_trail,    nullptr);
    menu_row(lst, i18n::t(i18n::T_WAYPOINTS), home_waypoints, nullptr);
    menu_row(lst, i18n::t(i18n::T_SETTINGS), home_settings, nullptr);
}
void noop() {}
screen_lifecycle_t home_life = { home_create, noop, noop, noop };

// ---- dashboard (idle landing screen, main_wio::dash_*) ---------------------
Handle dash_gpsbatt = nullptr;
Handle dash_clock   = nullptr;
Handle dash_unread  = nullptr;
Timer  dash_timer   = nullptr;

void dash_refresh(void*) {
    if (dash_clock) set_textf(dash_clock, "%02d:%02d", model::clock.hour, model::clock.minute);
    if (dash_gpsbatt) {
        if (model::gps.has_fix)
            set_textf(dash_gpsbatt, "GPS %d  %d%%", (int)model::gps.satellites, model::battery.percent);
        else
            set_textf(dash_gpsbatt, "GPS --  %d%%", model::battery.percent);
    }
    if (dash_unread) set_textf(dash_unread, "%d %s", model::sleep_cfg.unread_messages, i18n::t(i18n::T_UNREAD));
}

void dash_create(Handle root) {
    free_layout(root);
    dash_gpsbatt = label(root, "GPS --  0%");
    font(dash_gpsbatt, Font::Body);
    align(dash_gpsbatt, Align::TopRight);
    dash_clock = label(root, "00:00");
    font(dash_clock, Font::ClockLg);
    align(dash_clock, Align::Center, 0, -10);
    dash_unread = label(root, "0 unread");
    font(dash_unread, Font::Title);
    align(dash_unread, Align::Center, 0, 26);
    dash_refresh(nullptr);
}
void dash_entry() {
    mono::set_statusbar(0, nullptr);
    dash_timer = every(1000, dash_refresh, nullptr);
}
void dash_exit() {
    if (dash_timer) { stop(dash_timer); dash_timer = nullptr; }
    mono::set_statusbar(g_sb_scale == 1 ? 14 : 46, draw_statusbar);
}
void dash_destroy() { dash_gpsbatt = dash_clock = dash_unread = nullptr; }
screen_lifecycle_t dash_life = { dash_create, dash_entry, dash_exit, dash_destroy };

// ---- key routing (main_wio::dispatch_key) ----------------------------------
void dispatch_key(char key) {
    if (ui::kit::keyboard_active())
        mono::feed_key(key);
    else if (ui::screen_mgr::top_id() == SCREEN_DASH)
        ui::screen_mgr::push(SCREEN_HOME, true);
    else if (key == 'B')
        ui::screen_mgr::pop(false);
    else
        mono::feed_key(key);
}

void register_all() {
    using namespace ui::screen;
    ui::screen_mgr::register_screen(SCREEN_DASH,     &dash_life);
    ui::screen_mgr::register_screen(SCREEN_HOME,     &home_life);
    ui::screen_mgr::register_screen(SCREEN_SETTINGS, &settings::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_GPS,      &gps::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_MESH,     &mesh_settings::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_SET_GPS,  &set_gps::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_SET_MESH, &set_mesh::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_SET_DISPLAY, &set_display::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_SET_BLE,  &set_ble::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_CHAT,     &chat::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_QUICKREPLY, &quick_reply::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_COMPASS,  &compass::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_TRAIL,    &trail::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_BATTERY,  &battery::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_TEAM,     &team::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_WAYPOINTS,       &waypoints::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_WAYPOINT_DETAIL, &waypoint_detail::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_PROVISION,       &provision::lifecycle);
    // Extra screens the Wio firmware reaches indirectly — registered so the page's
    // "jump to screen" picker can preview them directly.
    ui::screen_mgr::register_screen(SCREEN_STATUS,  &status::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_SOUND,   &set_sound::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_PRIVACY, &set_privacy::lifecycle);
}

uint32_t g_now = 1;

} // namespace

// ============================================================================
//  C ABI exported to JavaScript
// ============================================================================
extern "C" {

// target: 0 = Wio 250x122, 1 = T5 540x960 portrait (scaled UI). lang: 0=EN,1=SL.
EMSCRIPTEN_KEEPALIVE
void sim_boot(int target, int lang) {
    i18n::set_lang(lang ? i18n::SL : i18n::EN);
    model::sim_seed();
    i18n::set_lang(lang ? i18n::SL : i18n::EN);   // sim_seed() honors SIM_LANG; force the UI choice
    g_now = 1;
    sim_set_millis(g_now);

    if (target == 1) {
        display.set_size(540, 960);
        g_sb_scale = 3;
        mono::set_ui_scale(3);
        mono::set_statusbar(46, draw_statusbar);
    } else {
        display.set_size(250, 122);
        g_sb_scale = 1;
        mono::set_ui_scale(1);
        mono::set_statusbar(14, draw_statusbar);
    }
    register_all();
    ui::screen_mgr::switch_to(SCREEN_DASH, false);
}

// Feed a joystick key: 'U' 'D' 'L' 'R' 'E' (press/enter) 'B' (back).
EMSCRIPTEN_KEEPALIVE
void sim_key(int key) {
    dispatch_key((char)key);
    mono::render();
}

// Advance time by dt_ms and fire any due timers (clock tick, toast expiry).
EMSCRIPTEN_KEEPALIVE
void sim_tick(int dt_ms) {
    g_now += (uint32_t)(dt_ms > 0 ? dt_ms : 0);
    sim_set_millis(g_now);
    mono::tick(g_now);
    mono::render();
}

// Jump straight to any registered screen id (the page's screen picker).
EMSCRIPTEN_KEEPALIVE
void sim_goto(int id) {
    ui::screen_mgr::switch_to(id, false);
    mono::render();
}

// Toggle dark mode (panel paints white-on-black).
EMSCRIPTEN_KEEPALIVE
void sim_set_invert(int on) {
    mono::set_invert(on != 0);
    mono::redraw();
    mono::render();
}

EMSCRIPTEN_KEEPALIVE const uint8_t* sim_pixels() { return display.pixels(); }
EMSCRIPTEN_KEEPALIVE int sim_w() { return display.width(); }
EMSCRIPTEN_KEEPALIVE int sim_h() { return display.height(); }

} // extern "C"

int main() { return 0; }   // Emscripten entry; real init is sim_boot() from JS.
