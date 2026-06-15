#pragma once

// Screen IDs — shared by the LVGL (ESP32) and mono (nRF52) backends. Kept free
// of any LVGL/board dependency so screen .cpp files can include just this when
// all they need from ui_theme.h is the SCREEN_* ids.

enum screen_id {
    SCREEN_HOME     = 0,
    SCREEN_CONTACTS = 1,
    SCREEN_CHAT     = 2,
    SCREEN_SETTINGS = 3,
    SCREEN_GPS      = 4,
    SCREEN_BATTERY  = 5,
    SCREEN_MESH     = 6,
    SCREEN_STATUS   = 7,
    SCREEN_SET_DISPLAY = 8,
    SCREEN_SET_GPS     = 9,
    SCREEN_SET_MESH    = 10,
    SCREEN_DISCOVERY   = 11,
    SCREEN_LOCK        = 12,
    SCREEN_CONTACT_DETAIL = 13,
    SCREEN_MSG_DETAIL     = 14,
    SCREEN_SET_BLE        = 15,
    SCREEN_SET_STORAGE    = 16,
    SCREEN_COMPOSE        = 17,
    SCREEN_MAP            = 18,
    SCREEN_SENSORS        = 19,
    SCREEN_PING           = 20,
    SCREEN_SETTINGS_PREFERENCES = 21,
    SCREEN_SETTINGS_DEBUG       = 22,
    SCREEN_SETTINGS_DEVICE      = 23,
    SCREEN_TOUCH_DEBUG          = 24,
    SCREEN_TRAIL                = 25,
    SCREEN_QUICKREPLY           = 26,
    SCREEN_COMPASS              = 27,
    SCREEN_PRIVACY              = 28,
    SCREEN_TEAM                 = 29,
};
