#pragma once

#include <MeshCore.h>
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/SensorManager.h>
#include <helpers/BaseSerialInterface.h>
#include <Arduino.h>

#ifdef PIN_BUZZER
  #include <helpers/ui/buzzer.h>
#endif

#include "NodePrefs.h"

enum class UIEventType {
    none,
    contactMessage,
    channelMessage,
    roomMessage,
    newContactMessage,
    ack
};

class AbstractUITask {
protected:
  mesh::MainBoard* _board;
  BaseSerialInterface* _serial;
  bool _connected;

  AbstractUITask(mesh::MainBoard* board, BaseSerialInterface* serial) : _board(board), _serial(serial) {
    _connected = false;
  }

public:
  void setHasConnection(bool connected) { _connected = connected; }
  bool hasConnection() const { return _connected; }
  uint16_t getBattMilliVolts() const { return _board->getBattMilliVolts(); }
  bool isSerialEnabled() const { return _serial->isEnabled(); }
  void enableSerial() { _serial->enable(); }
  void disableSerial() { _serial->disable(); }
  virtual void setBuzzerQuiet(bool quiet) { (void)quiet; }
  virtual void msgRead(int msgcount) = 0;
  // channel_idx: index of the group channel a message arrived on, or 0xFF for a
  // direct (1:1) message. Lets the UI attribute/filter messages per channel.
  virtual void newMsg(uint8_t path_len, const char* from_name, const char* text, int msgcount, uint8_t channel_idx = 0xFF) = 0;
  virtual void telemetryResponse(const uint8_t* pub_key_prefix, const uint8_t* data, uint8_t len) = 0;
  virtual void traceResponse(uint32_t tag, uint8_t hop_count, int8_t snr_there_q4, int8_t snr_back_q4) = 0;
  virtual void notify(UIEventType t = UIEventType::none) = 0;
  virtual void loop() = 0;
};
