#pragma once

#include <cmath>
#include <cstdlib>
#include <cstring>

namespace ui::geo {

static constexpr double DEG_TO_RAD = M_PI / 180.0;
static constexpr double R_EARTH_KM = 6371.0;
static constexpr double KM_PER_DEG_LAT = 111.32;

// Haversine distance in km
inline double distance_km(double lat1, double lon1, double lat2, double lon2) {
    double dLat = (lat2 - lat1) * DEG_TO_RAD;
    double dLon = (lon2 - lon1) * DEG_TO_RAD;
    double a = sin(dLat / 2) * sin(dLat / 2) +
               cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) *
               sin(dLon / 2) * sin(dLon / 2);
    return R_EARTH_KM * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

// Bearing from point 1 to point 2 in degrees (0=N, 90=E)
inline double bearing(double lat1, double lon1, double lat2, double lon2) {
    double dLon = (lon2 - lon1) * DEG_TO_RAD;
    double y = sin(dLon) * cos(lat2 * DEG_TO_RAD);
    double x = cos(lat1 * DEG_TO_RAD) * sin(lat2 * DEG_TO_RAD) -
               sin(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) * cos(dLon);
    double b = atan2(y, x) * (180.0 / M_PI);
    return fmod(b + 360.0, 360.0);
}

// Parse a shared-location string out of a message body. Recognises:
//   geo:LAT,LON              RFC-5870-ish — what the quick-reply "GPS location" sends
//   geo:LAT,LON;u=NN         extra geo: params are ignored
//   [WAY]LAT,LON Some label  MeshCore-Solo waypoint share (label optional)
// Returns true and fills lat/lon on success; if `label`/`label_n` are given, the
// trailing free-text label (if any) is copied in. Coordinates are range-checked.
inline bool parse_location(const char* text, double& lat, double& lon,
                           char* label = nullptr, size_t label_n = 0) {
    if (label && label_n) label[0] = 0;
    if (!text) return false;

    const char* p = text;
    if      (strncmp(p, "[WAY]", 5) == 0) p += 5;
    else if (strncmp(p, "geo:",  4) == 0) p += 4;
    else return false;

    char* end = nullptr;
    double la = strtod(p, &end);
    if (end == p) return false;
    while (*end == ' ') end++;
    if (*end != ',') return false;
    end++;

    char* end2 = nullptr;
    double lo = strtod(end, &end2);
    if (end2 == end) return false;
    if (la < -90.0 || la > 90.0 || lo < -180.0 || lo > 180.0) return false;

    lat = la;
    lon = lo;

    if (label && label_n) {
        const char* l = end2;
        while (*l == ' ' || *l == ';') l++;   // skip the separator / geo: params start
        size_t k = 0;
        for (; *l && *l != ';' && k < label_n - 1; l++) label[k++] = *l;
        label[k] = 0;
    }
    return true;
}

inline const char* bearing_to_cardinal(double bearing) {
    if (bearing >= 337.5 || bearing < 22.5)  return "N";
    if (bearing < 67.5)  return "NE";
    if (bearing < 112.5) return "E";
    if (bearing < 157.5) return "SE";
    if (bearing < 202.5) return "S";
    if (bearing < 247.5) return "SW";
    if (bearing < 292.5) return "W";
    return "NW";
}

} // namespace ui::geo
