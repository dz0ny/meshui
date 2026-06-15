#pragma once

#include "AbstractUITask.h"
#include "../mesh_bridge.h"
// UITask implementation that routes companion radio events to our bridge queues.
// This replaces the display-based UITask from companion_radio.
class BridgeUITask : public AbstractUITask {
#ifdef PIN_BUZZER
    genericBuzzer buzzer;
#endif
public:
    BridgeUITask(mesh::MainBoard* board, BaseSerialInterface* serial)
        : AbstractUITask(board, serial) {}

    // Bring the buzzer up and play the startup chirp. Call once at boot after
    // prefs are loaded, passing the persisted quiet flag (NodePrefs.buzzer_quiet).
    // No-op on boards without a buzzer.
    void begin(bool quiet) {
#ifdef PIN_BUZZER
        buzzer.begin();
        buzzer.quiet(quiet);
        buzzer.startup();
#else
        (void)quiet;
#endif
    }

    void setBuzzerQuiet(bool quiet) override {
#ifdef PIN_BUZZER
        buzzer.quiet(quiet);
#else
        (void)quiet;
#endif
    }

    bool isBuzzerQuiet() const {
#ifdef PIN_BUZZER
        return const_cast<genericBuzzer&>(buzzer).isQuiet();
#else
        return true;
#endif
    }

    void msgRead(int msgcount) override {
        // UI acknowledged messages — nothing to do
    }

    void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount, uint8_t channel_idx = 0xFF) override {
        mesh::bridge::MessageIn mi = {};
        if (from_name) strncpy(mi.sender_name, from_name, sizeof(mi.sender_name) - 1);
        if (text) strncpy(mi.text, text, sizeof(mi.text) - 1);
        mi.is_direct = (path_len == 0xFF);
        mi.is_channel = (channel_idx != 0xFF);  // group channel vs direct message
        mi.channel_idx = channel_idx;
        mi.timestamp = 0;
        mesh::bridge::push_message(mi);

        Serial.printf("BRIDGE: msg from '%s': %s\n", from_name ? from_name : "?", text ? text : "?");
    }

    void telemetryResponse(const uint8_t* pub_key_prefix, const uint8_t* data, uint8_t len) override {
        if (!pub_key_prefix || !data || len == 0) return;

        mesh::bridge::TelemetryResponse tr = {};
        memcpy(tr.pub_key_prefix, pub_key_prefix, sizeof(tr.pub_key_prefix));
        tr.len = len > sizeof(tr.data) ? sizeof(tr.data) : len;
        memcpy(tr.data, data, tr.len);
        mesh::bridge::push_telemetry(tr);
    }

    void traceResponse(uint32_t tag, uint8_t hop_count, int8_t snr_there_q4, int8_t snr_back_q4) override {
        mesh::bridge::TraceResponse tr = {};
        tr.tag = tag;
        tr.hop_count = hop_count;
        tr.snr_there_q4 = snr_there_q4;
        tr.snr_back_q4 = snr_back_q4;
        mesh::bridge::push_trace(tr);
    }

    void notify(UIEventType t) override {
        if (t == UIEventType::newContactMessage) {
            mesh::bridge::mark_discovery_changed();
        }
#ifdef PIN_BUZZER
        // Short RTTTL chirps per event (same tunes as the reference companion
        // firmware). play() is a no-op while quiet, and interrupts any chirp
        // still ringing. No melody editor — these are fixed.
        switch (t) {
            case UIEventType::contactMessage:
                buzzer.play("MsgRcv3:d=4,o=6,b=200:32e,32g,32b,16c7");
                break;
            case UIEventType::channelMessage:
                buzzer.play("kerplop:d=16,o=6,b=120:32g#,32c#");
                break;
            case UIEventType::ack:
                buzzer.play("ack:d=32,o=8,b=120:c");
                break;
            default:
                break;
        }
#endif
    }

    void loop() override {
#ifdef PIN_BUZZER
        // Non-blocking RTTTL playback must be pumped continuously.
        if (buzzer.isPlaying()) buzzer.loop();
#endif
    }
};
