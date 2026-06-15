#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <ctime>
#ifdef DEG_TO_RAD
#undef DEG_TO_RAD
#endif
#include "ping.h"
#include "../ui_theme.h"
#include "../ui_port.h"
#include "../ui_screen_mgr.h"
#include "../components/nav_button.h"
#include "../components/geo_utils.h"
#include "../components/toast.h"
#include "../../sd_log.h"
#include "../../mesh/mesh_task.h"
#include "../../model.h"
#include <helpers/AdvertDataHelpers.h>

namespace ui::screen::ping {

static lv_obj_t* scr = NULL;
static lv_obj_t* history_list = NULL;
static lv_obj_t* lbl_auto_action = NULL;
static lv_obj_t* lbl_status = NULL;
static lv_obj_t* lbl_route = NULL;
static lv_obj_t* history_rows[24] = {};
static lv_obj_t* history_labels[24] = {};

static char contact_name[32];
static int32_t contact_lat = 0;
static int32_t contact_lon = 0;
static uint8_t contact_type = 0;
static bool contact_has_path = false;
static uint8_t contact_pubkey[7] = {};

static const uint32_t TELEMETRY_TIMEOUT_MS = 5000;
static const uint32_t TRACE_TIMEOUT_MS = 10000;
static const uint32_t AUTO_PING_INTERVAL_MS = 2000;
static const uint32_t MAX_AUTO_PING_DELAY_MS = 5000;
static const uint32_t TIMEOUT_PENALTY_MS = 10000;

struct PingEntry {
    bool success;
    bool timed_out;
    bool relay;
    bool used_flood;
    bool retried_flood;
    uint32_t duration_ms;
    uint8_t hop_count;
    int8_t snr_there_q4;
    int8_t snr_back_q4;
    uint32_t timestamp;
};

static PingEntry history[24] = {};
static int history_count = 0;

static bool ping_pending = false;
static bool pending_force_flood = false;
static bool pending_retried_flood = false;
static bool pending_relay = false;
static uint32_t pending_since_ms = 0;
static uint32_t pending_trace_tag = 0;
static uint32_t pending_telemetry_seq = 0;
static uint32_t pending_trace_seq = 0;
static bool auto_ping_enabled = false;
static uint32_t next_auto_ping_at = 0;

static bool is_relay_contact() {
    return contact_type == ADV_TYPE_REPEATER;
}

static sd_log::PingHistoryEntry to_store_entry(const PingEntry& entry) {
    sd_log::PingHistoryEntry stored = {};
    stored.success = entry.success;
    stored.timed_out = entry.timed_out;
    stored.relay = entry.relay;
    stored.used_flood = entry.used_flood;
    stored.retried_flood = entry.retried_flood;
    stored.duration_ms = entry.duration_ms;
    stored.hop_count = entry.hop_count;
    stored.snr_there_q4 = entry.snr_there_q4;
    stored.snr_back_q4 = entry.snr_back_q4;
    stored.timestamp = entry.timestamp;
    return stored;
}

static PingEntry from_store_entry(const sd_log::PingHistoryEntry& entry) {
    PingEntry stored = {};
    stored.success = entry.success;
    stored.timed_out = entry.timed_out;
    stored.relay = entry.relay;
    stored.used_flood = entry.used_flood;
    stored.retried_flood = entry.retried_flood;
    stored.duration_ms = entry.duration_ms;
    stored.hop_count = entry.hop_count;
    stored.snr_there_q4 = entry.snr_there_q4;
    stored.snr_back_q4 = entry.snr_back_q4;
    stored.timestamp = entry.timestamp;
    return stored;
}

static void persist_history() {
    sd_log::PingHistoryEntry stored[24] = {};
    for (int i = 0; i < history_count; i++) {
        stored[i] = to_store_entry(history[i]);
    }
    sd_log::store_ping_history(contact_pubkey, stored, history_count);
}

static void load_history() {
    sd_log::PingHistoryEntry stored[24] = {};
    uint8_t count = 0;
    history_count = 0;
    memset(history, 0, sizeof(history));
    if (!sd_log::get_ping_history(contact_pubkey, stored, &count, 24)) {
        return;
    }
    history_count = count;
    for (int i = 0; i < history_count; i++) {
        history[i] = from_store_entry(stored[i]);
    }
}

static lv_obj_t* create_card(lv_obj_t* parent) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(card, lv_color_hex(EPD_COLOR_BG), LV_PART_MAIN);
    lv_obj_set_style_border_width(card, UI_BORDER_CARD, LV_PART_MAIN);
    lv_obj_set_style_border_color(card, lv_color_hex(EPD_COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_radius(card, 20, LV_PART_MAIN);
    lv_obj_set_style_pad_all(card, 18, LV_PART_MAIN);
    lv_obj_set_style_pad_row(card, 10, LV_PART_MAIN);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static void format_distance(char* out, size_t out_size) {
    if (!out || out_size == 0) return;
    out[0] = 0;

    bool has_contact_gps = (contact_lat != 0 || contact_lon != 0);
    if (!has_contact_gps) return;
    if (!model::gps.has_fix) {
        snprintf(out, out_size, "Distance waiting for GPS");
        return;
    }

    double c_lat = contact_lat / 1e6;
    double c_lon = contact_lon / 1e6;
    double dist = ui::geo::distance_km(model::gps.lat, model::gps.lng, c_lat, c_lon);
    double bearing = ui::geo::bearing(model::gps.lat, model::gps.lng, c_lat, c_lon);
    if (dist < 1.0) {
        snprintf(out, out_size, "Distance %dm %s", (int)(dist * 1000), ui::geo::bearing_to_cardinal(bearing));
    } else {
        snprintf(out, out_size, "Distance %.1fkm %s", dist, ui::geo::bearing_to_cardinal(bearing));
    }
}

static void format_time(char* out, size_t out_size, uint32_t timestamp) {
    if (!out || out_size == 0) return;
    out[0] = 0;
    if (timestamp == 0) return;

    time_t raw = (time_t)timestamp;
    struct tm local_tm;
    localtime_r(&raw, &local_tm);
    snprintf(out, out_size, "%02d:%02d:%02d", local_tm.tm_hour, local_tm.tm_min, local_tm.tm_sec);
}

static void update_auto_action() {
    if (!lbl_auto_action) return;
    lv_label_set_text(lbl_auto_action, auto_ping_enabled ? "Auto On" : "Auto Off");
}

static void update_status_text(const char* status_line) {
    if (!lbl_status || !lbl_route) return;

    char distance_line[48];
    format_distance(distance_line, sizeof(distance_line));

    char status_buf[160];
    if (distance_line[0]) {
        snprintf(status_buf, sizeof(status_buf), "%s\n%s", status_line, distance_line);
    } else {
        snprintf(status_buf, sizeof(status_buf), "%s", status_line);
    }
    lv_label_set_text(lbl_status, status_buf);

    char route_buf[96];
    if (is_relay_contact()) {
        snprintf(route_buf, sizeof(route_buf), "Relay trace");
    } else if (contact_has_path) {
        snprintf(route_buf, sizeof(route_buf), "Telemetry ping via direct then flood fallback");
    } else {
        snprintf(route_buf, sizeof(route_buf), "Telemetry ping via flood");
    }
    lv_label_set_text(lbl_route, route_buf);
}

static void insert_history(const PingEntry& entry) {
    if (history_count < (int)(sizeof(history) / sizeof(history[0]))) {
        for (int i = history_count; i > 0; i--) {
            history[i] = history[i - 1];
        }
        history[0] = entry;
        history_count++;
    } else {
        for (int i = (int)(sizeof(history) / sizeof(history[0])) - 1; i > 0; i--) {
            history[i] = history[i - 1];
        }
        history[0] = entry;
    }
}

static void rebuild_history() {
    if (!history_list) return;

    for (int i = 0; i < 24; i++) {
        if (!history_rows[i]) {
            history_rows[i] = create_card(history_list);
            history_labels[i] = lv_label_create(history_rows[i]);
            lv_obj_set_width(history_labels[i], lv_pct(100));
            lv_label_set_long_mode(history_labels[i], LV_LABEL_LONG_WRAP);
            lv_obj_set_style_text_font(history_labels[i], UI_FONT_SMALL, LV_PART_MAIN);
            lv_obj_set_style_text_color(history_labels[i], lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
            lv_obj_add_flag(history_rows[i], LV_OBJ_FLAG_HIDDEN);
        }
    }

    for (int i = 0; i < 24; i++) {
        if (i >= history_count) {
            lv_obj_add_flag(history_rows[i], LV_OBJ_FLAG_HIDDEN);
            continue;
        }

        char time_buf[16];
        format_time(time_buf, sizeof(time_buf), history[i].timestamp);

        char distance_buf[48];
        format_distance(distance_buf, sizeof(distance_buf));

        char body[192];
        if (history[i].timed_out) {
            snprintf(body, sizeof(body), "%s\nTimeout%s%s",
                     time_buf[0] ? time_buf : "Now",
                     distance_buf[0] ? "\n" : "",
                     distance_buf[0] ? distance_buf : "");
        } else if (history[i].relay) {
            snprintf(body, sizeof(body), "%s\n%lums  %u hops\nThere %.1f dB  Back %.1f dB%s%s",
                     time_buf[0] ? time_buf : "Now",
                     (unsigned long)history[i].duration_ms,
                     history[i].hop_count,
                     history[i].snr_there_q4 / 4.0f,
                     history[i].snr_back_q4 / 4.0f,
                     distance_buf[0] ? "\n" : "",
                     distance_buf[0] ? distance_buf : "");
        } else {
            const char* mode = history[i].used_flood
                ? (history[i].retried_flood ? "Flood retry" : "Flood")
                : "Direct";
            snprintf(body, sizeof(body), "%s\n%lums  %s%s%s",
                     time_buf[0] ? time_buf : "Now",
                     (unsigned long)history[i].duration_ms,
                     mode,
                     distance_buf[0] ? "\n" : "",
                     distance_buf[0] ? distance_buf : "");
        }

        lv_label_set_text(history_labels[i], body);
        lv_obj_clear_flag(history_rows[i], LV_OBJ_FLAG_HIDDEN);
    }

    ui::port::keyboard_focus_invalidate();
}

static void schedule_next_auto_ping(bool timed_out, uint32_t duration_ms) {
    if (!auto_ping_enabled) {
        next_auto_ping_at = 0;
        return;
    }

    uint32_t response_delay = timed_out ? TIMEOUT_PENALTY_MS : duration_ms;
    uint32_t next_delay = AUTO_PING_INTERVAL_MS + response_delay;
    if (next_delay > MAX_AUTO_PING_DELAY_MS) {
        next_delay = MAX_AUTO_PING_DELAY_MS;
    }
    next_auto_ping_at = millis() + next_delay;
}

static void finish_ping(const PingEntry& entry, const char* status_line) {
    ping_pending = false;
    pending_force_flood = false;
    pending_retried_flood = false;
    pending_relay = false;
    pending_since_ms = 0;
    pending_trace_tag = 0;
    pending_telemetry_seq = 0;
    pending_trace_seq = 0;

    insert_history(entry);
    persist_history();
    rebuild_history();
    update_status_text(status_line);
    schedule_next_auto_ping(entry.timed_out, entry.duration_ms);
}

static bool begin_ping(bool force_flood) {
    if (ping_pending) return false;

    if (is_relay_contact()) {
        uint32_t tag = 0;
        if (!mesh::task::request_trace_ping(contact_pubkey, &tag)) {
            ui::toast::show("Ping failed");
            update_status_text("Trace request failed");
            schedule_next_auto_ping(true, 0);
            return false;
        }
        ping_pending = true;
        pending_relay = true;
        pending_trace_tag = tag;
        pending_trace_seq = model::trace_revision;
        pending_since_ms = millis();
        update_status_text("Tracing relay...");
        return true;
    }

    if (!mesh::task::request_telemetry(contact_pubkey, force_flood)) {
        ui::toast::show("Ping failed");
        update_status_text("Telemetry request failed");
        schedule_next_auto_ping(true, 0);
        return false;
    }

    ping_pending = true;
    pending_relay = false;
    pending_force_flood = force_flood;
    const model::TelemetryEntry* telemetry = model::find_telemetry(contact_pubkey);
    pending_telemetry_seq = telemetry ? telemetry->seq : 0;
    pending_since_ms = millis();
    update_status_text(force_flood ? "Flood ping..." : (contact_has_path ? "Direct ping..." : "Flood ping..."));
    return true;
}

static void start_ping() {
    next_auto_ping_at = 0;
    begin_ping(false);
}

static void on_back(lv_event_t* e) {
    ui::screen_mgr::pop(true);
}

static void on_toggle_auto(lv_event_t* e) {
    auto_ping_enabled = !auto_ping_enabled;
    if (!auto_ping_enabled) {
        next_auto_ping_at = 0;
    } else if (!ping_pending) {
        next_auto_ping_at = millis();
    }
    update_auto_action();
}

static void on_ping_now(lv_event_t* e) {
    start_ping();
}

static void on_clear(lv_event_t* e) {
    history_count = 0;
    memset(history, 0, sizeof(history));
    persist_history();
    rebuild_history();
    update_status_text("History cleared");
}

static void handle_telemetry_response(const model::TelemetryEntry& telemetry) {
    if (!ping_pending || pending_relay || !telemetry.valid ||
        memcmp(telemetry.pub_key_prefix, contact_pubkey, 7) != 0 ||
        telemetry.seq <= pending_telemetry_seq) {
        return;
    }

    PingEntry entry = {};
    entry.success = true;
    entry.timed_out = false;
    entry.relay = false;
    entry.used_flood = pending_force_flood || !contact_has_path;
    entry.retried_flood = pending_retried_flood;
    entry.duration_ms = millis() - pending_since_ms;
    entry.timestamp = (uint32_t)time(NULL);

    char status[96];
    snprintf(status, sizeof(status), "Reply in %lums", (unsigned long)entry.duration_ms);
    finish_ping(entry, status);
}

static void handle_trace_response(const model::TraceEntry& trace) {
    if (!ping_pending || !pending_relay || !trace.valid ||
        trace.tag != pending_trace_tag || trace.seq <= pending_trace_seq) {
        return;
    }

    PingEntry entry = {};
    entry.success = true;
    entry.timed_out = false;
    entry.relay = true;
    entry.duration_ms = millis() - pending_since_ms;
    entry.hop_count = trace.hop_count;
    entry.snr_there_q4 = trace.snr_there_q4;
    entry.snr_back_q4 = trace.snr_back_q4;
    entry.timestamp = (uint32_t)time(NULL);

    char status[96];
    snprintf(status, sizeof(status), "Reply in %lums", (unsigned long)entry.duration_ms);
    finish_ping(entry, status);
}

static void handle_timeout() {
    if (!ping_pending) return;

    uint32_t elapsed = millis() - pending_since_ms;
    uint32_t timeout_ms = pending_relay ? TRACE_TIMEOUT_MS : TELEMETRY_TIMEOUT_MS;
    if (elapsed < timeout_ms) return;

    if (!pending_relay && contact_has_path && !pending_force_flood) {
        ping_pending = false;
        pending_retried_flood = true;
        update_status_text("Direct timeout, retrying flood...");
        begin_ping(true);
        pending_retried_flood = true;
        return;
    }

    PingEntry entry = {};
    entry.success = false;
    entry.timed_out = true;
    entry.relay = pending_relay;
    entry.used_flood = pending_force_flood || !contact_has_path;
    entry.retried_flood = pending_retried_flood;
    entry.timestamp = (uint32_t)time(NULL);
    finish_ping(entry, "Ping timed out");
}

void process_events() {
    if (!scr) return;

    const model::TelemetryEntry* telemetry = model::find_telemetry(contact_pubkey);
    if (telemetry) {
        handle_telemetry_response(*telemetry);
    }

    const model::TraceEntry* trace = model::find_trace(pending_trace_tag);
    if (trace) {
        handle_trace_response(*trace);
    }

    handle_timeout();

    if (auto_ping_enabled && !ping_pending && next_auto_ping_at != 0 && millis() >= next_auto_ping_at) {
        begin_ping(false);
    }
}

void set_contact(const char* name, int32_t gps_lat, int32_t gps_lon, uint8_t type, bool has_path,
                 const uint8_t* pubkey_prefix) {
    strncpy(contact_name, name ? name : "", sizeof(contact_name) - 1);
    contact_name[sizeof(contact_name) - 1] = 0;
    contact_lat = gps_lat;
    contact_lon = gps_lon;
    contact_type = type;
    contact_has_path = has_path;
    memset(contact_pubkey, 0, sizeof(contact_pubkey));
    if (pubkey_prefix) {
        memcpy(contact_pubkey, pubkey_prefix, sizeof(contact_pubkey));
    }
}

static void create(ui::kit::Handle parent_kit) {
    lv_obj_t* parent = (lv_obj_t*)parent_kit;
    scr = parent;
    lbl_auto_action = ui::screen_mgr::set_nav_actions(
        auto_ping_enabled ? "Auto On" : "Auto Off", on_toggle_auto, NULL,
        "Clear", on_clear, NULL);

    history_list = ui::nav::scroll_list(parent);
    lv_obj_set_style_pad_row(history_list, 12, LV_PART_MAIN);
    lv_obj_set_style_pad_hor(history_list, 10, LV_PART_MAIN);

    lv_obj_t* summary_card = create_card(history_list);
    lv_obj_set_width(summary_card, lv_pct(100));

    lv_obj_t* name_label = lv_label_create(summary_card);
    lv_obj_set_style_text_font(name_label, UI_FONT_TITLE, LV_PART_MAIN);
    lv_obj_set_style_text_color(name_label, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);
    lv_obj_set_width(name_label, lv_pct(100));
    lv_label_set_long_mode(name_label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(name_label, contact_name);

    lbl_status = lv_label_create(summary_card);
    lv_obj_set_width(lbl_status, lv_pct(100));
    lv_label_set_long_mode(lbl_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(lbl_status, UI_FONT_BODY, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);

    lbl_route = lv_label_create(summary_card);
    lv_obj_set_width(lbl_route, lv_pct(100));
    lv_label_set_long_mode(lbl_route, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(lbl_route, UI_FONT_SMALL, LV_PART_MAIN);
    lv_obj_set_style_text_color(lbl_route, lv_color_hex(EPD_COLOR_TEXT), LV_PART_MAIN);

    lv_obj_t* ping_btn = ui::nav::text_button(history_list, "Ping", on_ping_now, NULL);
    lv_obj_set_width(ping_btn, lv_pct(100));

    update_auto_action();
    update_status_text("Tap Ping Again");
    rebuild_history();
}

static void entry() {
    load_history();
    ping_pending = false;
    pending_force_flood = false;
    pending_retried_flood = false;
    pending_relay = false;
    pending_since_ms = 0;
    pending_trace_tag = 0;
    pending_telemetry_seq = 0;
    pending_trace_seq = 0;
    next_auto_ping_at = 0;
    rebuild_history();
    start_ping();
    process_events();
}

static void exit_fn() {
    ping_pending = false;
    pending_telemetry_seq = 0;
    pending_trace_seq = 0;
    next_auto_ping_at = 0;
}

static void destroy() {
    scr = NULL;
    history_list = NULL;
    lbl_auto_action = NULL;
    lbl_status = NULL;
    lbl_route = NULL;
    for (int i = 0; i < 24; i++) {
        history_rows[i] = NULL;
        history_labels[i] = NULL;
    }
}

screen_lifecycle_t lifecycle = {
    .create  = create,
    .entry   = entry,
    .exit    = exit_fn,
    .destroy = destroy,
};

} // namespace ui::screen::ping
