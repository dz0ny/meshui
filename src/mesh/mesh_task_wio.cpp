// nRF52 mesh task — single-threaded: init in start(), run mesh.loop() from the
// main loop (no FreeRTOS task, no PSRAM, InternalFS instead of SPIFFS/SD).
// Mirrors mesh_task.cpp (ESP) minus the mutex/PSRAM/SD machinery.
#ifdef BOARD_WIO_L1

#include <Arduino.h>
#include <Wire.h>
#include <Mesh.h>
#include <InternalFileSystem.h>
#include <helpers/SimpleMeshTables.h>
#include "mesh_task.h"
#include "mesh_bridge.h"
#include "../util/text_filter.h"
#include "companion/target.h"
#include "companion/DataStore.h"
#include "companion/NodePrefs.h"
#include "companion/MyMesh.h"
#include "companion/NullSerialInterface.h"
#include "companion/BridgeUITask.h"
#include <helpers/nrf52/SerialBLEInterface.h>

namespace mesh::task {

static NullSerialInterface null_serial;
static SerialBLEInterface  ble_serial;
static BridgeUITask bridge_ui(&mc_board, &null_serial);
static StdRNG       mc_rng;
static SimpleMeshTables mc_tables;
static DataStore*   store = nullptr;
static MyMesh*      the_mesh = nullptr;
static volatile bool mesh_ready = false;
static bool         ble_active = false;

// BLE-enabled state persists as a single byte in LittleFS ("/ble"), mirroring the
// /lang and /msgchan UI prefs (NodePrefs has no field for it). 1 = on at boot.
static bool ble_pref_load() {
    using namespace Adafruit_LittleFS_Namespace;
    File f = InternalFS.open("/ble", FILE_O_READ);
    if (!f) return false;
    int b = f.read(); f.close();
    return b == 1;
}
static void ble_pref_save(bool on) {
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.remove("/ble");
    File f = InternalFS.open("/ble", FILE_O_WRITE);
    if (f) { uint8_t v = on ? 1 : 0; f.write(&v, 1); f.close(); }
}

void (*diag_step)(const char* step) = nullptr;

// Print to serial AND paint onto the e-ink (if a drawer is installed) so the
// last step is visible on the frozen panel even when serial can't be captured.
static void step(const char* s) {
    Serial.print("MESH: "); Serial.println(s);
    if (diag_step) diag_step(s);
}

void start(int /*core*/) {
    step("bridge.init");
    mesh::bridge::init();             // create the UI<->mesh queues + status mutex
                                      // (ESP path inits this in mesh_task.cpp; the
                                      //  nRF52 path must do it here or update_status/
                                      //  pop_* fault on the NULL FreeRTOS handles)
    step("board.begin");
    mc_board.begin();                 // Wire.begin() + buttons + DCDC + LoRa TX LED
    step("rtc.begin");
    rtc_clock.begin(Wire);            // probes I2C RTC (needs Wire up first!)

    step("radio_init");
    if (!radio_init()) {
        step("radio FAILED");
    }
    step("radio done");
    mc_rng.begin(radio_get_rng_seed());

    step("fs.begin");
    // Mount the on-chip LittleFS before anything touches it. Without this every
    // file op fails silently, so identity / prefs / contacts never persist
    // across reboots (matches the reference MeshCore nRF52 boot order).
    InternalFS.begin();

    step("datastore");
    // nRF52: InternalFS for identity/prefs/contacts (no SD), allocated in SRAM.
    store = new DataStore(InternalFS, rtc_clock);
    store->begin();

    step("mymesh");
    the_mesh = new MyMesh(radio_driver, mc_rng, rtc_clock, mc_tables, *store, &bridge_ui);
    the_mesh->begin(false);

    step("prefs");
    if (the_mesh->getNodePrefs()->manual_add_contacts == 0) {
        the_mesh->getNodePrefs()->manual_add_contacts = 1;
        the_mesh->savePrefs();
    }

    step("buzzer");
    // Buzzer comes up after prefs so it honours the persisted quiet flag, then
    // plays the startup chirp. No-op when PIN_BUZZER isn't defined.
    bridge_ui.begin(the_mesh->getNodePrefs()->buzzer_quiet != 0);

    step("iface");
    // Restore the BLE companion if it was on at last shutdown, else stay dark.
    if (ble_pref_load()) {
        ble_serial.begin(BLE_NAME_PREFIX, the_mesh->getNodePrefs()->node_name, the_mesh->getBLEPin());
        the_mesh->startInterface(ble_serial);
        ble_active = true;
        Serial.println("BLE: restored ON");
    } else {
        the_mesh->startInterface(null_serial);
    }

    step("radio params");
    NodePrefs* p = the_mesh->getNodePrefs();
    radio_set_params(p->freq, p->bw, p->sf, p->cr);
    radio_set_tx_power(p->tx_power_dbm);

    step("sensors");
    sensors.begin();
#if ENV_INCLUDE_GPS == 1
    the_mesh->applyGpsPrefs();
#endif

    Serial.printf("MESH: node '%s' on %.3f MHz\n", the_mesh->getNodeName(), p->freq);
#if defined(ENABLE_ADVERT_ON_BOOT) && ENABLE_ADVERT_ON_BOOT == 1
    step("advert");
    the_mesh->advert();
#endif
    step("ready");
    mesh_ready = true;
}

void loop() {
    if (!the_mesh) return;
    the_mesh->loop();
    bridge_ui.loop();   // pump non-blocking buzzer playback
    sensors.loop();
    rtc_clock.tick();   // advance the VolatileRTCClock fallback (no HW RTC on the Wio)

    mesh::bridge::MeshStatus s = {};
    s.peer_count = the_mesh->getNumContacts();
    s.last_rssi  = radio_driver.getLastRSSI();
    s.last_snr   = radio_driver.getLastSNR();
    s.radio_ok   = true;
    mesh::bridge::update_status(s);
}

bool is_ready() { return mesh_ready; }

const char* node_name() { return the_mesh ? the_mesh->getNodeName() : ""; }

bool send_to_name(const char* name, const char* text) {
    if (!the_mesh || !name || !text) return false;
    ContactInfo* r = the_mesh->searchContactsByPrefix(name);
    if (!r) return false;
    char clean[160]; util::strip_emoji_copy(clean, sizeof(clean), text);
    uint32_t ack, timeout;
    return the_mesh->sendMessage(*r, rtc_clock.getCurrentTime(), 0, clean, ack, timeout) != MSG_SEND_FAILED;
}

bool send_message(const char* recipient_prefix, const char* text) {
    return send_to_name(recipient_prefix, text);
}

bool send_channel(uint8_t channel_idx, const char* text) {
    if (!the_mesh || !text) return false;
    ChannelDetails ch;
    if (the_mesh->getChannel(channel_idx, ch) && ch.name[0]) {
        char clean[160]; util::strip_emoji_copy(clean, sizeof(clean), text);
        return the_mesh->sendGroupMessage(rtc_clock.getCurrentTime(), ch.channel,
                                          the_mesh->getNodeName(), clean, strlen(clean));
    }
    return false;
}

bool send_public(const char* text) { return send_channel(0, text); }

float   get_freq()         { return the_mesh ? the_mesh->getNodePrefs()->freq : 0; }
float   get_bw()           { return the_mesh ? the_mesh->getNodePrefs()->bw : 0; }
uint8_t get_sf()           { return the_mesh ? the_mesh->getNodePrefs()->sf : 0; }
uint8_t get_cr()           { return the_mesh ? the_mesh->getNodePrefs()->cr : 0; }
int8_t  get_tx_power()     { return the_mesh ? the_mesh->getNodePrefs()->tx_power_dbm : 0; }
uint32_t get_packets_recv(){ return radio_driver.getPacketsRecv(); }
uint32_t get_packets_sent(){ return radio_driver.getPacketsSent(); }

// ---- radio / GPS config setters (single-threaded: no mutex needed) ----------
// Radio params are read by the SX1262 driver at init; changing them persists to
// prefs and takes effect on the next reboot.
void set_freq(float freq_mhz) {
    if (!the_mesh) return;
    the_mesh->getNodePrefs()->freq = freq_mhz; the_mesh->savePrefs();
}
void set_bw(float bw_khz) {
    if (!the_mesh) return;
    the_mesh->getNodePrefs()->bw = bw_khz; the_mesh->savePrefs();
}
void set_sf(uint8_t sf) {
    if (!the_mesh) return;
    the_mesh->getNodePrefs()->sf = sf; the_mesh->savePrefs();
}
void set_cr(uint8_t cr) {
    if (!the_mesh) return;
    the_mesh->getNodePrefs()->cr = cr; the_mesh->savePrefs();
}
void set_tx_power(int8_t dbm) {
    if (!the_mesh) return;
    the_mesh->getNodePrefs()->tx_power_dbm = dbm; the_mesh->savePrefs();
}

void set_gps_enabled(bool enabled) {
    if (!the_mesh) return;
    the_mesh->getNodePrefs()->gps_enabled = enabled ? 1 : 0;
    the_mesh->applyGpsPrefs();
    the_mesh->savePrefs();
}
bool get_gps_enabled() {
    return the_mesh ? the_mesh->getNodePrefs()->gps_enabled != 0 : false;
}

void set_advert_location(bool share) {
    if (!the_mesh) return;
    the_mesh->getNodePrefs()->advert_loc_policy = share ? 1 : 0;
    the_mesh->savePrefs();
}
bool get_advert_location() {
    return the_mesh ? the_mesh->getNodePrefs()->advert_loc_policy != 0 : false;
}

void set_client_repeat(bool enabled) {
    if (!the_mesh) return;
    the_mesh->getNodePrefs()->client_repeat = enabled ? 1 : 0;
    the_mesh->savePrefs();
}
bool get_client_repeat() {
    return the_mesh ? the_mesh->getNodePrefs()->client_repeat != 0 : false;
}

// ---- BLE companion (Bluefruit) ---------------------------------------------
void ble_enable() {
    if (ble_active || !the_mesh) return;
    ble_serial.begin(BLE_NAME_PREFIX, the_mesh->getNodePrefs()->node_name, the_mesh->getBLEPin());
    the_mesh->startInterface(ble_serial);
    ble_active = true;
    ble_pref_save(true);
    Serial.println("BLE: enabled");
}
void ble_disable() {
    if (!ble_active) return;
    ble_serial.disable();
    if (the_mesh) the_mesh->startInterface(null_serial);
    ble_active = false;
    ble_pref_save(false);
    Serial.println("BLE: disabled");
}
bool ble_is_enabled() { return ble_active; }

void set_ble_pin(uint32_t pin) {
    if (!the_mesh) return;
    the_mesh->getNodePrefs()->ble_pin = pin;
    the_mesh->savePrefs();
}
uint32_t get_ble_pin() {
    if (!the_mesh) return BLE_PIN_CODE;
    uint32_t pin = the_mesh->getNodePrefs()->ble_pin;
    return pin > 0 ? pin : BLE_PIN_CODE;
}
uint32_t regen_ble_pin() {
    uint32_t pin = (uint32_t)mc_rng.nextInt(100000, 999999);
    set_ble_pin(pin);
    return pin;
}

// Single-threaded on the nRF52 (mesh.loop() runs from the main loop), so no
// mutex — iterate contacts straight into the bridge for the model to ingest.
void push_all_contacts() {
    if (!the_mesh) return;
    ContactsIterator iter = the_mesh->startContactsIterator();
    ContactInfo c;
    while (iter.hasNext(the_mesh, c)) {
        mesh::bridge::ContactUpdate cu = {};
        strncpy(cu.name, c.name, sizeof(cu.name) - 1);
        memcpy(cu.pub_key, c.id.pub_key, 32);
        cu.type    = c.type;
        cu.flags   = c.flags;
        cu.gps_lat = c.gps_lat;
        cu.gps_lon = c.gps_lon;
        cu.path_len = c.out_path_len;
        cu.is_new  = false;
        mesh::bridge::push_contact(cu);
    }
}

// Enumerate configured channels (idx 0 = Public, 1+ = provisioned via companion).
// A slot is "present" when its name is non-empty.
int get_channels(ChannelEntry* dest, int max_num) {
    if (!the_mesh || !dest || max_num <= 0) return 0;
    int n = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS && n < max_num; i++) {
        ChannelDetails ch;
        if (the_mesh->getChannel(i, ch) && ch.name[0]) {
            dest[n].idx = (uint8_t)i;
            strncpy(dest[n].name, ch.name, sizeof(dest[n].name) - 1);
            dest[n].name[sizeof(dest[n].name) - 1] = 0;
            n++;
        }
    }
    return n;
}

uint8_t get_fast_gps_channel() {
    return the_mesh ? the_mesh->getNodePrefs()->fast_gps_channel_idx : FAST_GPS_DISABLED;
}
void set_fast_gps_channel(uint8_t channel_idx) {
    if (!the_mesh) return;
    the_mesh->getNodePrefs()->fast_gps_channel_idx = channel_idx;
    the_mesh->savePrefs();   // engine re-reads prefs each loop, so this takes effect live
}

uint8_t get_fast_gps_region() {
    return the_mesh ? the_mesh->getNodePrefs()->fast_gps_region : 0;
}
void set_fast_gps_region(uint8_t region_idx) {
    if (!the_mesh) return;
    the_mesh->getNodePrefs()->fast_gps_region = region_idx;
    the_mesh->savePrefs();
}
bool get_buzzer_enabled() {
    return the_mesh ? (the_mesh->getNodePrefs()->buzzer_quiet == 0) : false;
}
void set_buzzer_enabled(bool enabled) {
    if (!the_mesh) return;
    the_mesh->getNodePrefs()->buzzer_quiet = enabled ? 0 : 1;
    bridge_ui.setBuzzerQuiet(!enabled);   // apply live to the running buzzer
    the_mesh->savePrefs();
}

uint8_t fast_gps_region_count() { return MyMesh::fastGpsRegionCount(); }
const char* fast_gps_region_label(uint8_t idx) {
    if (idx == 0) return "Unscoped";
    return MyMesh::fastGpsRegionName(idx);
}

// Active Messages-screen channel — persisted as a single byte in LittleFS so it
// survives reboots (NodePrefs has no field for a pure-UI selection). Defaults to
// channel 0 (Public).
static uint8_t s_msg_channel = 0;
static bool    s_msg_channel_loaded = false;
static void load_msg_channel() {
    if (s_msg_channel_loaded) return;
    s_msg_channel_loaded = true;
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();   // idempotent — guard against reading before the FS is mounted
    File f = InternalFS.open("/msgchan", FILE_O_READ);
    if (f) { int b = f.read(); f.close(); if (b >= 0) s_msg_channel = (uint8_t)b; }
}
uint8_t get_msg_channel() { load_msg_channel(); return s_msg_channel; }
void set_msg_channel(uint8_t channel_idx) {
    s_msg_channel = channel_idx;
    s_msg_channel_loaded = true;
    using namespace Adafruit_LittleFS_Namespace;
    InternalFS.begin();   // idempotent — ensure the FS is mounted before we write
    InternalFS.remove("/msgchan");
    File f = InternalFS.open("/msgchan", FILE_O_WRITE);
    if (f) { f.write(&channel_idx, 1); f.close(); }
}

int  get_discovered(DiscoveredNode*, int) { return 0; }
bool add_contact_by_prefix(const uint8_t*) { return false; }
bool is_contact(const uint8_t*) { return false; }

} // namespace mesh::task
#endif // BOARD_WIO_L1
