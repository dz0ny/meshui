#pragma once

#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include <stdint.h>

// User-marked GPS waypoints — a small, fixed RAM store (parity with the trail
// ring: survives auto-off, lost on reboot until FS snapshotting is wired).
//
// Entries deliberately expose lat_1e6 / lon_1e6 / ts / label so the store
// duck-types into TrailStore::gpxWaypoints() for GPX export with no coupling
// between the two headers.

struct Waypoint {
  int32_t  lat_1e6;
  int32_t  lon_1e6;
  uint32_t ts;            // epoch seconds (RTC), 0 if the clock wasn't set
  char     label[24];
};

class WaypointStore {
public:
  static const int CAPACITY = 16;

  int  count() const { return _count; }
  bool empty() const { return _count == 0; }
  bool full()  const { return _count >= CAPACITY; }
  const Waypoint& at(int i) const { return _wp[i]; }

  // Append a waypoint. A blank label is auto-named "WP N". Returns false when
  // the store is full.
  bool add(int32_t lat_1e6, int32_t lon_1e6, uint32_t ts, const char* label) {
    if (_count >= CAPACITY) return false;
    Waypoint& w = _wp[_count++];
    w.lat_1e6 = lat_1e6;
    w.lon_1e6 = lon_1e6;
    w.ts      = ts;
    if (label && label[0]) {
      strncpy(w.label, label, sizeof(w.label) - 1);
      w.label[sizeof(w.label) - 1] = 0;
    } else {
      snprintf(w.label, sizeof(w.label), "WP %d", _count);
    }
    return true;
  }

  void remove(int i) {
    if (i < 0 || i >= _count) return;
    for (int k = i; k < _count - 1; k++) _wp[k] = _wp[k + 1];
    _count--;
  }

  void clear() { _count = 0; }

  void rename(int i, const char* label) {
    if (i < 0 || i >= _count || !label) return;
    strncpy(_wp[i].label, label, sizeof(_wp[i].label) - 1);
    _wp[i].label[sizeof(_wp[i].label) - 1] = 0;
  }

  // Persistent snapshot — single slot, same shape as TrailStore::writeTo/readFrom
  // so FS wiring is a drop-in later. Caller owns the opened File.
  static const uint32_t SAVE_MAGIC   = 0x59415057;  // "WPAY"
  static const uint8_t  SAVE_VERSION = 1;

  template <typename F>
  bool writeTo(F& file) {
    uint32_t magic = SAVE_MAGIC;
    uint8_t  ver = SAVE_VERSION, res = 0;
    uint16_t cnt = (uint16_t)_count;
    if (file.write((uint8_t*)&magic, sizeof(magic)) != sizeof(magic)) return false;
    if (file.write(&ver, 1) != 1) return false;
    if (file.write(&res, 1) != 1) return false;
    if (file.write((uint8_t*)&cnt, sizeof(cnt)) != sizeof(cnt)) return false;
    for (int i = 0; i < _count; i++) {
      if (file.write((uint8_t*)&_wp[i], sizeof(Waypoint)) != sizeof(Waypoint)) return false;
    }
    return true;
  }

  template <typename F>
  bool readFrom(F& file) {
    uint32_t magic = 0;
    if (file.read((uint8_t*)&magic, sizeof(magic)) != (int)sizeof(magic)) return false;
    if (magic != SAVE_MAGIC) return false;
    uint8_t  ver = 0, res = 0;
    uint16_t cnt = 0;
    file.read(&ver, 1);
    file.read(&res, 1);
    file.read((uint8_t*)&cnt, sizeof(cnt));
    if (ver != SAVE_VERSION || cnt > CAPACITY) return false;
    _count = 0;
    for (int i = 0; i < cnt; i++) {
      Waypoint w;
      if (file.read((uint8_t*)&w, sizeof(Waypoint)) != (int)sizeof(Waypoint)) break;
      _wp[_count++] = w;
    }
    return true;
  }

private:
  Waypoint _wp[CAPACITY];
  int      _count = 0;
};
