#pragma once
#include <stdint.h>

// Tiny header-only i18n for meshui. Header-only (inline functions + function-
// local static tables) so it can be #included from screens shared by every
// backend without adding a .cpp to each PlatformIO env's build_src_filter.
//
// The active language lives in a single process-global (lang_ref). Persistence
// is the caller's job: read the stored byte at boot and call set_lang(); write
// it back when the user changes it (main_wio.cpp does this via InternalFS).
//
// IMPORTANT: the mono e-ink backend draws with Adafruit-GFX's built-in 5x7
// font, which is ASCII-only. Slovenian translations are therefore written
// WITHOUT the diacritics c/s/z-with-caron (they would render as tofu) — e.g.
// "Sporocila", "Pocisti". This is the standard "ASCII Slovenian" fallback.

namespace i18n {

enum Lang : uint8_t { EN = 0, SL = 1, LANG_COUNT };

enum Str : uint16_t {
    // home menu
    T_MESSAGES, T_TRAIL, T_STATUS, T_SETTINGS, T_TEAM,
    // status menu
    T_BATTERY, T_GPS_INFO, T_MESH_INFO,
    // settings menu
    T_DISPLAY, T_BLUETOOTH, T_GPS_SETTINGS, T_MESH_SETTINGS, T_LANGUAGE,
    T_PRIVACY, T_ADVERT_GPS, T_SOUND, T_BUZZER, T_INVERT,
    // common values
    T_ON, T_OFF, T_OK, T_NONE, T_AUTO,
    // set_gps
    T_GPS, T_MODULE, T_RTC_SYNC,
    // set_mesh / mesh_info
    T_NODE, T_TX_POWER, T_GPS_SHARE, T_REPEAT, T_RADIO, T_STATS, T_PEERS,
    // gps info
    T_GPS_STATUS, T_LATITUDE, T_LONGITUDE, T_SATELLITES, T_ALTITUDE, T_SPEED,
    // battery
    T_CHARGE, T_VOLTAGE, T_CURRENT,
    // trail
    T_START, T_STOP, T_CLEAR, T_NO_GPS_TRAIL, T_STOPPED, T_WAITING_FIX,
    T_TRACKING, T_TRACKING_STARTED, T_WAITING_GPS_FIX, T_TRACKING_STOPPED,
    T_TRAIL_CLEARED,
    // chat / channels
    T_NO_MESSAGES, T_MSG_CHANNEL, T_GPS_CHANNEL, T_UNREAD,
    // quick replies
    T_REPLY, T_SENT, T_SEND_FAILED, T_NO_GPS_FIX, T_QR_GPS_LOC,
    T_QR_OMW, T_QR_YES, T_QR_NO, T_QR_HELP, T_QR_ARRIVED,
    // map / compass
    T_MAP, T_NO_LOCATIONS, T_MOVE_TO_CAL,
    // time
    T_TIMEZONE,
    // provision (settings sync)
    T_PROVISION, T_SHARE_PROFILE, T_RECEIVE_PROFILE,
    T_PROV_WAIT, T_PROV_SEARCH, T_PROV_TRANSFER, T_PROV_DONE, T_PROV_FAILED, T_PROV_SELECT,
    // self-advert (announce this node on the mesh)
    T_ADVERT_ZEROHOP, T_ADVERT_FLOOD, T_ADVERT_SENT,
    // language names (always shown in their own language)
    T_LANG_EN, T_LANG_SL,
    T_COUNT
};

inline Lang& lang_ref() { static Lang l = EN; return l; }
inline Lang  get_lang() { return lang_ref(); }
inline void  set_lang(Lang l) { if (l < LANG_COUNT) lang_ref() = l; }
inline void  set_lang(uint8_t l) { set_lang(l < LANG_COUNT ? (Lang)l : EN); }

inline const char* t(Str id) {
    static const char* const EN_T[T_COUNT] = {
        "Messages", "Trail", "Status", "Settings", "Team",
        "Battery", "GPS Info", "Mesh Info",
        "Display", "Bluetooth", "GPS Settings", "Mesh Settings", "Language",
        "Privacy", "GPS in advert", "Sound", "Buzzer", "Invert",
        "On", "Off", "OK", "None", "Auto",
        "GPS", "Module", "RTC Sync",
        "Node", "TX Power", "GPS Share", "Repeat", "Radio", "Stats", "Peers",
        "Status", "Latitude", "Longitude", "Satellites", "Altitude", "Speed",
        "Charge", "Voltage", "Current",
        "Start", "Stop", "Clear", "No GPS / no trail", "Stopped", "Waiting fix",
        "Tracking", "Tracking started", "Waiting for GPS fix", "Tracking stopped",
        "Trail cleared",
        "No messages yet", "Channel", "GPS Chan", "unread",
        "Reply", "Sent", "Send failed", "No GPS fix", "GPS location",
        "On my way", "Yes", "No", "Need help", "Arrived",
        "Map", "No positions yet", "Move to calibrate",
        "Time zone",
        "Provision", "Share Profile", "Receive Profile",
        "Waiting for receiver", "Searching...", "Transferring", "Done", "Failed", "Select device",
        "Advert (direct)", "Advert (flood)", "Advert sent",
        "English", "Slovensko",
    };
    static const char* const SL_T[T_COUNT] = {
        "Sporocila", "Sled", "Stanje", "Nastavitve", "Ekipa",
        "Baterija", "GPS info", "Mesh info",
        "Zaslon", "Bluetooth", "GPS nastav.", "Mesh nastav.", "Jezik",
        "Zasebnost", "GPS v oglasu", "Zvok", "Brencalo", "Obrni barve",
        "Da", "Ne", "OK", "Brez", "Samod.",
        "GPS", "Modul", "RTC sinhr.",
        "Vozlisce", "Moc TX", "Deli GPS", "Ponovi", "Radio", "Statistika", "Sosedje",
        "Stanje", "S. sirina", "Z. dolzina", "Sateliti", "Visina", "Hitrost",
        "Polnost", "Napetost", "Tok",
        "Zacni", "Ustavi", "Pocisti", "Ni GPS / sledi", "Ustavljeno", "Cakam fix",
        "Sledim", "Sledenje vklop.", "Cakam GPS fix", "Sledenje ustavljeno",
        "Sled pociscena",
        "Ni sporocil", "Kanal", "GPS kanal", "neprebranih",
        "Odgovori", "Poslano", "Posiljanje ni uspelo", "Ni GPS fix", "GPS lokacija",
        "Ze grem", "Da", "Ne", "Rabim pomoc", "Prispel",
        "Zemljevid", "Ni lokacij", "Premakni za kalib.",
        "Casovni pas",
        "Prenos nastav.", "Deli profil", "Prejmi profil",
        "Cakam prejemnika", "Iscem...", "Prenasam", "Koncano", "Napaka", "Izberi napravo",
        "Oglas (direkt)", "Oglas (poplava)", "Oglas poslan",
        "English", "Slovensko",
    };
    if (id >= T_COUNT) return "";
    return (lang_ref() == SL) ? SL_T[id] : EN_T[id];
}

} // namespace i18n
