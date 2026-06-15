#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include "model.h"
#include "trail_store.h"
#include "util/text_filter.h"
#include "board.h"
#include "mesh/mesh_bridge.h"
#include "mesh/mesh_task.h"
#include "mesh/companion/target.h"
#include <helpers/sensors/LocationProvider.h>
#include "nvs_param.h"
#include "sd_log.h"

namespace model {

GPS     gps = {};
Battery battery = {};
Mesh    mesh = {};
TrailStore trail;
Clock   clock = {};
ContactEntry contacts[MAX_CONTACT_ENTRIES] = {};
int contact_count = 0;
uint32_t contacts_revision = 0;
uint32_t epoch_now = 0;
DiscoveryEntry discovery[MAX_DISCOVERY_ENTRIES] = {};
int discovery_count = 0;
uint32_t discovery_revision = 0;
TelemetryEntry telemetry[MAX_TELEMETRY_ENTRIES] = {};
uint32_t telemetry_revision = 0;
TraceEntry traces[MAX_TRACE_ENTRIES] = {};
uint32_t trace_revision = 0;
LivePosition live_positions[MAX_LIVE_POSITIONS] = {};
int live_position_count = 0;
uint32_t live_position_revision = 0;
static portMUX_TYPE dirty_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t dirty_flags = DIRTY_NONE;
static uint32_t telemetry_seq = 0;
static uint32_t trace_seq = 0;

static int find_contact_index_by_prefix_internal(const uint8_t* prefix, int prefix_len) {
    if (!prefix || prefix_len <= 0) return -1;
    for (int i = 0; i < contact_count; i++) {
        if (memcmp(contacts[i].pub_key, prefix, prefix_len) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_contact_index_by_name_internal(const char* name) {
    if (!name || !name[0]) return -1;
    for (int i = 0; i < contact_count; i++) {
        if (strcmp(contacts[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static bool apply_contact_update(const mesh::bridge::ContactUpdate& cu) {
    int idx = find_contact_index_by_prefix_internal(cu.pub_key, 7);
    if (idx < 0) {
        if (contact_count >= MAX_CONTACT_ENTRIES) return false;
        idx = contact_count++;
        memset(&contacts[idx], 0, sizeof(contacts[idx]));
    }

    ContactEntry& entry = contacts[idx];
    bool changed = false;
    bool has_path = (cu.path_len != 0xFF && cu.path_len > 0);

    if (strncmp(entry.name, cu.name, sizeof(entry.name)) != 0) {
        strncpy(entry.name, cu.name, sizeof(entry.name) - 1);
        entry.name[sizeof(entry.name) - 1] = 0;
        changed = true;
    }
    if (memcmp(entry.pub_key, cu.pub_key, sizeof(entry.pub_key)) != 0) {
        memcpy(entry.pub_key, cu.pub_key, sizeof(entry.pub_key));
        changed = true;
    }
    if (entry.type != cu.type) {
        entry.type = cu.type;
        changed = true;
    }
    if (entry.flags != cu.flags) {
        entry.flags = cu.flags;
        changed = true;
    }
    if (entry.has_path != has_path) {
        entry.has_path = has_path;
        changed = true;
    }
    if (entry.gps_lat != cu.gps_lat) {
        entry.gps_lat = cu.gps_lat;
        changed = true;
    }
    if (entry.gps_lon != cu.gps_lon) {
        entry.gps_lon = cu.gps_lon;
        changed = true;
    }

    return changed;
}

static int find_telemetry_index_internal(const uint8_t* prefix, int prefix_len) {
    if (!prefix || prefix_len <= 0) return -1;
    for (int i = 0; i < MAX_TELEMETRY_ENTRIES; i++) {
        if (!telemetry[i].valid) continue;
        if (memcmp(telemetry[i].pub_key_prefix, prefix, prefix_len) == 0) {
            return i;
        }
    }
    return -1;
}

static void store_telemetry(const mesh::bridge::TelemetryResponse& tr) {
    int idx = find_telemetry_index_internal(tr.pub_key_prefix, 7);
    if (idx < 0) {
        idx = -1;
        for (int i = 0; i < MAX_TELEMETRY_ENTRIES; i++) {
            if (!telemetry[i].valid) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            uint32_t oldest_seq = telemetry[0].seq;
            idx = 0;
            for (int i = 1; i < MAX_TELEMETRY_ENTRIES; i++) {
                if (telemetry[i].seq < oldest_seq) {
                    oldest_seq = telemetry[i].seq;
                    idx = i;
                }
            }
        }
    }

    telemetry[idx].valid = true;
    memcpy(telemetry[idx].pub_key_prefix, tr.pub_key_prefix, sizeof(telemetry[idx].pub_key_prefix));
    telemetry[idx].len = tr.len;
    memcpy(telemetry[idx].data, tr.data, tr.len);
    telemetry[idx].timestamp = (uint32_t)time(nullptr);
    telemetry[idx].seq = ++telemetry_seq;
}

static int find_trace_index_internal(uint32_t tag) {
    for (int i = 0; i < MAX_TRACE_ENTRIES; i++) {
        if (!traces[i].valid) continue;
        if (traces[i].tag == tag) {
            return i;
        }
    }
    return -1;
}

static void store_trace(const mesh::bridge::TraceResponse& tr) {
    int idx = find_trace_index_internal(tr.tag);
    if (idx < 0) {
        idx = -1;
        for (int i = 0; i < MAX_TRACE_ENTRIES; i++) {
            if (!traces[i].valid) {
                idx = i;
                break;
            }
        }
        if (idx < 0) {
            uint32_t oldest_seq = traces[0].seq;
            idx = 0;
            for (int i = 1; i < MAX_TRACE_ENTRIES; i++) {
                if (traces[i].seq < oldest_seq) {
                    oldest_seq = traces[i].seq;
                    idx = i;
                }
            }
        }
    }

    traces[idx].valid = true;
    traces[idx].tag = tr.tag;
    traces[idx].hop_count = tr.hop_count;
    traces[idx].snr_there_q4 = tr.snr_there_q4;
    traces[idx].snr_back_q4 = tr.snr_back_q4;
    traces[idx].timestamp = millis();
    traces[idx].seq = ++trace_seq;
}

void mark_dirty(uint32_t flags) {
    portENTER_CRITICAL(&dirty_lock);
    dirty_flags |= flags;
    portEXIT_CRITICAL(&dirty_lock);
}

uint32_t take_dirty() {
    portENTER_CRITICAL(&dirty_lock);
    uint32_t flags = dirty_flags;
    dirty_flags = DIRTY_NONE;
    portEXIT_CRITICAL(&dirty_lock);
    return flags;
}

void refresh_contacts() {
    memset(contacts, 0, sizeof(contacts));
    contact_count = 0;

    mesh::task::push_all_contacts();

    mesh::bridge::ContactUpdate cu;
    while (mesh::bridge::pop_contact(cu)) {
        apply_contact_update(cu);
    }

    contacts_revision++;
}

void refresh_discovery() {
    mesh::task::DiscoveredNode nodes[MAX_DISCOVERY_ENTRIES] = {};
    int count = mesh::task::get_discovered(nodes, MAX_DISCOVERY_ENTRIES);

    memset(discovery, 0, sizeof(discovery));
    discovery_count = count;
    for (int i = 0; i < discovery_count; i++) {
        strncpy(discovery[i].name, nodes[i].name, sizeof(discovery[i].name) - 1);
        discovery[i].name[sizeof(discovery[i].name) - 1] = 0;
        memcpy(discovery[i].pubkey_prefix, nodes[i].pubkey_prefix, sizeof(discovery[i].pubkey_prefix));
        discovery[i].path_len = nodes[i].path_len;
        discovery[i].recv_timestamp = nodes[i].recv_timestamp;
    }

    discovery_revision++;
}

void ingest_bridge_events() {
    mesh::bridge::MessageIn message = {};
    while (mesh::bridge::pop_message(message)) {
        // Strip emoji from received text/sender — the e-ink fonts can't render
        // them, so they'd otherwise show as blank boxes.
        util::strip_emoji_inplace(message.sender_name);
        util::strip_emoji_inplace(message.text);
        if (messages && message_count < MAX_STORED_MESSAGES) {
            auto& msg = messages[message_count];
            if (message.sender_name[0]) strncpy(msg.sender, message.sender_name, sizeof(msg.sender) - 1);
            if (message.text[0]) strncpy(msg.text, message.text, sizeof(msg.text) - 1);
            msg.hour = clock.hour;
            msg.minute = clock.minute;
            msg.is_self = false;
            msg.channel_idx = message.channel_idx;
            message_count++;
        }

        sd_log::mark_dirty();
        note_incoming_message(message.sender_name[0] ? message.sender_name : nullptr,
                              message.text[0] ? message.text : nullptr);
    }

    bool contacts_changed = false;
    mesh::bridge::ContactUpdate contact = {};
    while (mesh::bridge::pop_contact(contact)) {
        contacts_changed |= apply_contact_update(contact);
    }
    if (contacts_changed) {
        contacts_revision++;
    }

    bool telemetry_changed = false;
    mesh::bridge::TelemetryResponse telemetry_response = {};
    while (mesh::bridge::pop_telemetry(telemetry_response)) {
        store_telemetry(telemetry_response);
        telemetry_changed = true;
    }
    if (telemetry_changed) {
        telemetry_revision++;
    }

    bool trace_changed = false;
    mesh::bridge::TraceResponse trace = {};
    while (mesh::bridge::pop_trace(trace)) {
        store_trace(trace);
        trace_changed = true;
    }
    if (trace_changed) {
        trace_revision++;
    }

    mesh::bridge::PositionUpdate position = {};
    while (mesh::bridge::pop_position(position)) {
        upsert_live_position(position.pub_key_prefix, position.name,
                             position.lat_e6, position.lon_e6, position.timestamp,
                             position.speed_kmh);
    }

    if (mesh::bridge::take_discovery_changed()) {
        refresh_discovery();
    }
}

void upsert_live_position(const uint8_t* prefix6, const char* name,
                          int32_t lat_e6, int32_t lon_e6, uint32_t timestamp,
                          uint8_t speed_kmh) {
    int slot = -1;
    for (int i = 0; i < live_position_count; i++) {
        if (memcmp(live_positions[i].pub_key_prefix, prefix6, 6) == 0) { slot = i; break; }
    }
    if (slot < 0) {
        if (live_position_count < MAX_LIVE_POSITIONS) {
            slot = live_position_count++;
        } else {  // table full — evict the stalest entry
            slot = 0;
            for (int i = 1; i < live_position_count; i++)
                if (live_positions[i].timestamp < live_positions[slot].timestamp) slot = i;
        }
        memset(&live_positions[slot], 0, sizeof(LivePosition));
        memcpy(live_positions[slot].pub_key_prefix, prefix6, 6);
    }
    LivePosition& p = live_positions[slot];
    p.lat_e6 = lat_e6; p.lon_e6 = lon_e6; p.timestamp = timestamp; p.speed_kmh = speed_kmh; p.valid = true;
    if (name && name[0]) { strncpy(p.name, name, sizeof(p.name) - 1); p.name[sizeof(p.name) - 1] = 0; }
    live_position_revision++;
}

const ContactEntry* find_contact_by_prefix(const uint8_t* prefix, int prefix_len) {
    int idx = find_contact_index_by_prefix_internal(prefix, prefix_len);
    return idx >= 0 ? &contacts[idx] : nullptr;
}

const ContactEntry* find_contact_by_name(const char* name) {
    int idx = find_contact_index_by_name_internal(name);
    return idx >= 0 ? &contacts[idx] : nullptr;
}

const TelemetryEntry* find_telemetry(const uint8_t* prefix, int prefix_len) {
    int idx = find_telemetry_index_internal(prefix, prefix_len);
    return idx >= 0 ? &telemetry[idx] : nullptr;
}

const TraceEntry* find_trace(uint32_t tag) {
    int idx = find_trace_index_internal(tag);
    return idx >= 0 ? &traces[idx] : nullptr;
}

// Local great-circle helpers. We can't reuse ui::geo (geo_utils.h) here because
// board.h pulls in <Arduino.h>, whose DEG_TO_RAD macro would clobber the token
// inside that namespace. Use a private constant the macro can't touch.
namespace {
constexpr double kDegToRad = 0.017453292519943295;

double great_circle_m(double la1, double lo1, double la2, double lo2) {
    double dLat = (la2 - la1) * kDegToRad;
    double dLon = (lo2 - lo1) * kDegToRad;
    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(la1 * kDegToRad) * cos(la2 * kDegToRad) *
               sin(dLon / 2) * sin(dLon / 2);
    return 6371000.0 * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

double course_deg(double la1, double lo1, double la2, double lo2) {
    double dLon = (lo2 - lo1) * kDegToRad;
    double y = sin(dLon) * cos(la2 * kDegToRad);
    double x = cos(la1 * kDegToRad) * sin(la2 * kDegToRad) -
               sin(la1 * kDegToRad) * cos(la2 * kDegToRad) * cos(dLon);
    return fmod(atan2(y, x) * (180.0 / M_PI) + 360.0, 360.0);
}

// --- Fast-GPS beacon schedule (mirror of MyMesh::maybeSendFastGpsUpdate) -----
// The trail sampler reuses the *exact* decision the mesh core uses to emit a
// fast-GPS beacon, so the breadcrumb track visualises precisely what would be
// transmitted: a 10 m movement gate, a speed-aware moving interval, and a
// stationary backoff that doubles 60 s → 1024 s while parked. Keep these
// constants in lock-step with the FAST_GPS_* defines in MyMesh.cpp.
constexpr double   kFastGpsMinMovementM        = 10.0;
constexpr uint32_t kFastGpsStationaryBaseMs    = 60UL * 1000UL;
constexpr uint32_t kFastGpsStationaryMaxMs     = 1024UL * 1000UL;
constexpr double   kFastGpsSpeedIdleMaxMps     = 0.75;
constexpr double   kFastGpsSpeedWalkMaxMps     = 1.8;
constexpr double   kFastGpsSpeedFastMaxMps     = 4.0;
constexpr uint32_t kFastGpsWalkIntervalMs      = 30UL * 1000UL;
constexpr uint32_t kFastGpsFastIntervalMs      = 15UL * 1000UL;
constexpr uint32_t kFastGpsVeryFastIntervalMs  = 5UL * 1000UL;

uint32_t fast_gps_moving_interval_ms(double speed_mps) {
    if (speed_mps < kFastGpsSpeedIdleMaxMps) return kFastGpsStationaryBaseMs;
    if (speed_mps < kFastGpsSpeedWalkMaxMps) return kFastGpsWalkIntervalMs;
    if (speed_mps < kFastGpsSpeedFastMaxMps) return kFastGpsFastIntervalMs;
    return kFastGpsVeryFastIntervalMs;
}
} // namespace

// Derive ground speed (km/h) and course-over-ground from the displacement
// between successive valid fixes. The LocationProvider abstraction doesn't
// expose GPS-reported speed, so we integrate position ourselves. A small
// displacement gate rejects the few-metre horizontal jitter of a parked
// receiver; a longer timeout lets a genuinely stationary device decay to 0.
static void update_speed_heading() {
    static double   prev_lat = 0, prev_lng = 0;
    static uint32_t prev_ms = 0;
    static bool     prev_valid = false;

    if (!gps.has_fix) {
        prev_valid = false;
        gps.speed_kmh = 0.0;
        gps.heading_valid = false;
        return;
    }

    uint32_t now_ms = millis();
    if (!prev_valid) {
        prev_lat = gps.lat; prev_lng = gps.lng; prev_ms = now_ms;
        prev_valid = true;
        return;
    }

    double   dist_m = great_circle_m(prev_lat, prev_lng, gps.lat, gps.lng);
    uint32_t dt_ms  = now_ms - prev_ms;
    const double GATE_M = 5.0;  // ~horizontal noise of a good fix

    if (dist_m >= GATE_M && dt_ms >= 1000) {
        double inst_kmh = (dist_m / 1000.0) / ((double)dt_ms / 3600000.0);
        if (inst_kmh <= 300.0) {  // reject NMEA glitch jumps
            // Light EWMA — sparse updates, so weight the new sample heavily.
            gps.speed_kmh = gps.speed_kmh * 0.4 + inst_kmh * 0.6;
            gps.heading_deg = course_deg(prev_lat, prev_lng, gps.lat, gps.lng);
            gps.heading_valid = true;
        }
        prev_lat = gps.lat; prev_lng = gps.lng; prev_ms = now_ms;
    } else if (dt_ms >= 8000) {
        // Stayed inside the gate for a while → parked. Decay to zero and
        // re-baseline so accumulated jitter doesn't later read as motion.
        gps.speed_kmh = 0.0;
        prev_lat = gps.lat; prev_lng = gps.lng; prev_ms = now_ms;
    }
    // else: below gate but recent — keep the old baseline so slow walking
    // accumulates enough displacement to cross GATE_M on a later call.
}

// Local GPS-read cadence only. This does NOT drive any LoRa transmission — the
// fast-GPS position beacon has its own speed-aware, rate-limited schedule in
// MyMesh (min 5 s on air). 2 s is the floor here so we never poll per-second.
uint32_t gps_update_interval_ms() {
    if (!gps.has_fix) return 5000;   // searching — moderate poll
    double v = gps.speed_kmh;
    if (v >= 30.0) return 2000;      // driving fast
    if (v >= 10.0) return 3000;      // cycling / city driving
    if (v >= 3.0)  return 5000;      // walking
    return 10000;                    // stationary → slow, save power
}

void update_gps() {
    bool prev_has_fix = gps.has_fix;
    bool prev_module_ok = gps.module_ok;

    // Read from MeshCore's EnvironmentSensorManager / MicroNMEALocationProvider
    LocationProvider* loc = sensors.getLocationProvider();
    if (loc) {
        gps.module_ok = true;
        gps.has_fix = loc->isValid();
        if (gps.has_fix) {
            gps.lat = loc->getLatitude() / 1e6;
            gps.lng = loc->getLongitude() / 1e6;
            gps.altitude_m = loc->getAltitude() / 1000.0;
        }
        gps.satellites = loc->satellitesCount();
        gps.status_text = gps.has_fix ? "Fix OK" : "Searching...";

        update_speed_heading();

        // GPS breadcrumb trail sampling — runs in the background whenever
        // tracking is active, independent of the current screen. We store a
        // point on exactly the same schedule the mesh core uses to *transmit* a
        // fast-GPS beacon (10 m movement gate + speed-aware moving interval +
        // stationary 60→1024 s backoff), so the rendered track is a faithful
        // simulation of what actually goes out over the air.
        if (trail.isActive() && gps.has_fix) {
            static bool     fg_sent_valid = false;
            static double   fg_last_lat = 0, fg_last_lng = 0;
            static uint32_t fg_last_at_ms = 0;
            static uint32_t fg_next_stationary_at = 0;
            static uint32_t fg_stationary_interval_ms = kFastGpsStationaryBaseMs;

            uint32_t now_ms = millis();
            bool should_store    = !fg_sent_valid;
            bool reset_backoff   = should_store;
            if (!should_store) {
                double dist_m = great_circle_m(fg_last_lat, fg_last_lng, gps.lat, gps.lng);
                if (dist_m > kFastGpsMinMovementM) {
                    if (fg_last_at_ms != 0 && now_ms > fg_last_at_ms) {
                        uint32_t elapsed_ms = now_ms - fg_last_at_ms;
                        double speed_mps = (dist_m * 1000.0) / (double)elapsed_ms;
                        should_store = elapsed_ms >= fast_gps_moving_interval_ms(speed_mps);
                    }
                    if (should_store) reset_backoff = true;
                } else if (fg_next_stationary_at != 0 &&
                           (int32_t)(now_ms - fg_next_stationary_at) >= 0) {
                    should_store = true;   // parked beacon keep-alive
                }
            }

            if (should_store) {
                time_t now = 0;
                time(&now);
                trail.addPoint((int32_t)loc->getLatitude(),
                               (int32_t)loc->getLongitude(),
                               (uint32_t)now,
                               0);  // gate already enforced by the beacon schedule
                fg_sent_valid = true;
                fg_last_lat = gps.lat; fg_last_lng = gps.lng;
                fg_last_at_ms = now_ms;
                if (reset_backoff) {
                    fg_stationary_interval_ms = kFastGpsStationaryBaseMs;
                } else if (fg_stationary_interval_ms < kFastGpsStationaryMaxMs) {
                    fg_stationary_interval_ms *= 2;
                    if (fg_stationary_interval_ms > kFastGpsStationaryMaxMs)
                        fg_stationary_interval_ms = kFastGpsStationaryMaxMs;
                }
                fg_next_stationary_at = now_ms + fg_stationary_interval_ms;
            }
        }
    } else {
        gps.module_ok = false;
        gps.status_text = "No Module";
        gps.speed_kmh = 0.0;
        gps.heading_valid = false;
    }

    // Sync hardware RTC so it persists across reboots (e-paper board only).
#ifdef BOARD_EPAPER
    static bool hw_rtc_synced = false;
    if (gps.has_fix && !hw_rtc_synced && board::peri_status[E_PERI_RTC]) {
        time_t now;
        time(&now);
        struct tm utc;
        gmtime_r(&now, &utc);
        board::rtc.setDateTime(utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                               utc.tm_hour, utc.tm_min, utc.tm_sec);
        hw_rtc_synced = true;
        Serial.println("GPS: hardware RTC synced");
    }
#endif

    if (gps.has_fix != prev_has_fix || gps.module_ok != prev_module_ok) {
        mark_dirty(DIRTY_GPS);
    }
}

void update_battery() {
    uint16_t prev_percent = battery.percent;
    uint16_t prev_voltage_mv = battery.voltage_mv;
    bool prev_charging = battery.charging;

    battery.percent = board::battery_percent();
    battery.voltage_mv = board::battery_voltage_mv();
    battery.current_ma = board::battery_current_ma();
    uint16_t temp_raw = board::battery_temperature();
    battery.temperature_c = (temp_raw > 0 && temp_raw < 10000) ? (temp_raw / 10.0f) - 273.15f : 0;
    battery.remain_mah = board::battery_remain_capacity();
    battery.full_mah = board::battery_full_capacity();
    battery.design_mah = board::battery_design_capacity();
    battery.health_pct = board::battery_health();
    battery.charging = board::battery_is_charging();

    battery.charger_ok = board::charger_is_valid();
    if (battery.charger_ok) {
        battery.charge_status = board::charger_status_str();
        battery.bus_status = board::charger_bus_status_str();
        battery.ntc_status = board::charger_ntc_status_str();
        battery.vbus_v = board::charger_vbus_v();
        battery.vsys_v = board::charger_vsys_v();
        battery.vbat_v = board::charger_vbat_v();
        battery.charge_current_ma = board::charger_current_ma();
    }

    if (battery.percent != prev_percent || battery.voltage_mv != prev_voltage_mv || battery.charging != prev_charging) {
        mark_dirty(DIRTY_BATTERY);
    }
}

void update_mesh() {
    bool prev_ble_enabled = mesh.ble_enabled;
    const char* prev_node_name = mesh.node_name;

    auto ms = mesh::bridge::get_status();
    mesh.peer_count = ms.peer_count;
    mesh.last_rssi = ms.last_rssi;
    mesh.last_snr = ms.last_snr;
    mesh.radio_ok = ms.radio_ok;
    mesh.ble_enabled = mesh::task::ble_is_enabled();
    mesh.rx_packets = mesh::task::get_packets_recv();
    mesh.tx_packets = mesh::task::get_packets_sent();
    mesh.node_name = mesh::task::node_name();
    mesh.freq_mhz = mesh::task::get_freq();
    mesh.bw_khz = mesh::task::get_bw();
    mesh.sf = mesh::task::get_sf();
    mesh.cr = mesh::task::get_cr();
    mesh.tx_power_dbm = mesh::task::get_tx_power();

    if (mesh.ble_enabled != prev_ble_enabled || mesh.node_name != prev_node_name) {
        mark_dirty(DIRTY_MESH);
    }
}

void update_clock() {
    uint8_t prev_hour = clock.hour;
    uint8_t prev_minute = clock.minute;
    uint8_t prev_day = clock.day;
    uint8_t prev_month = clock.month;
    uint8_t prev_year = clock.year;

    // Read UTC from ESP32 system clock (seeded from hardware RTC at boot, updated by GPS)
    time_t now;
    time(&now);
    if (now > 1700000000) epoch_now = (uint32_t)now;   // valid epoch only
    struct tm utc;
    gmtime_r(&now, &utc);

    uint8_t utc_h = utc.tm_hour;
    uint8_t utc_m = utc.tm_min;
    uint8_t utc_s = utc.tm_sec;
    int full_year = utc.tm_year + 1900;
    clock.year  = (full_year >= 2000 && full_year <= 2099) ? full_year - 2000 : 0;

    static bool clock_debug_once = false;
    if (!clock_debug_once) {
        Serial.printf("update_clock: time_t=%ld year=%d UTC=%02d:%02d:%02d\n",
            (long)now, full_year, utc_h, utc_m, utc_s);
        clock_debug_once = true;
    }
    clock.month = utc.tm_mon + 1;
    clock.day   = utc.tm_mday;

    // Auto timezone from GPS coordinates, fallback to MeshCore advertised location.
    double lat = gps.lat;
    double lng = gps.lng;
    bool has_loc = gps.has_fix && (lat != 0.0 || lng != 0.0);

    if (!has_loc && mesh.node_name) {
        lat = mesh::task::get_node_lat();
        lng = mesh::task::get_node_lon();
        has_loc = (lat != 0.0 || lng != 0.0);
    }

    if (has_loc) {
        // Europe special cases
        if (lat > 35 && lat < 72 && lng > -12 && lng < 40) {
            if (lng < 0)            clock.tz_offset_hours = 0;  // UK, Portugal, Iceland
            else if (lng < 16)      clock.tz_offset_hours = 1;  // CET
            else if (lng < 30)      clock.tz_offset_hours = 2;  // EET
            else                    clock.tz_offset_hours = 3;  // Moscow
        }
        else if (lng < -52) {
            if (lng < -120)         clock.tz_offset_hours = -8; // PST
            else if (lng < -105)    clock.tz_offset_hours = -7; // MST
            else if (lng < -90)     clock.tz_offset_hours = -6; // CST
            else if (lng < -70)     clock.tz_offset_hours = -5; // EST
            else                    clock.tz_offset_hours = -4; // AST
        }
        else if (lng > 40) {
            if (lng < 60)           clock.tz_offset_hours = 4;
            else if (lng < 82)      clock.tz_offset_hours = 5;
            else if (lng < 98)      clock.tz_offset_hours = 6;
            else if (lng < 105)     clock.tz_offset_hours = 7;
            else if (lng < 120)     clock.tz_offset_hours = 8;
            else if (lng < 135)     clock.tz_offset_hours = 9;
            else if (lng < 150)     clock.tz_offset_hours = 10;
            else                    clock.tz_offset_hours = 12;
        }
        else {
            clock.tz_offset_hours = (int8_t)(lng / 15.0);
        }
    }

    // DST detection
    int8_t dst = 0;
    if (has_loc && clock.year > 0) {
        uint8_t m = clock.month;
        uint8_t d = clock.day;
        if (lat > 35 && lat < 72 && lng > -12 && lng < 40) {
            if (m >= 4 && m <= 9) dst = 1;
            else if (m == 3 && d >= 25) dst = 1;
            else if (m == 10 && d < 25) dst = 1;
        }
        else if (lng < -52 && lng > -130) {
            if (m >= 4 && m <= 10) dst = 1;
            else if (m == 3 && d >= 8) dst = 1;
            else if (m == 11 && d < 8) dst = 1;
        }
    }

    int local_h = utc_h + clock.tz_offset_hours + dst;
    if (local_h < 0) local_h += 24;
    if (local_h >= 24) local_h -= 24;

    clock.hour = (uint8_t)local_h;
    clock.minute = utc_m;
    clock.second = utc_s;

    if (clock.hour != prev_hour ||
        clock.minute != prev_minute ||
        clock.day != prev_day ||
        clock.month != prev_month ||
        clock.year != prev_year) {
        mark_dirty(DIRTY_CLOCK);
    }
}

// Message history — allocated in PSRAM to save DRAM
StoredMessage* messages = nullptr;
int message_count = 0;

void init_messages() {
    if (!messages) {
        messages = (StoredMessage*)heap_caps_calloc(MAX_STORED_MESSAGES, sizeof(StoredMessage), MALLOC_CAP_SPIRAM);
    }
}

void delete_message(int idx) {
    if (idx < 0 || idx >= message_count) return;
    for (int i = idx; i < message_count - 1; i++) {
        messages[i] = messages[i + 1];
    }
    message_count--;
    mark_dirty(DIRTY_MESSAGES);
}

void note_incoming_message(const char* from_name, const char* text) {
    sleep_cfg.unread_messages++;
    note_contact_unread(from_name);   // per-contact tally for the Team screen badge
    if (from_name) {
        strncpy(sleep_cfg.last_sender, from_name, sizeof(sleep_cfg.last_sender) - 1);
        sleep_cfg.last_sender[sizeof(sleep_cfg.last_sender) - 1] = 0;
    }
    if (text) {
        strncpy(sleep_cfg.last_message, text, sizeof(sleep_cfg.last_message) - 1);
        sleep_cfg.last_message[sizeof(sleep_cfg.last_message) - 1] = 0;
    }
    mark_dirty(DIRTY_MESSAGES | DIRTY_SLEEP);
}

void clear_unread_messages() {
    if (sleep_cfg.unread_messages == 0 && sleep_cfg.last_sender[0] == 0 && sleep_cfg.last_message[0] == 0) return;
    sleep_cfg.unread_messages = 0;
    sleep_cfg.last_sender[0] = 0;
    sleep_cfg.last_message[0] = 0;
    mark_dirty(DIRTY_SLEEP);
}

// Sleep
static const uint32_t timeout_presets[] = {0, 60000, 120000, 300000, 900000, 1800000};
Sleep sleep_cfg = {};
static bool sleep_loaded = false;

void touch_activity() {
    sleep_cfg.last_activity_ms = millis();
}

bool should_sleep() {
    // Load from NVS on first check
    if (!sleep_loaded) {
        sleep_cfg.timeout_idx = nvs_param_get_u8(NVS_ID_SLEEP_TIMEOUT);
        if (sleep_cfg.timeout_idx >= 6) sleep_cfg.timeout_idx = 3;
        sleep_cfg.timeout_ms = timeout_presets[sleep_cfg.timeout_idx];
        sleep_cfg.last_activity_ms = millis();
        sleep_loaded = true;
    }
    if (sleep_cfg.timeout_ms == 0) return false;
    return (millis() - sleep_cfg.last_activity_ms) > sleep_cfg.timeout_ms;
}

} // namespace model
