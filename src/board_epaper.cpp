#ifdef BOARD_EPAPER

#include "board.h"
#include "model.h"
#include "nvs_param.h"
#include "mesh/companion/target.h"

namespace board {

SemaphoreHandle_t i2c_mutex = NULL;

// ---------- Global peripheral instances ----------

bool peri_status[E_PERI_MAX] = {0};
XPowersPPM ppm;
BQ27220 bq27220;
TouchDrvGT911 touch;
SensorPCF8563 rtc;
static constexpr uint8_t CARDKB_I2C_ADDR = 0x5F;
static constexpr uint8_t CARDKB_KEY_UP = 0xB5;
static constexpr uint8_t CARDKB_KEY_DOWN = 0xB6;
static constexpr uint8_t CARDKB_KEY_LEFT = 0xB4;
static constexpr uint8_t CARDKB_KEY_RIGHT = 0xB7;
static constexpr uint8_t CARDKB_KEY_TAB = 0x09;
static constexpr uint8_t CARDKB_KEY_ESC = 0x1B;
static constexpr uint8_t CARDKB_KEY_BS = 0x08;
static constexpr uint8_t CARDKB_KEY_ENTER = 0x0D;
static constexpr uint8_t CARDKB_KEY_DEL = 0x7F;
static constexpr uint8_t CARDKB_KEY_PREV = 0xF2;
static constexpr uint8_t CARDKB_KEY_NEXT = 0xF1;
static constexpr uint8_t CARDKB_KEY_LEFT_NAV = 0xF3;
static constexpr uint8_t CARDKB_KEY_RIGHT_NAV = 0xF4;
static uint32_t cardkb_last_poll = 0;
static uint32_t cardkb_poll_interval = 50;
static uint8_t cardkb_error_count = 0;

// E-paper
#define WAVEFORM EPD_BUILTIN_WAVEFORM
#define DEMO_BOARD epd_board_v7
EpdiyHighlevelState hl;

// Home button
volatile bool home_button_pressed = false;

// GPS is now handled by MeshCore's EnvironmentSensorManager + MicroNMEALocationProvider
// (configured in target.cpp, polled by sensors.loop() in mesh_task.cpp)

// ---------- Detail init functions ----------

namespace detail {

bool screen_init() {
    epd_init(&DEMO_BOARD, &ED047TC1, EPD_LUT_64K);
    epd_set_vcom(nvs_param_get_u16(NVS_ID_EPD_VCOM));
    hl = epd_hl_init(WAVEFORM);
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);

    Serial.printf("Display: %d x %d\n", epd_rotated_display_width(), epd_rotated_display_height());

    epd_set_lcd_pixel_clock_MHz(17);
    epd_poweron();
    epd_clear();
    epd_poweroff();
    return true;
}

bool bq25896_init() {
    bool result = ppm.init(Wire, BOARD_SDA, BOARD_SCL, BQ25896_SLAVE_ADDRESS);
    if (!result) return false;

    ppm.setSysPowerDownVoltage(3300);
    ppm.setInputCurrentLimit(3250);
    ppm.disableCurrentLimitPin();
    ppm.setChargeTargetVoltage(4208);
    ppm.setPrechargeCurr(64);
    ppm.setChargerConstantCurr(1024);
    ppm.enableMeasure();
    ppm.enableCharge();
    ppm.enableOTG();
    ppm.disableOTG();
    return true;
}

bool bq27220_init() {
    return bq27220.init();
}

bool rtc_init() {
    pinMode(BOARD_RTC_IRQ, INPUT_PULLUP);
    if (!rtc.begin(Wire, PCF8563_SLAVE_ADDRESS, BOARD_RTC_SDA, BOARD_RTC_SCL)) {
        Serial.println("Failed to find PCF8563");
        return false;
    }
    return true;
}

bool touch_init() {
    touch.setPins(BOARD_TOUCH_RST, BOARD_TOUCH_INT);
    if (!touch.begin(Wire, GT911_SLAVE_ADDRESS_L, BOARD_SDA, BOARD_SCL)) {
        Serial.println("Failed to find GT911");
        return false;
    }
    Serial.println("GT911 touch init OK");

    // Home button (center touch button on GT911) — sets flag for UI task
    touch.setHomeButtonCallback([](void* user_data) {
        Serial.println("Home button pressed");
        home_button_pressed = true;
    }, NULL);

    touch.setInterruptMode(TouchDrvGT911::LOW_LEVEL_QUERY);
    return true;
}

bool sd_init() {
    if (!SD.begin(BOARD_SD_CS)) {
        Serial.println("SD card mount failed");
        return false;
    }
    if (SD.cardType() == CARD_NONE) {
        Serial.println("No SD card");
        return false;
    }
    return true;
}

bool gps_init() {
    // GPS is now initialized by MeshCore's EnvironmentSensorManager (sensors.begin())
    // via MicroNMEALocationProvider which uses Serial1 with PIN_GPS_TX/RX
    // Just return true — GPS detection happens in sensors.begin()
    return true;
}

bool keyboard_init() {
    Wire.beginTransmission(CARDKB_I2C_ADDR);
    bool ok = (Wire.endTransmission() == 0);
    if (ok) {
        Serial.println("CardKB keyboard found");
    } else {
        Serial.println("CardKB keyboard not found");
    }
    return ok;
}

} // namespace detail

void seed_clock_from_rtc() {
    if (!peri_status[E_PERI_RTC]) return;
    RTC_DateTime dt = rtc.getDateTime();
    Serial.printf("RTC raw: %04d-%02d-%02d %02d:%02d:%02d\n",
        dt.getYear(), dt.getMonth(), dt.getDay(), dt.getHour(), dt.getMinute(), dt.getSecond());
    if (dt.getYear() >= 2020 && dt.getYear() <= 2099) {
        setenv("TZ", "UTC0", 1);
        tzset();
        struct tm t = {};
        t.tm_year = dt.getYear() - 1900;
        t.tm_mon  = dt.getMonth() - 1;
        t.tm_mday = dt.getDay();
        t.tm_hour = dt.getHour();
        t.tm_min  = dt.getMinute();
        t.tm_sec  = dt.getSecond();
        t.tm_isdst = 0;
        time_t epoch = mktime(&t);
        struct timeval tv = { .tv_sec = epoch, .tv_usec = 0 };
        settimeofday(&tv, NULL);
        Serial.printf("System clock seeded: epoch=%ld\n", (long)epoch);
    }
}

// ---------- Main init ----------

void init() {
    gpio_hold_dis((gpio_num_t)BOARD_TOUCH_RST);
    gpio_hold_dis((gpio_num_t)BOARD_LORA_RST);
    gpio_deep_sleep_hold_dis();

    // Pull all SPI CS lines high before init
    pinMode(BOARD_LORA_CS, OUTPUT);
    digitalWrite(BOARD_LORA_CS, HIGH);
    pinMode(BOARD_SD_CS, OUTPUT);
    digitalWrite(BOARD_SD_CS, HIGH);

    if (BOARD_PCA9535_INT > 0) {
        pinMode(BOARD_PCA9535_INT, INPUT_PULLUP);
    }

    Serial.begin(115200);
    SPI.begin(BOARD_SPI_SCLK, BOARD_SPI_MISO, BOARD_SPI_MOSI);
    pinMode(BOARD_BL_EN, OUTPUT);

    nsv_param_init();
    model::init_messages();

    // I2C first — epdiy reuses the existing driver
    Wire.begin(BOARD_SDA, BOARD_SCL);
    Wire.setTimeOut(50);

    peri_status[E_PERI_BQ27220] = detail::bq27220_init();
    peri_status[E_PERI_INK_POWER] = detail::screen_init();
    io_extend_lora_gps_power_on(true);
    peri_status[E_PERI_BQ25896] = detail::bq25896_init();
    peri_status[E_PERI_RTC] = detail::rtc_init();

    seed_clock_from_rtc();

    peri_status[E_PERI_TOUCH] = detail::touch_init();
    peri_status[E_PERI_KEYBOARD] = detail::keyboard_init();
    peri_status[E_PERI_SD_CARD] = detail::sd_init();
    peri_status[E_PERI_GPS] = detail::gps_init();

    // I2C mutex for cross-core safety (epdiy on Core 1, mesh/RTC on Core 0)
    i2c_mutex = xSemaphoreCreateMutex();

    Serial.println("Board init complete");
    for (int i = 0; i < E_PERI_MAX; i++) {
        Serial.printf("  %s = %s\n", peri_name(i), peri_status[i] ? "OK" : "FAIL");
    }
    Serial.printf("Free DRAM: %u, Free PSRAM: %u\n",
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

// ---------- Battery ----------

bool battery_is_charging() {
    return ppm.isCharging();
}

uint16_t battery_percent() {
    if (!peri_status[E_PERI_BQ27220]) return 0;
    uint16_t pct = bq27220.getStateOfCharge();
    if (pct > 100) return 0;
    return pct;
}

uint16_t battery_voltage_mv() {
    if (!peri_status[E_PERI_BQ27220]) return 0;
    return bq27220.getVoltage();
}

int16_t battery_current_ma() {
    if (!peri_status[E_PERI_BQ27220]) return 0;
    return bq27220.getCurrent();
}

uint16_t battery_temperature() {
    if (!peri_status[E_PERI_BQ27220]) return 0;
    return bq27220.getTemperature();
}

uint16_t battery_full_capacity() {
    if (!peri_status[E_PERI_BQ27220]) return 0;
    return bq27220.getFullChargeCapacity();
}

uint16_t battery_design_capacity() {
    if (!peri_status[E_PERI_BQ27220]) return 0;
    return bq27220.getDesignCapacity();
}

uint16_t battery_remain_capacity() {
    if (!peri_status[E_PERI_BQ27220]) return 0;
    return bq27220.getRemainingCapacity();
}

uint16_t battery_health() {
    if (!peri_status[E_PERI_BQ27220]) return 0;
    return bq27220.getStateOfHealth();
}

// BQ25896 charger
bool charger_is_valid() { return peri_status[E_PERI_BQ25896]; }
bool charger_vbus_in() { return ppm.isVbusIn(); }
const char* charger_status_str() { return ppm.getChargeStatusString(); }
const char* charger_bus_status_str() { return ppm.getBusStatusString(); }
const char* charger_ntc_status_str() { return ppm.getNTCStatusString(); }
float charger_vbus_v() { return ppm.getVbusVoltage() / 1000.0f; }
float charger_vsys_v() { return ppm.getSystemVoltage() / 1000.0f; }
float charger_vbat_v() { return ppm.getBattVoltage() / 1000.0f; }
float charger_target_v() { return ppm.getChargeTargetVoltage() / 1000.0f; }
float charger_current_ma() { return ppm.getChargeCurrent(); }
float charger_prechrg_ma() { return ppm.getPrechargeCurr(); }

// ---------- GPS ----------

void gps_get_coord(double* lat, double* lng) {
    *lat = 0; *lng = 0; // GPS now via MeshCore sensors
}

uint32_t gps_satellites() {
    return 0; // GPS now via MeshCore sensors
}

// ---------- RTC ----------

void rtc_get_time(uint8_t* h, uint8_t* m, uint8_t* s) {
    RTC_DateTime dt = rtc.getDateTime();
    *h = dt.getHour();
    *m = dt.getMinute();
    *s = dt.getSecond();
}

void rtc_get_date(uint8_t* year, uint8_t* month, uint8_t* day, uint8_t* week) {
    RTC_DateTime dt = rtc.getDateTime();
    if (year) *year = dt.getYear() % 100;
    if (month) *month = dt.getMonth();
    if (day) *day = dt.getDay();
    if (week) *week = 0;
}

// ---------- Keyboard ----------

int keyboard_read_char() {
    if (!peri_status[E_PERI_KEYBOARD]) return -1;

    uint32_t now = millis();
    if (now - cardkb_last_poll < cardkb_poll_interval) return -1;
    cardkb_last_poll = now;

    if (i2c_mutex && xSemaphoreTake(i2c_mutex, pdMS_TO_TICKS(5)) != pdTRUE) {
        return -1;
    }

    Wire.requestFrom((uint8_t)CARDKB_I2C_ADDR, (uint8_t)1);
    bool available = Wire.available();
    uint8_t raw = available ? Wire.read() : 0;

    if (i2c_mutex) {
        xSemaphoreGive(i2c_mutex);
    }

    if (!available) {
        cardkb_error_count++;
        if (cardkb_error_count >= 3) {
            Wire.begin(BOARD_SDA, BOARD_SCL);
            Wire.setTimeOut(50);
            cardkb_poll_interval = 500;
            cardkb_error_count = 0;
            Serial.println("CardKB I2C recovery");
        }
        return -1;
    }

    cardkb_error_count = 0;
    cardkb_poll_interval = 50;

    if (raw == 0) return -1;

    switch (raw) {
        case CARDKB_KEY_UP: return CARDKB_KEY_PREV;
        case CARDKB_KEY_DOWN: return CARDKB_KEY_NEXT;
        case CARDKB_KEY_LEFT: return CARDKB_KEY_LEFT_NAV;
        case CARDKB_KEY_RIGHT: return CARDKB_KEY_RIGHT_NAV;
        case CARDKB_KEY_ENTER: return '\r';
        case CARDKB_KEY_BS: return '\b';
        case CARDKB_KEY_DEL: return '\b';
        case CARDKB_KEY_ESC: return 0x1B;
        case CARDKB_KEY_TAB: return 0x09;
        default:
            if (raw >= 0x20 && raw <= 0x7E) {
                return (int)raw;
            }
            return -1;
    }
}

void keyboard_set_backlight(uint8_t level) {
    (void)level;
}

uint8_t keyboard_get_backlight() {
    return 0;
}

} // namespace board

#endif // BOARD_EPAPER
