// Mono model — defines the model:: globals the ported screens read, with stub
// implementations of the update/query functions. On the tracker these will be
// fed by real nRF52 GPS / RTC / MeshCore later; for now they hold defaults so
// the shared screens render.
#ifdef BOARD_WIO_L1

#include "model.h"
#include "trail_store.h"
#include "waypoint_store.h"
#include "util/text_filter.h"
#include "mesh/mesh_bridge.h"
#include "mesh/mesh_task.h"          // push_all_contacts()
#include "mesh/companion/target.h"   // mc_board.getBattMilliVolts() (Wio L1 ADC read)
#include <string.h>

namespace model {

GPS     gps = {};
Battery battery = {};
Mesh    mesh = {};
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
Sleep sleep_cfg = {};
StoredMessage* messages = nullptr;
int message_count = 0;
LivePosition live_positions[MAX_LIVE_POSITIONS] = {};
int live_position_count = 0;
uint32_t live_position_revision = 0;
TrailStore trail;
WaypointStore waypoints;

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

// Merge one contact from the bridge into contacts[] (matched by 7-byte prefix).
// Mirrors the ESP model's apply_contact_update so the Team screen can read
// chat-type favorites on the tracker too.
static void apply_contact_update(const mesh::bridge::ContactUpdate& cu) {
    int idx = -1;
    for (int i = 0; i < contact_count; i++) {
        if (memcmp(contacts[i].pub_key, cu.pub_key, 7) == 0) { idx = i; break; }
    }
    if (idx < 0) {
        if (contact_count >= MAX_CONTACT_ENTRIES) return;
        idx = contact_count++;
        memset(&contacts[idx], 0, sizeof(contacts[idx]));
    }
    ContactEntry& e = contacts[idx];
    strncpy(e.name, cu.name, sizeof(e.name) - 1);
    e.name[sizeof(e.name) - 1] = 0;
    memcpy(e.pub_key, cu.pub_key, sizeof(e.pub_key));
    e.type     = cu.type;
    e.flags    = cu.flags;
    e.has_path = (cu.path_len != 0xFF && cu.path_len > 0);
    e.gps_lat  = cu.gps_lat;
    e.gps_lon  = cu.gps_lon;
}

void init_messages() {
    if (!messages) messages = new StoredMessage[MAX_STORED_MESSAGES]();
}
void touch_activity() {}
bool should_sleep() { return false; }
void delete_message(int) {}
void note_incoming_message(const char* from, const char* text, uint8_t channel_idx) {
    note_contact_unread(from);   // per-contact tally for the Team screen badge (all channels)

    // Dashboard/lock-screen unread count + preview track only the selected channel.
    if (channel_idx != mesh::task::get_msg_channel()) return;

    sleep_cfg.unread_messages++;
    if (from) { strncpy(sleep_cfg.last_sender, from, sizeof(sleep_cfg.last_sender) - 1); }
    if (text) { strncpy(sleep_cfg.last_message, text, sizeof(sleep_cfg.last_message) - 1); }
}
void clear_unread_messages() { sleep_cfg.unread_messages = 0; }
void mark_dirty(uint32_t) {}
uint32_t take_dirty() { return 0; }

// Drain the mesh->UI bridge into model::messages so the chat screen shows real
// traffic. Other queues are drained-and-ignored to keep them from filling.
void ingest_bridge_events() {
    mesh::bridge::MessageIn m = {};
    while (mesh::bridge::pop_message(m)) {
        util::strip_emoji_inplace(m.sender_name);   // e-ink fonts have no emoji glyphs
        util::strip_emoji_inplace(m.text);
        if (messages && message_count < MAX_STORED_MESSAGES) {
            StoredMessage& msg = messages[message_count];
            memset(&msg, 0, sizeof(msg));
            if (m.sender_name[0]) strncpy(msg.sender, m.sender_name, sizeof(msg.sender) - 1);
            if (m.text[0])        strncpy(msg.text,   m.text,        sizeof(msg.text)   - 1);
            msg.hour = clock.hour; msg.minute = clock.minute; msg.is_self = false;
            msg.channel_idx = m.channel_idx;
            message_count++;
        }
        note_incoming_message(m.sender_name[0] ? m.sender_name : nullptr,
                              m.text[0] ? m.text : nullptr,
                              m.channel_idx);
    }
    mesh::bridge::PositionUpdate p = {};
    while (mesh::bridge::pop_position(p)) {
        upsert_live_position(p.pub_key_prefix, p.name, p.lat_e6, p.lon_e6, p.timestamp, p.speed_kmh);
    }
    mesh::bridge::ContactUpdate c = {};       while (mesh::bridge::pop_contact(c)) { apply_contact_update(c); }
    mesh::bridge::TelemetryResponse t = {};   while (mesh::bridge::pop_telemetry(t)) {}
    mesh::bridge::TraceResponse tr = {};      while (mesh::bridge::pop_trace(tr)) {}
    mesh::bridge::take_discovery_changed();
}
void refresh_contacts() {
    memset(contacts, 0, sizeof(contacts));
    contact_count = 0;
    mesh::task::push_all_contacts();
    mesh::bridge::ContactUpdate cu = {};
    while (mesh::bridge::pop_contact(cu)) apply_contact_update(cu);
    contacts_revision++;
}
void refresh_discovery() {}
const ContactEntry* find_contact_by_prefix(const uint8_t* prefix, int prefix_len) {
    if (!prefix || prefix_len <= 0) return nullptr;
    for (int i = 0; i < contact_count; i++)
        if (memcmp(contacts[i].pub_key, prefix, prefix_len) == 0) return &contacts[i];
    return nullptr;
}
const ContactEntry* find_contact_by_name(const char* name) {
    if (!name || !name[0]) return nullptr;
    for (int i = 0; i < contact_count; i++)
        if (strcmp(contacts[i].name, name) == 0) return &contacts[i];
    return nullptr;
}
const TelemetryEntry* find_telemetry(const uint8_t*, int) { return nullptr; }
const TraceEntry* find_trace(uint32_t) { return nullptr; }
void update_gps() {}

// Wio Tracker L1 has no fuel-gauge / charger IC — just an ADC divider on
// PIN_VBAT_READ. Read the cell voltage via the MeshCore board's
// getBattMilliVolts() (the proven solo-branch read: VBAT_ENABLE pulse +
// AR_INTERNAL + settle delay) and estimate a percentage from the Li-ion curve
// endpoints. Current/charger fields stay 0.
void update_battery() {
    uint16_t mv = mc_board.getBattMilliVolts();
    battery.voltage_mv = mv;
    battery.vbat_v     = mv / 1000.0f;

    float v = mv / 1000.0f;
    float pct = (v - 3.30f) / (4.20f - 3.30f) * 100.0f;
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    battery.percent    = (uint16_t)(pct + 0.5f);
    battery.charging   = (v > 4.25f);   // USB present pulls the rail above full
    battery.current_ma = 0;
}
void update_mesh() {}
void update_clock() {}

} // namespace model
#endif // BOARD_WIO_L1
