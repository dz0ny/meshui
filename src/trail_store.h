#pragma once

#include <Arduino.h>
#include <algorithm>
#include <math.h>
#include <stdint.h>
#include <time.h>

// RAM-only GPS trail ring buffer.
// Storage cost: 1024 × 16 B = 16 KB. The trail survives auto-off (only the
// display blanks) but is lost on reboot — user explicitly snapshots to a
// LittleFS slot before powering down to keep it.
//
// When the ring fills we DON'T drop the oldest point (that would silently
// erase the start of a long track). Instead addPoint() coalesces: it halves
// the stored points (keeping both endpoints) and coarsens the sampling gate,
// so the whole journey stays represented, just at lower resolution.

struct TrailPoint {
  int32_t  lat_1e6;
  int32_t  lon_1e6;
  uint32_t ts;            // epoch seconds (RTC)
  uint8_t  flags;         // bit 0 = SEG_START (don't draw a line from the previous point)
};

static const uint8_t TRAIL_FLAG_SEG_START = 0x01;

class TrailStore {
public:
  static const int CAPACITY = 1024;

  // Fixed sampling cadence — matches the sensor manager's default GPS update
  // rate (1 s). Density is controlled by the min-delta gate (settings) rather
  // than by throttling the GPS poll. The NodePrefs::trail_interval_idx field
  // is retained as reserved for backwards compatibility but no longer used.
  static const uint16_t SAMPLING_SECS = 1;

  // Min-delta (metres) gates samples too close to the previous one.
  // Default 1 m (finest): full detail for short tracks. As the ring fills,
  // coalesce() widens the effective gate, so longer tracks naturally thin.
  // The index is a unit-agnostic "level" (0=finest … 3=coarsest); the actual
  // gate distance and its label follow the global metric/imperial preference.
  static const uint8_t MIN_DELTA_COUNT = 4;
  static uint16_t minDeltaMeters(uint8_t idx, bool imperial) {
    static const uint16_t MET[MIN_DELTA_COUNT] = { 1, 10, 25, 100 };
    static const uint16_t IMP[MIN_DELTA_COUNT] = { 1, 9, 23, 91 };  // ≈ 3/30/75/300 ft
    if (idx >= MIN_DELTA_COUNT) idx = 0;
    return imperial ? IMP[idx] : MET[idx];
  }
  static const char* minDeltaLabel(uint8_t idx, bool imperial) {
    static const char* MET[MIN_DELTA_COUNT] = { "1 m", "10 m", "25 m", "100 m" };
    static const char* IMP[MIN_DELTA_COUNT] = { "3 ft", "30 ft", "75 ft", "300 ft" };
    if (idx >= MIN_DELTA_COUNT) idx = 0;
    return imperial ? IMP[idx] : MET[idx];
  }

  // Speed / pace display units. UNITS_KMH / UNITS_MPH show speed; UNITS_PACE_KM
  // / UNITS_PACE_MI show time per distance ("pace"). Index 0 = km/h default.
  enum Units : uint8_t {
    UNITS_KMH     = 0,
    UNITS_MPH     = 1,
    UNITS_PACE_KM = 2,
    UNITS_PACE_MI = 3,
  };
  static const uint8_t UNITS_COUNT = 4;
  static const char* unitLabel(uint8_t idx) {
    static const char* L[UNITS_COUNT] = { "km/h", "mph", "min/km", "min/mi" };
    return L[idx < UNITS_COUNT ? idx : 0];
  }
  static bool unitIsPace(uint8_t idx) { return idx == UNITS_PACE_KM || idx == UNITS_PACE_MI; }

  bool isActive() const { return _active; }
  void setActive(bool a) {
    if (a && !_active) {
      // off → on: start a new session timer.
      _session_start_ms = millis();
    } else if (_active && !a) {
      // on → off: bank the elapsed of this session and arm a segment break
      // so the renderer doesn't draw a straight line through the dead time.
      if (_session_start_ms != 0) {
        _accumulated_ms   += millis() - _session_start_ms;
        _session_start_ms  = 0;
      }
      _pending_seg_break = true;
    }
    _active = a;
  }

  int  count() const { return _count; }
  bool empty() const { return _count == 0; }

  // i = 0 → oldest entry, i = count()-1 → newest.
  const TrailPoint& at(int i) const { return _buf[(_head + i) % CAPACITY]; }
  const TrailPoint& first() const   { return at(0); }
  const TrailPoint& last()  const   { return at(_count - 1); }

  void clear() {
    _head = 0; _count = 0;
    _pending_seg_break = false;
    _accumulated_ms    = 0;
    _session_start_ms  = 0;
    _coalesce_shift    = 0;
  }

  // Returns true if the point was stored (passed the min-delta gate).
  // First point of the ring and the first point after a stop/start cycle
  // get flagged TRAIL_FLAG_SEG_START so the map renderer breaks the line.
  bool addPoint(int32_t lat_1e6, int32_t lon_1e6, uint32_t ts, uint16_t min_delta_m) {
    // The gate widens as we coalesce, so freshly-added points stay roughly as
    // dense as the already-thinned history instead of refilling immediately.
    uint32_t gate = (uint32_t)min_delta_m << _coalesce_shift;
    if (_count > 0 && !_pending_seg_break) {
      float d = haversineMeters(last().lat_1e6, last().lon_1e6, lat_1e6, lon_1e6);
      if (d < (float)gate) return false;
    }
    uint8_t flags = (_count == 0 || _pending_seg_break) ? TRAIL_FLAG_SEG_START : 0;
    _pending_seg_break = false;

    if (_count == CAPACITY) coalesce();  // full → thin in place, never truncate the start
    int pos = (_head + _count) % CAPACITY;
    _count++;
    _buf[pos].lat_1e6 = lat_1e6;
    _buf[pos].lon_1e6 = lon_1e6;
    _buf[pos].ts      = ts;
    _buf[pos].flags   = flags;
    return true;
  }

  // Sum of pairwise Haversine deltas across the whole ring, skipping segment
  // boundaries (a SEG_START point isn't reached from its predecessor).
  uint32_t totalDistanceMeters() const {
    float d = 0;
    for (int i = 1; i < _count; i++) {
      if (at(i).flags & TRAIL_FLAG_SEG_START) continue;
      d += haversineMeters(at(i - 1).lat_1e6, at(i - 1).lon_1e6,
                            at(i).lat_1e6,     at(i).lon_1e6);
    }
    return (uint32_t)d;
  }

  // Cumulative active tracking time across all start→stop sessions, in
  // seconds. Counts ticks while the trail is on, freezes while it's off.
  // millis()-based so it doesn't depend on RTC sync.
  uint32_t elapsedSeconds() const {
    uint32_t ms = _accumulated_ms;
    if (_active && _session_start_ms != 0) ms += millis() - _session_start_ms;
    return ms / 1000;
  }

  // Average speed in km/h = total distance / cumulative active time.
  uint16_t avgSpeedKmh() const {
    uint32_t es = elapsedSeconds();
    if (es == 0) return 0;
    return (uint16_t)((float)totalDistanceMeters() / (float)es * 3.6f);
  }

  // Compute bounding box across all points. Returns false if empty.
  bool boundingBox(int32_t& min_lat, int32_t& min_lon,
                   int32_t& max_lat, int32_t& max_lon) const {
    if (_count == 0) return false;
    min_lat = max_lat = first().lat_1e6;
    min_lon = max_lon = first().lon_1e6;
    for (int i = 1; i < _count; i++) {
      const auto& p = at(i);
      if (p.lat_1e6 < min_lat) min_lat = p.lat_1e6;
      if (p.lat_1e6 > max_lat) max_lat = p.lat_1e6;
      if (p.lon_1e6 < min_lon) min_lon = p.lon_1e6;
      if (p.lon_1e6 > max_lon) max_lon = p.lon_1e6;
    }
    return true;
  }

  // Persistent snapshot — single slot at the given filesystem path.
  // Layout: 4-byte magic "TRAL", uint8 version, uint8 reserved, uint16 count,
  // uint32 accumulated_ms, then `count` raw TrailPoint records. count is
  // clamped to CAPACITY on load.
  static const uint32_t SAVE_MAGIC = 0x4C415254;  // "TRAL"
  static const uint8_t  SAVE_VERSION = 1;

  // Caller supplies an opened, writable File (the FS-open call is
  // platform-specific). Returns true if the header and every point wrote
  // cleanly. The file is left open for the caller to close.
  template <typename F>
  bool writeTo(F& file) {
    uint32_t magic = SAVE_MAGIC;
    uint8_t  ver = SAVE_VERSION;
    uint8_t  res = 0;
    uint16_t cnt = (uint16_t)_count;
    uint32_t accum = currentAccumulatedMs();
    if (file.write((uint8_t*)&magic, sizeof(magic)) != sizeof(magic)) return false;
    if (file.write(&ver, 1)                          != 1)             return false;
    if (file.write(&res, 1)                          != 1)             return false;
    if (file.write((uint8_t*)&cnt,   sizeof(cnt))    != sizeof(cnt))   return false;
    if (file.write((uint8_t*)&accum, sizeof(accum))  != sizeof(accum)) return false;
    for (int i = 0; i < _count; i++) {
      if (file.write((uint8_t*)&at(i), sizeof(TrailPoint)) != sizeof(TrailPoint)) return false;
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
    uint32_t accum = 0;
    file.read(&ver, 1);
    file.read(&res, 1);
    file.read((uint8_t*)&cnt, sizeof(cnt));
    file.read((uint8_t*)&accum, sizeof(accum));
    if (ver != SAVE_VERSION || cnt > CAPACITY) return false;
    if (_active) {
      _active = false;
      _session_start_ms = 0;
    }
    _head = 0;
    _count = 0;
    for (int i = 0; i < cnt; i++) {
      TrailPoint p;
      int n = file.read((uint8_t*)&p, sizeof(TrailPoint));
      if (n != (int)sizeof(TrailPoint)) break;
      _buf[_count++] = p;
    }
    _accumulated_ms = accum;
    _pending_seg_break = true;
    _coalesce_shift = 0;
    return true;
  }

  // GPX writers — shared helpers so we can dump from RAM and from flash
  // through the same formatting code.

  template <typename S>
  static size_t gpxHeader(S& out) {
    size_t n = 0;
    n += out.print(F("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"));
    n += out.print(F("<gpx version=\"1.1\" creator=\"MeshCore\" "
                      "xmlns=\"http://www.topografix.com/GPX/1/1\">\n"));
    return n;
  }

  template <typename S>
  static size_t gpxTrackOpen(S& out, const char* name) {
    size_t n = 0;
    n += out.print(F("<trk><name>"));
    n += out.print(name);
    n += out.print(F("</name>\n"));
    return n;
  }

  // Emit saved waypoints as <wpt> elements. In GPX 1.1 these must precede the
  // <trk>. Duck-typed over any store exposing count()/at(i) whose entries have
  // lat_1e6 / lon_1e6 / ts / label, so Trail.h stays decoupled from Waypoint.h.
  template <typename S, typename WP>
  static size_t gpxWaypoints(S& out, WP& store) {
    size_t n = 0;
    for (int i = 0; i < store.count(); i++) {
      const auto& w = store.at(i);
      // XML-escape the user label (&, <, > only).
      char esc[64]; int e = 0;
      for (const char* p = w.label; *p && e < (int)sizeof(esc) - 6; p++) {
        if      (*p == '&') { memcpy(esc + e, "&amp;", 5); e += 5; }
        else if (*p == '<') { memcpy(esc + e, "&lt;",  4); e += 4; }
        else if (*p == '>') { memcpy(esc + e, "&gt;",  4); e += 4; }
        else                  esc[e++] = *p;
      }
      esc[e] = '\0';
      char buf[160];
      int len = snprintf(buf, sizeof(buf),
        "<wpt lat=\"%.6f\" lon=\"%.6f\"><name>%s</name>",
        w.lat_1e6 / 1.0e6, w.lon_1e6 / 1.0e6, esc);
      if (len > 0) {
        if ((size_t)len > sizeof(buf)) len = sizeof(buf);
        n += out.write((const uint8_t*)buf, (size_t)len);
      }
      if (w.ts > 1000000000UL) {                 // append <time> when the RTC was set
        time_t t = (time_t)w.ts;
        struct tm* gt = ::gmtime(&t);
        if (gt) {
          len = snprintf(buf, sizeof(buf),
            "<time>%04d-%02d-%02dT%02d:%02d:%02dZ</time>",
            gt->tm_year + 1900, gt->tm_mon + 1, gt->tm_mday,
            gt->tm_hour, gt->tm_min, gt->tm_sec);
          if (len > 0) { if ((size_t)len > sizeof(buf)) len = sizeof(buf);
                         n += out.write((const uint8_t*)buf, (size_t)len); }
        }
      }
      n += out.print(F("</wpt>\n"));
    }
    return n;
  }

  template <typename S>
  static size_t gpxFooter(S& out, bool in_segment) {
    size_t n = 0;
    if (in_segment) n += out.print(F("</trkseg>\n"));
    n += out.print(F("</trk></gpx>\n"));
    return n;
  }

  // Emit a single <trkpt>; opens a <trkseg> on a segment boundary. Updates
  // `in_segment` to track open/close pairing.
  template <typename S>
  static size_t gpxPoint(S& out, const TrailPoint& p, bool first, bool& in_segment) {
    size_t n = 0;
    bool seg_start = first || (p.flags & TRAIL_FLAG_SEG_START);
    if (seg_start) {
      if (in_segment) n += out.print(F("</trkseg>\n"));
      n += out.print(F("<trkseg>\n"));
      in_segment = true;
    }
    char buf[120];
    time_t t = (time_t)p.ts;
    struct tm* gt = ::gmtime(&t);
    if (!gt) return n;  // defensive: skip malformed timestamps
    int len = snprintf(buf, sizeof(buf),
      "<trkpt lat=\"%.6f\" lon=\"%.6f\"><time>%04d-%02d-%02dT%02d:%02d:%02dZ</time></trkpt>\n",
      p.lat_1e6 / 1.0e6, p.lon_1e6 / 1.0e6,
      gt->tm_year + 1900, gt->tm_mon + 1, gt->tm_mday,
      gt->tm_hour, gt->tm_min, gt->tm_sec);
    if (len < 0) return n;
    if ((size_t)len > sizeof(buf)) len = sizeof(buf);  // snprintf returns intended size
    n += out.write((const uint8_t*)buf, (size_t)len);
    return n;
  }

  // Dump the live RAM ring as GPX (with saved waypoints). Returns bytes written.
  template <typename S, typename WP>
  size_t exportGpx(S& out, WP& wpts, const char* trk_name = "MeshCore Trail") {
    size_t total = gpxHeader(out);
    total += gpxWaypoints(out, wpts);
    total += gpxTrackOpen(out, trk_name);
    bool in_segment = false;
    for (int i = 0; i < _count; i++) {
      total += gpxPoint(out, at(i), i == 0, in_segment);
    }
    total += gpxFooter(out, in_segment);
    return total;
  }

  // Stream a saved trail straight from the open file as GPX without
  // touching the live RAM ring. Returns 0 on format mismatch.
  template <typename F, typename S, typename WP>
  static size_t exportGpxFromFile(F& file, S& out, WP& wpts, const char* trk_name = "MeshCore Trail") {
    uint32_t magic = 0;
    if (file.read((uint8_t*)&magic, sizeof(magic)) != (int)sizeof(magic)) return 0;
    if (magic != SAVE_MAGIC) return 0;
    uint8_t  ver = 0, res = 0;
    uint16_t cnt = 0;
    uint32_t accum = 0;
    file.read(&ver, 1);
    file.read(&res, 1);
    file.read((uint8_t*)&cnt, sizeof(cnt));
    file.read((uint8_t*)&accum, sizeof(accum));
    if (ver != SAVE_VERSION || cnt > CAPACITY) return 0;

    size_t total = gpxHeader(out);
    total += gpxWaypoints(out, wpts);
    total += gpxTrackOpen(out, trk_name);
    bool in_segment = false;
    for (uint16_t i = 0; i < cnt; i++) {
      TrailPoint p;
      int n = file.read((uint8_t*)&p, sizeof(TrailPoint));
      if (n != (int)sizeof(TrailPoint)) break;
      total += gpxPoint(out, p, i == 0, in_segment);
    }
    total += gpxFooter(out, in_segment);
    return total;
  }

  uint32_t currentAccumulatedMs() const {
    uint32_t ms = _accumulated_ms;
    if (_active && _session_start_ms != 0) ms += millis() - _session_start_ms;
    return ms;
  }

  // Approximate great-circle distance in metres (Haversine).
  static float haversineMeters(int32_t la1, int32_t lo1, int32_t la2, int32_t lo2) {
    const float R   = 6371000.0f;
    const float D2R = (float)M_PI / 180.0f;
    float lat1 = (la1 / 1.0e6f) * D2R;
    float lat2 = (la2 / 1.0e6f) * D2R;
    float dlat = ((la2 - la1) / 1.0e6f) * D2R;
    float dlon = ((lo2 - lo1) / 1.0e6f) * D2R;
    float sdl  = sinf(dlat * 0.5f);
    float sdo  = sinf(dlon * 0.5f);
    float a    = sdl * sdl + cosf(lat1) * cosf(lat2) * sdo * sdo;
    float c    = 2.0f * atan2f(sqrtf(a), sqrtf(1.0f - a));
    return R * c;
  }

private:
  // Buffer is full — halve the point count instead of dropping the oldest.
  // Normalize the ring so logical order == physical order, then keep every
  // *odd* logical index: that preserves the oldest point (index 1 becomes the
  // new first) AND the newest (the last index is odd for an even CAPACITY).
  // A dropped point hands its SEG_START flag forward to the survivor that
  // inherits the gap, so segment breaks survive the thinning. Finally bump the
  // coalesce shift so addPoint()'s gate doubles and density stays consistent.
  void coalesce() {
    if (_count <= 0) return;
    std::rotate(_buf, _buf + _head, _buf + CAPACITY);
    _head = 0;
    int w = 0;
    uint8_t carry = 0;
    for (int r = 0; r < _count; r++) {
      if (r & 1) {
        TrailPoint p = _buf[r];
        p.flags = (uint8_t)(p.flags | carry);
        carry = 0;
        _buf[w++] = p;
      } else {
        carry = (uint8_t)(carry | (_buf[r].flags & TRAIL_FLAG_SEG_START));
      }
    }
    _count = w;
    if (_coalesce_shift < 8) _coalesce_shift++;  // cap: keeps min_delta<<shift inside uint32
  }

  TrailPoint _buf[CAPACITY];
  int      _head             = 0;
  int      _count            = 0;
  bool     _active           = false;
  bool     _pending_seg_break = false;  // next addPoint flags itself SEG_START
  uint8_t  _coalesce_shift   = 0;       // octaves the min-delta gate has widened
  uint32_t _accumulated_ms   = 0;       // banked active time across previous sessions
  uint32_t _session_start_ms = 0;       // millis() of the current active session, 0 if none
};
