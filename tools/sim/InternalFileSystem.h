#pragma once
// Host no-op stand-in for the nRF52 Adafruit_LittleFS InternalFS, so settings
// screens that persist prefs (set_display) compile and run under the sim.
// Writes go nowhere — the sim only renders, it never persists.
#include <cstdint>
#include <cstddef>

namespace Adafruit_LittleFS_Namespace {

enum { FILE_O_READ = 0, FILE_O_WRITE = 1 };

class File {
public:
    explicit operator bool() const { return false; }   // sim: never "opens"
    size_t write(const uint8_t*, size_t n) { return n; }
    int read(void*, size_t) { return 0; }
    void close() {}
};

class _FS {
public:
    bool remove(const char*) { return true; }
    File open(const char*, int) { return File(); }
    bool begin() { return true; }
};

} // namespace Adafruit_LittleFS_Namespace

// Callers reach FILE_O_WRITE via `using namespace Adafruit_LittleFS_Namespace`.
static Adafruit_LittleFS_Namespace::_FS InternalFS;
