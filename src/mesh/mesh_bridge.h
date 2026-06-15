#pragma once

#if defined(ESP32)
  #include <freertos/FreeRTOS.h>
  #include <freertos/queue.h>
  #include <freertos/semphr.h>
  #include <freertos/task.h>
#else  // Adafruit nRF52 core: FreeRTOS headers without the freertos/ prefix
  #include <FreeRTOS.h>
  #include <queue.h>
  #include <semphr.h>
  #include <task.h>
#endif
#include <stdint.h>
#include <string.h>

// Thread-safe bridge between MeshCore (core 0) and UI (core 1).
// MeshCore callbacks push data into queues; UI reads from them.

namespace mesh::bridge {

// ---------- Data types ----------

struct ContactUpdate {
    char name[32];
    uint8_t pub_key[32];
    uint8_t type;       // ADV_TYPE_*
    uint8_t flags;      // bit 0 = favorite
    int32_t gps_lat;    // 6 decimal places * 1e6
    int32_t gps_lon;
    uint8_t path_len;   // 0xFF = unknown
    bool is_new;
};

struct MessageIn {
    char sender_name[32];
    char text[160];
    uint32_t timestamp;
    bool is_direct;     // direct vs flood routing
    bool is_channel;    // group channel message
    uint8_t channel_idx; // channel index for group messages; 0xFF = direct (not a channel)
};

struct TelemetryResponse {
    uint8_t pub_key_prefix[7];
    uint8_t data[96];
    uint8_t len;
};

// A peer's live GPS position, learned from a fast-GPS group beacon.
struct PositionUpdate {
    uint8_t pub_key_prefix[6];
    char name[32];      // resolved from contacts if known, else empty
    int32_t lat_e6;
    int32_t lon_e6;
    uint32_t timestamp; // local RX time (epoch seconds) — when we last heard the beacon
    uint8_t speed_kmh;  // sender's ground speed, km/h (0 if stationary/unknown)
};

struct TraceResponse {
    uint32_t tag;
    uint8_t hop_count;
    int8_t snr_there_q4;
    int8_t snr_back_q4;
};

struct MeshStatus {
    int peer_count;
    uint32_t last_rx_time;
    float last_rssi;
    float last_snr;
    bool radio_ok;
};

// ---------- Queue handles ----------

extern QueueHandle_t contact_queue;   // ContactUpdate items
extern QueueHandle_t message_queue;   // MessageIn items
extern QueueHandle_t telemetry_queue; // TelemetryResponse items
extern QueueHandle_t trace_queue;     // TraceResponse items
extern QueueHandle_t position_queue;  // PositionUpdate items

// ---------- Status (protected by mutex) ----------

extern SemaphoreHandle_t status_mutex;
extern MeshStatus status;

// ---------- Discovery change flag ----------

extern volatile bool discovery_changed;

// ---------- API ----------

void init();

// Called from mesh task (core 0)
void push_contact(const ContactUpdate& c);
void push_message(const MessageIn& m);
void push_telemetry(const TelemetryResponse& t);
void push_trace(const TraceResponse& t);
void push_position(const PositionUpdate& p);
void update_status(const MeshStatus& s);
void set_ui_task_handle(TaskHandle_t handle);
void mark_discovery_changed();

// Called from UI task (core 1)
bool pop_contact(ContactUpdate& c);
bool pop_message(MessageIn& m);
bool pop_telemetry(TelemetryResponse& t);
bool pop_trace(TraceResponse& t);
bool pop_position(PositionUpdate& p);
bool take_discovery_changed();
MeshStatus get_status();

} // namespace mesh::bridge
