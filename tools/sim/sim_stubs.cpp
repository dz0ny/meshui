// Host stand-ins for the hardware/data layers the screens read. Provides just
// enough of model:: / mesh::task:: / ui::screen_mgr:: / ui::toast:: to render a
// screen off-device, plus sample fixtures so screens have something to draw.
#include "../../src/model.h"
#include "../../src/mesh/mesh_task.h"
#include "../../src/ui/ui_screen_mgr.h"
#include "../../src/ui/components/toast.h"
#include "../../src/waypoint_store.h"
#include "../../src/trail_store.h"
#include "../../src/mesh/provision.h"
#include "../../src/ui/i18n.h"
#include <cstring>
#include <cstdlib>

// ---- model fixtures --------------------------------------------------------
namespace model {
    static StoredMessage msg_store[MAX_STORED_MESSAGES];
    StoredMessage* messages = msg_store;
    int message_count = 0;

    Clock   clock   = {};
    GPS     gps     = {};
    Battery battery = {};
    Mesh    mesh    = {};
    Sleep   sleep_cfg = {};   // dashboard reads unread_messages
    uint32_t epoch_now = 0;

    ContactEntry contacts[MAX_CONTACT_ENTRIES] = {};
    int contact_count = 0;
    uint32_t contacts_revision = 0;

    LivePosition live_positions[MAX_LIVE_POSITIONS] = {};
    int live_position_count = 0;
    uint32_t live_position_revision = 0;

    TrailStore    trail;
    WaypointStore waypoints;

    void clear_unread_messages() {}
    void delete_message(int) {}
    void mark_dirty(uint32_t) {}
    void refresh_contacts() {}
    void update_battery() {}

    void sim_seed() {
        // Default to English; set SIM_LANG=sl to exercise the Lemon font's
        // Slovenian diacritics.
        const char* lang = getenv("SIM_LANG");
        i18n::set_lang(lang && lang[0] == 's' ? i18n::SL : i18n::EN);

        clock.hour = 10; clock.minute = 54;
        gps.has_fix = true; gps.satellites = 9;
        gps.lat = 46.05; gps.lng = 14.50;     // Ljubljana-ish, for team distances
        battery.percent = 37;
        sleep_cfg.unread_messages = 2;   // so the dashboard shows a count
        epoch_now = 100000;

        // Team = favorited chat contacts. Seed two, with live positions so the
        // Team screen shows distance/bearing instead of an empty list.
        auto add_team = [](const char* name, uint8_t tag, int32_t lat_e6, int32_t lon_e6) {
            ContactEntry& c = contacts[contact_count];
            memset(&c, 0, sizeof(c));
            strncpy(c.name, name, sizeof(c.name) - 1);
            c.type = CONTACT_TYPE_CHAT;
            c.flags = CONTACT_FLAG_FAVORITE;
            for (int i = 0; i < 6; i++) c.pub_key[i] = tag + i;
            LivePosition& p = live_positions[live_position_count++];
            memset(&p, 0, sizeof(p));
            for (int i = 0; i < 6; i++) p.pub_key_prefix[i] = tag + i;
            strncpy(p.name, name, sizeof(p.name) - 1);
            p.lat_e6 = lat_e6; p.lon_e6 = lon_e6;
            p.timestamp = epoch_now - 120; p.valid = true;
            contact_count++;
        };
        add_team("Ana",  0x10, 46070000, 14520000);
        add_team("Bojan",0x20, 46030000, 14480000);

        auto add = [](const char* who, const char* text, bool self) {
            StoredMessage& m = msg_store[message_count++];
            memset(&m, 0, sizeof(m));
            strncpy(m.sender, who, sizeof(m.sender) - 1);
            strncpy(m.text, text, sizeof(m.text) - 1);
            m.is_self = self;
            m.channel_idx = 0;
        };
        add("Ana \xF0\x9F\x9A\x80", "On my way, there in 10 minutes.", false);   // emoji in sender name
        add("me",   "OK", true);
        add("Bojan","Meet at the hut on top?", false);
        add("me",   "Yes, great. See you there.", true);

        waypoints.add(46123456, 14987654, 0, "Hut");
        waypoints.add(46200000, 15000000, 0, "Spring");

        // A recorded trail (~1.2 km) so the Trail screen shows real numbers.
        trail.setActive(true);
        int32_t la = 46050000, lo = 14500000;
        for (int i = 0; i < 12; i++) { trail.addPoint(la, lo, epoch_now + i * 60, 5); la += 1000; lo += 500; }
    }
}

// ---- mesh::task ------------------------------------------------------------
namespace mesh { namespace task {
    uint8_t get_msg_channel() { return 0; }
    const char* node_name() { return "Bob \xF0\x9F\x9A\x80"; }   // "Bob 🚀" — emoji header test
    int get_channels(ChannelEntry* dest, int max_num) {
        if (max_num < 1) return 0;
        dest[0].idx = 0;
        strncpy(dest[0].name, "Public", sizeof(dest[0].name) - 1);
        dest[0].name[sizeof(dest[0].name) - 1] = 0;
        return 1;
    }
    bool send_public(const char*) { return true; }
    bool send_channel(uint8_t, const char*) { return true; }
    void set_node_name(const char*) {}
    void set_msg_channel(uint8_t) {}
    bool get_channel_alerts() { return false; }
    void set_channel_alerts(bool) {}

    // Radio params
    float   get_freq() { return 869.525f; }
    float   get_bw()   { return 250.0f; }
    uint8_t get_sf()   { return 11; }
    uint8_t get_cr()   { return 5; }
    int8_t  get_tx_power() { return 22; }
    void    set_freq(float) {}
    void    set_bw(float) {}
    void    set_sf(uint8_t) {}
    void    set_cr(uint8_t) {}
    void    set_tx_power(int8_t) {}

    // Fast-GPS region/channel
    uint8_t     get_fast_gps_channel() { return 0; }
    void        set_fast_gps_channel(uint8_t) {}
    uint8_t     get_fast_gps_region() { return 0; }
    void        set_fast_gps_region(uint8_t) {}
    uint8_t     fast_gps_region_count() { return 2; }
    const char* fast_gps_region_label(uint8_t idx) { return idx == 0 ? "Unscoped" : "si"; }

    // Toggles
    bool get_buzzer_enabled() { return true; }
    void set_buzzer_enabled(bool) {}
    bool get_gps_enabled() { return true; }
    void set_gps_enabled(bool) {}
    bool get_advert_location() { return false; }
    void set_advert_location(bool) {}
    bool get_client_repeat() { return false; }
    void set_client_repeat(bool) {}

    // BLE
    void     ble_enable() {}
    void     ble_disable() {}
    bool     ble_is_enabled() { return false; }
    void     set_ble_pin(uint32_t) {}
    uint32_t get_ble_pin() { return 123456; }
    uint32_t regen_ble_pin() { return 123456; }

    void send_advert(bool) {}
    int  last_import_channels() { return 0; }
    int  last_import_contacts() { return 0; }
}}

// ---- ui::screen_mgr (navigation no-ops) ------------------------------------
// The static PNG sim renders one screen at a time, so navigation is stubbed out.
// The interactive web build (SIM_WEB) instead links the real mono screen manager
// (ui_screen_mgr_mono.cpp) plus a real toast, so screens actually navigate.
#ifndef SIM_WEB
namespace ui { namespace screen_mgr {
    void init() {}
    bool register_screen(int, screen_lifecycle_t*) { return true; }
    bool switch_to(int, bool) { return true; }
    bool push(int, bool) { return true; }
    bool pop(bool) { return true; }
    void reload_stack() {}
    void set_nav_title(const char*) {}
    const char* previous_nav_title(const char* fallback) { return fallback; }
    int top_id() { return -1; }
}}

// ---- ui::toast -------------------------------------------------------------
namespace ui { namespace toast {
    void show(const char*, uint32_t) {}
}}
#endif // !SIM_WEB

// ---- provision backend (idle/no-op) ----------------------------------------
namespace provision {
    Mode  pending() { return Mode::None; }
    void  request(Mode) {}
    void  begin(Mode) {}
    void  loop() {}
    void  reboot() {}
    State state() { return State::Idle; }
    int   progress() { return 0; }
    int   bytes_done() { return 0; }
    int   bytes_total() { return 0; }
    int   device_count() { return 0; }
    const char* device_name(int) { return ""; }
    int   device_rssi(int) { return 0; }
    void  connect_to(int) {}
}
