#pragma once

#include <stdint.h>

namespace mesh::task {

// Optional diagnostic hook: called with the name of each init step inside
// start() *before* that step runs. The app installs a drawer that paints the
// step onto the e-ink panel, so if a step hangs the frozen screen shows the
// last step reached (capture-free boot diagnostic). Null => no-op.
extern void (*diag_step)(const char* step);

// Start the mesh networking task on the specified core.
// Must be called after board::init() since it uses the SPI bus and LoRa radio.
void start(int core);

// nRF52 (single-core): run one mesh iteration from the main loop. Unused on
// ESP32, which runs mesh on a pinned FreeRTOS task instead.
void loop();

// True once mesh init is complete (identity loaded, radio configured).
bool is_ready();

// Send a text message to the currently selected contact.
// Thread-safe — can be called from UI core.
bool send_message(const char* recipient_prefix, const char* text);

// Send a message to a contact found by name prefix.
bool send_to_name(const char* name, const char* text);

// Send a broadcast to the public channel.
bool send_public(const char* text);
bool send_channel(uint8_t channel_idx, const char* text);

struct ChannelEntry {
    uint8_t idx;
    char name[32];
};

// Get the node name.
const char* node_name();

// Radio parameters (read current values)
float get_freq();
float get_bw();
uint8_t get_sf();
uint8_t get_cr();
int8_t get_tx_power();
uint32_t get_packets_recv();
uint32_t get_packets_sent();

// Push all known contacts to the bridge queue (for UI to display).
void push_all_contacts();
int get_channels(ChannelEntry* dest, int max_num);

// Fast GPS broadcast: periodically send our position to a group channel.
// Channel 0xFF (FAST_GPS_DISABLED) = off; channel 0 (Public) is also treated as
// disabled by the engine. Persists in NodePrefs.
static const uint8_t FAST_GPS_DISABLED = 0xFF;
uint8_t get_fast_gps_channel();
void    set_fast_gps_channel(uint8_t channel_idx);

// Region scope for fast-GPS beacons. Index 0 = unscoped (flood everywhere, as
// before); 1..N are public hashtag regions (si, si-not, ...) that region-aware
// repeaters confine the flood to. Persists in NodePrefs.
uint8_t     get_fast_gps_region();
void        set_fast_gps_region(uint8_t region_idx);
uint8_t     fast_gps_region_count();             // number of options incl. unscoped
const char* fast_gps_region_label(uint8_t idx);  // UI label ("Unscoped", "si", ...)

// Buzzer (piezo) notification sounds. Persisted via NodePrefs.buzzer_quiet
// (inverted: enabled == !quiet). No-op on boards without a buzzer.
bool get_buzzer_enabled();
void set_buzzer_enabled(bool enabled);

// Active channel shown on the Messages screen. The chat screen filters its list
// to messages tagged with this channel index. Persisted across reboots.
uint8_t get_msg_channel();
void    set_msg_channel(uint8_t channel_idx);

// Discovery: get recently heard nodes (not yet contacts)
struct DiscoveredNode {
    char name[32];
    uint8_t pubkey_prefix[7];
    uint8_t path_len;
    uint32_t recv_timestamp;
};
int get_discovered(DiscoveredNode* dest, int max_num);

// Add a discovered node as a contact (by pubkey prefix match)
bool add_contact_by_prefix(const uint8_t* pubkey_prefix);

// Check if a pubkey prefix belongs to an existing contact
bool is_contact(const uint8_t* pubkey_prefix);

// Remove a contact by pubkey prefix
bool remove_contact_by_prefix(const uint8_t* pubkey_prefix);

// Set radio params (saves to prefs, requires reboot to apply)
void set_node_name(const char* name);
void set_freq(float freq_mhz);
void set_bw(float bw_khz);
void set_sf(uint8_t sf);
void set_cr(uint8_t cr);
void set_tx_power(int8_t dbm);

// GPS location sharing over mesh adverts
void set_gps_enabled(bool enabled);
bool get_gps_enabled();
void set_advert_location(bool share);
bool get_advert_location();

// Client repeat: when on, this node forwards (repeats) packets it hears,
// acting as a lightweight repeater in addition to its client role.
void set_client_repeat(bool enabled);
bool get_client_repeat();

// Node advertised location (from prefs)
double get_node_lat();
double get_node_lon();

// BLE companion app control
void ble_enable();
void ble_disable();
bool ble_is_enabled();
void set_ble_pin(uint32_t pin);
uint32_t get_ble_pin();
// Generate a fresh random 6-digit BLE pairing PIN, persist it, and return it.
uint32_t regen_ble_pin();

// Toggle favorite flag on a contact (bit 0 of ContactInfo::flags)
bool is_favorite(const uint8_t* pubkey_prefix);
bool toggle_favorite(const uint8_t* pubkey_prefix);
bool request_telemetry(const uint8_t* pubkey_prefix, bool force_flood = false);
bool request_trace_ping(const uint8_t* pubkey_prefix, uint32_t* out_tag = nullptr);

// Clear all contacts or channels (saves immediately)
void clear_contacts();
void clear_channels();
void flush_storage();

// Enter light sleep — waits for radio idle, then sleeps both cores.
// Wakes on LoRa packet (DIO1) or timer.
void enter_sleep(uint32_t wake_secs);

} // namespace mesh::task
