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
#include <cstring>

// ---- model fixtures --------------------------------------------------------
namespace model {
    static StoredMessage msg_store[MAX_STORED_MESSAGES];
    StoredMessage* messages = msg_store;
    int message_count = 0;

    Clock   clock   = {};
    GPS     gps     = {};
    Battery battery = {};
    Mesh    mesh    = {};
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
        clock.hour = 10; clock.minute = 54;
        gps.has_fix = false; gps.satellites = 0;
        battery.percent = 37;

        auto add = [](const char* who, const char* text, bool self) {
            StoredMessage& m = msg_store[message_count++];
            memset(&m, 0, sizeof(m));
            strncpy(m.sender, who, sizeof(m.sender) - 1);
            strncpy(m.text, text, sizeof(m.text) - 1);
            m.is_self = self;
            m.channel_idx = 0;
        };
        add("Ana",  "Sem na poti, pridem cez 10 minut.", false);
        add("me",   "OK", true);
        add("Bojan","Najdemo se pri koci na vrhu?", false);
        add("me",   "Ja, super. Vidimo se tam.", true);

        waypoints.add(46123456, 14987654, 0, "Koca");
        waypoints.add(46200000, 15000000, 0, "Izvir");
    }
}

// ---- mesh::task ------------------------------------------------------------
namespace mesh { namespace task {
    uint8_t get_msg_channel() { return 0; }
    const char* node_name() { return "me"; }
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
