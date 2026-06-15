#include "mesh_bridge.h"

namespace mesh::bridge {

QueueHandle_t contact_queue = NULL;
QueueHandle_t message_queue = NULL;
QueueHandle_t telemetry_queue = NULL;
QueueHandle_t trace_queue = NULL;
QueueHandle_t position_queue = NULL;
SemaphoreHandle_t status_mutex = NULL;
MeshStatus status = {};
volatile bool discovery_changed = false;
static TaskHandle_t ui_task_handle = NULL;

static void notify_ui_task() {
    if (!ui_task_handle) return;
    xTaskNotifyGive(ui_task_handle);
}

void init() {
    contact_queue = xQueueCreate(128, sizeof(ContactUpdate));
    message_queue = xQueueCreate(32, sizeof(MessageIn));
    telemetry_queue = xQueueCreate(16, sizeof(TelemetryResponse));
    trace_queue = xQueueCreate(16, sizeof(TraceResponse));
    position_queue = xQueueCreate(32, sizeof(PositionUpdate));
    status_mutex = xSemaphoreCreateMutex();
}

void push_contact(const ContactUpdate& c) {
    if (!contact_queue) return;
    xQueueSend(contact_queue, &c, 0); // don't block
    notify_ui_task();
}

void push_message(const MessageIn& m) {
    if (!message_queue) return;
    xQueueSend(message_queue, &m, 0);
    notify_ui_task();
}

void push_telemetry(const TelemetryResponse& t) {
    if (!telemetry_queue) return;
    xQueueSend(telemetry_queue, &t, 0);
    notify_ui_task();
}

void push_trace(const TraceResponse& t) {
    if (!trace_queue) return;
    xQueueSend(trace_queue, &t, 0);
    notify_ui_task();
}

void push_position(const PositionUpdate& p) {
    if (!position_queue) return;
    xQueueSend(position_queue, &p, 0);
    notify_ui_task();
}

void update_status(const MeshStatus& s) {
    if (status_mutex && xSemaphoreTake(status_mutex, pdMS_TO_TICKS(10))) {
        status = s;
        xSemaphoreGive(status_mutex);
    }
}

void set_ui_task_handle(TaskHandle_t handle) {
    ui_task_handle = handle;
}

void mark_discovery_changed() {
    discovery_changed = true;
    notify_ui_task();
}

bool pop_contact(ContactUpdate& c) {
    if (!contact_queue) return false;
    return xQueueReceive(contact_queue, &c, 0) == pdTRUE;
}

bool pop_message(MessageIn& m) {
    if (!message_queue) return false;
    return xQueueReceive(message_queue, &m, 0) == pdTRUE;
}

bool pop_telemetry(TelemetryResponse& t) {
    if (!telemetry_queue) return false;
    return xQueueReceive(telemetry_queue, &t, 0) == pdTRUE;
}

bool pop_trace(TraceResponse& t) {
    if (!trace_queue) return false;
    return xQueueReceive(trace_queue, &t, 0) == pdTRUE;
}

bool pop_position(PositionUpdate& p) {
    if (!position_queue) return false;
    return xQueueReceive(position_queue, &p, 0) == pdTRUE;
}

bool take_discovery_changed() {
    bool changed = discovery_changed;
    discovery_changed = false;
    return changed;
}

MeshStatus get_status() {
    MeshStatus s = {};
    if (status_mutex && xSemaphoreTake(status_mutex, pdMS_TO_TICKS(10))) {
        s = status;
        xSemaphoreGive(status_mutex);
    }
    return s;
}

} // namespace mesh::bridge
