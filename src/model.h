#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Forward-declared so model.h doesn't pull in trail_store.h (and transitively
// <Arduino.h>, whose DEG_TO_RAD macro would clobber ui::geo::DEG_TO_RAD in the
// many screens that include model.h). Files that touch model::trail include
// trail_store.h themselves.
class TrailStore;
class WaypointStore;   // see waypoint_store.h (same rationale as TrailStore)

// Reactive data model — single source of truth for all UI screens.
// Updated by background tasks (mesh, gps, board), read by UI.
// No mutexes needed: writers are atomic-friendly POD types,
// and UI only reads (worst case: one stale frame).

namespace model {

enum DirtyFlags : uint32_t {
    DIRTY_NONE = 0,
    DIRTY_CLOCK = 1 << 0,
    DIRTY_BATTERY = 1 << 1,
    DIRTY_GPS = 1 << 2,
    DIRTY_MESH = 1 << 3,
    DIRTY_MESSAGES = 1 << 4,
    DIRTY_SLEEP = 1 << 5,
};

struct GPS {
    double lat;
    double lng;
    uint32_t satellites;
    double altitude_m;
    bool has_fix;
    bool module_ok;
    // "No Module", "Searching...", "Fix OK"
    const char* status_text;
    // Dead-reckoned heading (course over ground) derived from successive fixes.
    // No magnetometer on the tracker, so this is only valid once we've moved.
    double heading_deg;     // 0 = North, 90 = East
    bool   heading_valid;
    // Ground speed (km/h), derived from displacement between successive fixes.
    // Drives the adaptive GPS poll cadence — see gps_update_interval_ms().
    double speed_kmh;
};

struct Battery {
    uint16_t percent;       // 0-100
    uint16_t voltage_mv;
    int16_t  current_ma;
    float    temperature_c;
    uint16_t remain_mah;
    uint16_t full_mah;
    uint16_t design_mah;
    uint16_t health_pct;
    bool     charging;
    // Charger (BQ25896)
    bool     charger_ok;
    const char* charge_status;
    const char* bus_status;
    const char* ntc_status;
    float    vbus_v;
    float    vsys_v;
    float    vbat_v;
    float    charge_current_ma;
};

struct Mesh {
    int      peer_count;
    uint32_t rx_packets;
    uint32_t tx_packets;
    float    last_rssi;
    float    last_snr;
    bool     radio_ok;
    bool     ble_enabled;
    // Radio config (read-only from prefs)
    const char* node_name;
    float    freq_mhz;
    float    bw_khz;
    uint8_t  sf;
    uint8_t  cr;
    int8_t   tx_power_dbm;
};

struct Clock {
    uint8_t hour, minute, second;   // local time (UTC + tz_offset)
    uint8_t year, month, day;
    int8_t  tz_offset_hours;        // auto-calculated from GPS longitude
};

static constexpr int MAX_CONTACT_ENTRIES = 100;
static constexpr int MAX_DISCOVERY_ENTRIES = 16;
static constexpr int MAX_TELEMETRY_ENTRIES = 32;
static constexpr int MAX_TRACE_ENTRIES = 16;

// Contact advert type (matches MeshCore ADV_TYPE_*). Kept here so screens that
// only include model.h (e.g. the portable Team screen) don't have to pull in
// MeshCore's AdvertDataHelpers.h.
static constexpr uint8_t CONTACT_TYPE_CHAT = 1;       // ADV_TYPE_CHAT
// Favorite bit in ContactEntry::flags (same flag the Contacts screen filters on).
static constexpr uint8_t CONTACT_FLAG_FAVORITE = 0x01;

struct ContactEntry {
    char name[32];
    uint8_t pub_key[32];
    uint8_t type;
    uint8_t flags;
    bool has_path;
    int32_t gps_lat;
    int32_t gps_lon;
};

struct DiscoveryEntry {
    char name[32];
    uint8_t pubkey_prefix[7];
    uint8_t path_len;
    uint32_t recv_timestamp;
};

struct TelemetryEntry {
    bool valid;
    uint8_t pub_key_prefix[7];
    uint8_t data[96];
    uint8_t len;
    uint32_t timestamp;
    uint32_t seq;
};

struct TraceEntry {
    bool valid;
    uint32_t tag;
    uint8_t hop_count;
    int8_t snr_there_q4;
    int8_t snr_back_q4;
    uint32_t timestamp;
    uint32_t seq;
};

// Live peer positions learned from fast-GPS group beacons (see MyMesh
// onChannelDataRecv). Keyed by 6-byte pub-key prefix; the Map/compass screens
// read these to show distance and bearing to each node.
static constexpr int MAX_LIVE_POSITIONS = 32;
struct LivePosition {
    uint8_t pub_key_prefix[6];
    char    name[32];       // resolved from contacts if known, else hex
    int32_t lat_e6;
    int32_t lon_e6;
    uint32_t timestamp;     // local RX time (epoch seconds) — when last heard
    uint8_t speed_kmh;      // sender's ground speed, km/h (0 if stationary/unknown)
    bool    valid;
};
extern LivePosition live_positions[MAX_LIVE_POSITIONS];
extern int live_position_count;
extern uint32_t live_position_revision;

// Insert or update a peer's live position (matched by prefix). Empty `name`
// leaves any previously-resolved name intact.
void upsert_live_position(const uint8_t* prefix6, const char* name,
                          int32_t lat_e6, int32_t lon_e6, uint32_t timestamp,
                          uint8_t speed_kmh);

// GPS breadcrumb trail (RAM ring buffer). Recording is toggled from the Trail
// screen; points are sampled in the background by update_gps() while active.
extern TrailStore trail;

// User-marked GPS waypoints (RAM store). Added from the Waypoints screen or by
// saving a location shared in a message. See waypoint_store.h.
extern WaypointStore waypoints;

// Global state — written by updaters, read by UI
extern GPS     gps;
extern Battery battery;
extern Mesh    mesh;
extern Clock   clock;
extern ContactEntry contacts[MAX_CONTACT_ENTRIES];
extern int contact_count;
extern uint32_t contacts_revision;

// Current wall-clock time (epoch seconds), 0 until the RTC/GPS sets it. Updated
// by update_clock() (ESP) / feed_model() (mono); the Team screen uses it to age
// each member's last-heard beacon timestamp.
extern uint32_t epoch_now;

// "Team" = favorited chat-type contacts. The Team screen lists them and the home
// menu shows its row only when at least one exists.
inline bool is_team_member(const ContactEntry& c) {
    return c.type == CONTACT_TYPE_CHAT && (c.flags & CONTACT_FLAG_FAVORITE);
}
inline int team_count() {
    int n = 0;
    for (int i = 0; i < contact_count; i++)
        if (is_team_member(contacts[i])) n++;
    return n;
}

// Per-contact unread tally, keyed by sender name so it survives the periodic
// refresh_contacts() rebuild of contacts[]. Header-only (function-local statics)
// so the LVGL and mono backends share one table without a per-env .cpp.
inline uint16_t& _unread_slot(const char* name) {
    static char     names[MAX_CONTACT_ENTRIES][32];
    static uint16_t counts[MAX_CONTACT_ENTRIES] = {};
    static int      n = 0;
    static uint16_t scratch = 0;
    if (!name || !name[0]) { scratch = 0; return scratch; }
    for (int i = 0; i < n; i++)
        if (strncmp(names[i], name, 32) == 0) return counts[i];
    if (n < MAX_CONTACT_ENTRIES) {
        strncpy(names[n], name, 31); names[n][31] = 0; counts[n] = 0;
        return counts[n++];
    }
    scratch = 0; return scratch;   // table full — swallow
}
inline void     note_contact_unread(const char* name)  { if (name && name[0]) _unread_slot(name)++; }
inline uint16_t contact_unread(const char* name)        { return (name && name[0]) ? _unread_slot(name) : 0; }
inline void     clear_contact_unread(const char* name)  { if (name && name[0]) _unread_slot(name) = 0; }
extern DiscoveryEntry discovery[MAX_DISCOVERY_ENTRIES];
extern int discovery_count;
extern uint32_t discovery_revision;
extern TelemetryEntry telemetry[MAX_TELEMETRY_ENTRIES];
extern uint32_t telemetry_revision;
extern TraceEntry traces[MAX_TRACE_ENTRIES];
extern uint32_t trace_revision;

// Sleep config
struct Sleep {
    uint8_t timeout_idx;       // index into timeout presets
    uint32_t timeout_ms;       // 0 = disabled
    uint32_t last_activity_ms; // last touch/interaction timestamp
    int unread_messages;
    char last_message[80];
    char last_sender[32];
};

extern Sleep sleep_cfg;

// Message history (persists across screen switches)
struct StoredMessage {
    char sender[32];
    char text[160];
    uint8_t hour, minute;
    bool is_self;
    uint8_t channel_idx;  // group channel index, or 0xFF for a direct message
};

#define MAX_STORED_MESSAGES 50
extern StoredMessage* messages;
extern int message_count;

void init_messages();  // call once at startup to allocate PSRAM
void touch_activity();  // call on any user interaction
bool should_sleep();    // check if timeout expired
void delete_message(int idx);  // remove message at index, shift remaining
// channel_idx is the group channel the message arrived on (0xFF for a direct
// message). The dashboard unread count only tracks the channel selected under
// Display settings; other channels still store/badge but don't bump the count.
void note_incoming_message(const char* from_name, const char* text, uint8_t channel_idx);
void clear_unread_messages();
void mark_dirty(uint32_t flags);
uint32_t take_dirty();
void ingest_bridge_events();
void refresh_contacts();
void refresh_discovery();
const ContactEntry* find_contact_by_prefix(const uint8_t* prefix, int prefix_len = 7);
const ContactEntry* find_contact_by_name(const char* name);
const TelemetryEntry* find_telemetry(const uint8_t* prefix, int prefix_len = 7);
const TraceEntry* find_trace(uint32_t tag);

// Call from background tasks to refresh the model
void update_gps();
void update_battery();
void update_mesh();
void update_clock();

// Adaptive GPS poll cadence (ms): faster while moving, slow while parked.
// Computed from gps.speed_kmh, so it tracks the most recent fix.
uint32_t gps_update_interval_ms();

} // namespace model
