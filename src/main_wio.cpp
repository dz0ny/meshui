#ifdef BOARD_WIO_L1
#include <Arduino.h>
#include <time.h>
#include <InternalFileSystem.h>
#include "board_wio.h"
#include "ui/screen_ids.h"
#include "ui/ui_screen_mgr.h"
#include "ui/i18n.h"
#include "ui/kit/ui_kit.h"
#include "ui/kit/ui_kit_mono.h"
#include "ui/screens/status.h"
#include "ui/screens/settings.h"
#include "ui/screens/gps.h"
#include "ui/screens/mesh_settings.h"
#include "ui/screens/set_gps.h"
#include "ui/screens/set_mesh.h"
#include "ui/screens/set_display.h"
#include "ui/screens/set_sound.h"
#include "ui/screens/set_privacy.h"
#include "ui/screens/set_ble.h"
#include "ui/screens/chat.h"
#include "ui/screens/quick_reply.h"
#include "ui/screens/compass.h"
#include "ui/screens/trail.h"
#include "ui/screens/battery.h"
#include "ui/screens/team.h"
#include "ui/screens/waypoints.h"
#include "ui/screens/waypoint_detail.h"
#include "ui/screens/provision.h"
#include "model.h"
#include "trail_store.h"
#include "mesh/mesh_task.h"
#include "mesh/provision.h"
#include "mesh/companion/target.h"           // sensors, rtc_clock
#include <helpers/sensors/LocationProvider.h>

// Phase 5 — real MeshCore stack on the tracker: radio + mesh.loop() driven from
// the main loop, feeding the model (GPS / clock / mesh stats). Messages flow via
// the bridge into model::messages for the chat screen.

using namespace ui::kit;

// Joystick / button pins. Panel rotated 90°: left = up, right = down.
static const uint8_t BTN_UP    = 27;
static const uint8_t BTN_DOWN  = 28;
static const uint8_t BTN_PRESS = 29;
static const uint8_t BTN_BACK  = 13;

// ---- feed the model from the mesh sensors / radio ---------------------------
static void feed_model() {
    LocationProvider* loc = sensors.getLocationProvider();
    if (loc) {
        model::gps.module_ok = true;
        model::gps.has_fix   = loc->isValid();
        if (model::gps.has_fix) {
            model::gps.lat        = loc->getLatitude()  / 1e6;
            model::gps.lng        = loc->getLongitude() / 1e6;
            model::gps.altitude_m = loc->getAltitude()  / 1000.0;
        }
        model::gps.satellites  = loc->satellitesCount();
        model::gps.status_text = model::gps.has_fix ? "Fix OK" : "Searching";

        // Sync the clock from GPS time the moment the receiver decodes it — even
        // with a single satellite and before a position fix. The vendored provider
        // only syncs the RTC once isValid() (a full fix), which can take minutes;
        // message timestamps and beacon aging shouldn't wait that long for time
        // that's available almost immediately.
        {
            static uint32_t next_time_sync_ms = 0;
            if ((int32_t)(millis() - next_time_sync_ms) >= 0) {
                next_time_sync_ms = millis() + 2000;
                uint32_t gps_secs = (uint32_t)loc->getTimestamp();
                if (gps_secs > 1700000000UL) {              // plausible epoch (>= 2023-11)
                    uint32_t cur  = (uint32_t)rtc_clock.getCurrentTime();
                    uint32_t diff = gps_secs > cur ? gps_secs - cur : cur - gps_secs;
                    if (cur < 1700000000UL || diff > 2)     // clock unset or drifted >2 s
                        rtc_clock.setCurrentTime(gps_secs);
                }
            }
        }

        // Dead-reckoned heading (course over ground): the tracker has no
        // magnetometer, so derive our facing direction from how the fix moves.
        // Only update once we've travelled far enough for the bearing to be
        // meaningful; otherwise GPS jitter would spin the compass.
        static bool     have_prev = false;
        static double   prev_lat = 0, prev_lng = 0;
        static uint32_t prev_ms = 0;
        if (model::gps.has_fix) {
            if (have_prev) {
                double rad = M_PI / 180.0;
                double mlat = (model::gps.lat - prev_lat) * 111320.0;
                double mlng = (model::gps.lng - prev_lng) * 111320.0 * cos(prev_lat * rad);
                double moved = sqrt(mlat * mlat + mlng * mlng);
                uint32_t dt_ms = millis() - prev_ms;
                if (moved >= 5.0) {   // ~5 m gate
                    double dLon = (model::gps.lng - prev_lng) * rad;
                    double y = sin(dLon) * cos(model::gps.lat * rad);
                    double x = cos(prev_lat * rad) * sin(model::gps.lat * rad) -
                               sin(prev_lat * rad) * cos(model::gps.lat * rad) * cos(dLon);
                    double b = atan2(y, x) * 180.0 / M_PI;
                    if (b < 0) b += 360.0;
                    model::gps.heading_deg = b;
                    model::gps.heading_valid = true;
                    // Ground speed from the same displacement/time baseline.
                    if (dt_ms >= 1000) {
                        double kmh = (moved / 1000.0) / ((double)dt_ms / 3600000.0);
                        if (kmh <= 300.0)
                            model::gps.speed_kmh = model::gps.speed_kmh * 0.4 + kmh * 0.6;
                    }
                    prev_lat = model::gps.lat; prev_lng = model::gps.lng; prev_ms = millis();
                } else if (dt_ms >= 8000) {
                    // Parked inside the gate for a while → decay to zero.
                    model::gps.speed_kmh = 0.0;
                    prev_lat = model::gps.lat; prev_lng = model::gps.lng; prev_ms = millis();
                }
            } else {
                prev_lat = model::gps.lat; prev_lng = model::gps.lng;
                prev_ms = millis(); have_prev = true;
            }
        } else {
            model::gps.speed_kmh = 0.0;
        }

        // GPS breadcrumb trail sampling — runs in the background whenever
        // tracking is active, independent of the current screen. The min-delta
        // gate inside addPoint() drops near-stationary fixes; the 1 s cadence
        // gate avoids hammering the ring if feed_model() runs faster.
        if (model::trail.isActive() && model::gps.has_fix) {
            static uint32_t next_trail_sample_ms = 0;
            if ((int32_t)(millis() - next_trail_sample_ms) >= 0) {
                next_trail_sample_ms = millis() + (uint32_t)TrailStore::SAMPLING_SECS * 1000UL;
                model::trail.addPoint((int32_t)loc->getLatitude(),
                                      (int32_t)loc->getLongitude(),
                                      (uint32_t)rtc_clock.getCurrentTime(),
                                      TrailStore::minDeltaMeters(0, false));  // ~1 m gate
            }
        }
    }

    time_t now = (time_t)rtc_clock.getCurrentTime();
    if (now > 1700000000) {                 // valid epoch (clock set)
        model::epoch_now = (uint32_t)now;   // age live beacons on the Team screen
        // RTC keeps UTC; shift by the user's timezone offset before breaking it
        // down so the date rolls over correctly across midnight.
        time_t local = now + (time_t)model::clock.tz_offset_hours * 3600;
        struct tm* t = gmtime(&local);
        model::clock.hour = t->tm_hour; model::clock.minute = t->tm_min; model::clock.second = t->tm_sec;
        model::clock.year = (uint8_t)((t->tm_year + 1900) - 2000);
        model::clock.month = t->tm_mon + 1; model::clock.day = t->tm_mday;
    }

    model::mesh.node_name    = mesh::task::node_name();
    model::mesh.freq_mhz     = mesh::task::get_freq();
    model::mesh.bw_khz       = mesh::task::get_bw();
    model::mesh.sf           = mesh::task::get_sf();
    model::mesh.cr           = mesh::task::get_cr();
    model::mesh.tx_power_dbm = mesh::task::get_tx_power();
    model::mesh.rx_packets   = mesh::task::get_packets_recv();
    model::mesh.tx_packets   = mesh::task::get_packets_sent();
    model::mesh.radio_ok     = true;
}

// ---- mesh boot-step banner (capture-free freeze diagnostic) -----------------
// Paint the current mesh-init step onto the e-ink before it runs. If a step
// hangs, the frozen panel keeps showing the last step reached.
static void draw_mesh_step(const char* s) {
    int w = display.width();
    int h = display.height();
    int band_y = h / 2 - 8;
    display.setPartialWindow(0, band_y, w, 16);
    display.setFont(nullptr);
    display.setTextSize(1);
    display.setTextColor(GxEPD_BLACK);
    display.firstPage();
    do {
        display.fillRect(0, band_y, w, 16, GxEPD_WHITE);
        display.setCursor(2, band_y + 4);
        display.print("MESH: ");
        display.print(s);
    } while (display.nextPage());
}

// ---- status bar (clock / GPS) ----------------------------------------------
static void draw_statusbar(int w, int h) {
    (void)h;
    display.setFont(nullptr);
    display.setTextSize(1);
    display.setTextColor(mono::fg());   // tracks dark-mode invert

    char t[8];
    snprintf(t, sizeof(t), "%02d:%02d", model::clock.hour, model::clock.minute);
    display.setCursor(2, 3); display.print(t);

    char g[12];
    if (model::gps.has_fix) snprintf(g, sizeof(g), "GPS %d", (int)model::gps.satellites);
    else                    snprintf(g, sizeof(g), "GPS --");
    char r[24];
    snprintf(r, sizeof(r), "%s  %d%%", g, model::battery.percent);
    display.setCursor(w - (int)strlen(r) * 6 - 2, 3); display.print(r);
}

// ---- home screen ------------------------------------------------------------
static void home_messages(void*) { ui::screen_mgr::push(SCREEN_CHAT, true); }
static void home_team(void*)     { ui::screen_mgr::push(SCREEN_TEAM, true); }
static void home_trail(void*)    { ui::screen_mgr::push(SCREEN_TRAIL, true); }
static void home_waypoints(void*){ ui::screen_mgr::push(SCREEN_WAYPOINTS, true); }
static void home_status(void*)   { ui::screen_mgr::push(SCREEN_STATUS, true); }
static void home_settings(void*) { ui::screen_mgr::push(SCREEN_SETTINGS, true); }

static void home_create(Handle root) {
    Handle lst = list(root);
    gap(lst, 2);
    menu_row(lst, i18n::t(i18n::T_MESSAGES), home_messages, nullptr);
    // "Team" only when at least one favorited chat contact exists.
    model::refresh_contacts();
    if (model::team_count() > 0)
        menu_row(lst, i18n::t(i18n::T_TEAM), home_team, nullptr);
    menu_row(lst, i18n::t(i18n::T_TRAIL),    home_trail,    nullptr);
    menu_row(lst, "Waypoints",               home_waypoints, nullptr);
    // GPS / Mesh / Battery live under Status (it already lists their info pages).
    menu_row(lst, i18n::t(i18n::T_STATUS),   home_status,   nullptr);
    menu_row(lst, i18n::t(i18n::T_SETTINGS), home_settings, nullptr);
}
static void noop() {}
static screen_lifecycle_t home_life = { home_create, noop, noop, noop };

// ---- default dashboard (idle landing screen) --------------------------------
// The screen the tracker boots into and returns to. No status bar — instead it
// shows GPS + battery top-right, a big clock, and the unread message count.
// Pressing the joystick opens the menu (the old home screen).
static Handle dash_gpsbatt = nullptr;
static Handle dash_clock   = nullptr;
static Handle dash_unread  = nullptr;
static ui::kit::Timer dash_timer = nullptr;

static void dash_refresh(void*) {
    if (dash_clock) set_textf(dash_clock, "%02d:%02d", model::clock.hour, model::clock.minute);
    if (dash_gpsbatt) {
        if (model::gps.has_fix)
            set_textf(dash_gpsbatt, "GPS %d  %d%%", (int)model::gps.satellites, model::battery.percent);
        else
            set_textf(dash_gpsbatt, "GPS --  %d%%", model::battery.percent);
    }
    if (dash_unread) set_textf(dash_unread, "%d %s", model::sleep_cfg.unread_messages, i18n::t(i18n::T_UNREAD));
}

static void dash_create(Handle root) {
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

    // No on-screen controls: poll_buttons() routes ANY button press here straight
    // to the menu (the old home screen).
    dash_refresh(nullptr);
}

static void dash_entry() {
    mono::set_statusbar(0, nullptr);          // hide the status bar on the dashboard
    dash_timer = every(1000, dash_refresh, nullptr);
}
static void dash_exit() {
    if (dash_timer) { stop(dash_timer); dash_timer = nullptr; }
    mono::set_statusbar(14, draw_statusbar);  // restore it for every other screen
}
static void dash_destroy() { dash_gpsbatt = dash_clock = dash_unread = nullptr; }
static screen_lifecycle_t dash_life = { dash_create, dash_entry, dash_exit, dash_destroy };

// ---- joystick ---------------------------------------------------------------
struct Btn { uint8_t pin; char key; bool prev; uint32_t t; };
static Btn g_btns[] = {
    { BTN_UP,    'U', false, 0 },
    { BTN_DOWN,  'D', false, 0 },
    { BTN_PRESS, 'E', false, 0 },
    { BTN_BACK,  'B', false, 0 },
};
static void poll_buttons() {
    uint32_t now = millis();
    for (auto& b : g_btns) {
        bool down = (digitalRead(b.pin) == LOW);
        if (down && !b.prev && (now - b.t) > 150) {
            b.t = now;
            if (ui::screen_mgr::top_id() == SCREEN_DASH)
                ui::screen_mgr::push(SCREEN_HOME, true);   // any button leaves the dashboard for the menu
            else if (ui::screen_mgr::top_id() == SCREEN_PROVISION_RUN ||
                     ui::screen_mgr::top_id() == SCREEN_PROVISION_PICK) {
                if (b.key == 'B') provision::reboot();     // abort provisioning → normal boot
                else              mono::feed_key(b.key);    // navigate the device picker (no-op on run)
            }
            else if (b.key == 'B') ui::screen_mgr::pop(false);
            else                   mono::feed_key(b.key);
        }
        b.prev = down;
    }
}

// ---- language persistence (single byte in LittleFS) -------------------------
// Loaded before the first screen is built so the UI comes up in the saved
// language. set_display.cpp writes "/lang" when the user changes it.
static void load_language() {
    InternalFS.begin();   // idempotent; mesh::task::start() mounts it again later
    using namespace Adafruit_LittleFS_Namespace;
    File f = InternalFS.open("/lang", FILE_O_READ);
    if (f) {
        int b = f.read();
        f.close();
        if (b >= 0) i18n::set_lang((uint8_t)b);
    }
}

// ---- timezone persistence (single signed byte, hours from UTC) --------------
// set_display.cpp writes "/tz"; the RTC keeps UTC and feed_model() applies this
// offset when breaking the epoch down for the clock / status bar.
static void load_timezone() {
    InternalFS.begin();   // idempotent
    using namespace Adafruit_LittleFS_Namespace;
    File f = InternalFS.open("/tz", FILE_O_READ);
    if (f) {
        int b = f.read();
        f.close();
        if (b >= 0) model::clock.tz_offset_hours = (int8_t)(uint8_t)b;
    }
}

// ---- display invert persistence (single byte, 0/1) --------------------------
// set_display.cpp writes "/invert" when the user toggles dark mode; loaded here
// before the first screen is built so the UI comes up in the saved scheme.
static void load_invert() {
    InternalFS.begin();   // idempotent
    using namespace Adafruit_LittleFS_Namespace;
    File f = InternalFS.open("/invert", FILE_O_READ);
    if (f) {
        int b = f.read();
        f.close();
        if (b > 0) mono::set_invert(true);
    }
}

void setup() {
    Serial.begin(115200);
    delay(200);
    board_wio::init();
    for (auto& b : g_btns) pinMode(b.pin, INPUT_PULLUP);

    load_language();         // pick up the saved UI language before building screens
    load_timezone();         // and the saved timezone offset for the clock
    load_invert();           // and the saved dark-mode setting
    model::init_messages();  // allocate the message store

    // Bring the UI up first so the screen is responsive even if mesh init is slow.
    ui::screen_mgr::register_screen(SCREEN_DASH,     &dash_life);
    ui::screen_mgr::register_screen(SCREEN_HOME,     &home_life);
    ui::screen_mgr::register_screen(SCREEN_STATUS,   &ui::screen::status::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_SETTINGS, &ui::screen::settings::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_GPS,      &ui::screen::gps::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_MESH,     &ui::screen::mesh_settings::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_SET_GPS,  &ui::screen::set_gps::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_SET_MESH, &ui::screen::set_mesh::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_SET_DISPLAY, &ui::screen::set_display::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_SOUND, &ui::screen::set_sound::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_PRIVACY, &ui::screen::set_privacy::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_SET_BLE,  &ui::screen::set_ble::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_CHAT,     &ui::screen::chat::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_QUICKREPLY, &ui::screen::quick_reply::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_COMPASS,  &ui::screen::compass::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_TRAIL,    &ui::screen::trail::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_BATTERY,  &ui::screen::battery::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_TEAM,     &ui::screen::team::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_WAYPOINTS,       &ui::screen::waypoints::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_WAYPOINT_DETAIL, &ui::screen::waypoint_detail::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_PROVISION,      &ui::screen::provision::lifecycle);
    ui::screen_mgr::register_screen(SCREEN_PROVISION_RUN,  &ui::screen::provision::run_lifecycle);
    ui::screen_mgr::register_screen(SCREEN_PROVISION_PICK, &ui::screen::provision::pick_lifecycle);
    mono::set_statusbar(14, draw_statusbar);
    // A pending provision request reboots into an exclusive BLE mode (the companion
    // radio is skipped in mesh::task::start). Receive lands on the device picker
    // (choose a sharer), Share on the transfer-status screen; everything else boots
    // to the dashboard.
    provision::Mode pm = provision::pending();
    int boot_screen = (pm == provision::Mode::Receive) ? SCREEN_PROVISION_PICK
                    : (pm == provision::Mode::Share)   ? SCREEN_PROVISION_RUN
                                                       : SCREEN_DASH;
    ui::screen_mgr::switch_to(boot_screen, false);

    // To debug a mesh-init hang, install the e-ink step drawer so the frozen
    // panel shows the last step reached:  mesh::task::diag_step = draw_mesh_step;
    // (left off in normal operation — each step is a slow partial refresh.)

    Serial.println("meshui-mini: UI up; mesh starting");
}

void loop() {
    // Bring the mesh up shortly after boot so the UI is responsive first, then
    // drive mesh.loop() + model feed from here (single-threaded, no RTOS task).
    static bool mesh_started = false;
    static uint32_t boot_ms = millis();
    if (!mesh_started && (millis() - boot_ms) > 1500) {
        mesh_started = true;
        mesh::task::start(0);
    }

    if (mesh_started) {
        mesh::task::loop();
        feed_model();
        model::ingest_bridge_events();
    }

    // Home is built at boot, *before* the mesh starts, so its "Team" row — which
    // only appears when a favorited chat contact exists — is missing on the first
    // paint (contacts aren't loaded yet). mesh::task::start() loads contacts
    // synchronously, so on the first loop after it ran we refresh and, if we're
    // still on home and a team now exists, rebuild it in place. Without this the
    // Team row only showed up after leaving and re-entering home.
    static bool home_team_synced = false;
    if (mesh_started && !home_team_synced) {
        home_team_synced = true;
        model::refresh_contacts();
        if (model::team_count() > 0 && ui::screen_mgr::top_id() == SCREEN_HOME)
            ui::screen_mgr::switch_to(SCREEN_HOME, false);   // rebuild with the Team row
    }

    poll_buttons();

    // Battery: the ADC read pulses VBAT_ENABLE + a settle delay, so throttle it
    // hard (~15 s). Redraw the header only when the displayed percent changes.
    static uint32_t last_batt_ms = 0;
    static int      last_batt_pct = -1;
    if (last_batt_ms == 0 || (millis() - last_batt_ms) > 15000) {
        last_batt_ms = millis();
        model::update_battery();
        if ((int)model::battery.percent != last_batt_pct) {
            last_batt_pct = model::battery.percent;
            mono::redraw();
        }
    }

    static uint8_t last_min = 0xFF;
    if (model::clock.minute != last_min) { last_min = model::clock.minute; mono::redraw(); }

    mono::tick(millis());
    mono::render();
    delay(5);
}
#endif // BOARD_WIO_L1
