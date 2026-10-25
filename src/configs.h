#ifndef configs_h
#define configs_h

// Pins used:

/*
1 BTN
2 SPI
4 BAT I2C
5 BAT I2C
6 SPI
7 SPI
8 BTN
9 BTN
10 SD CS
13 GPS UART
14 GPS UART
23 TFT
24 TFT
27 TFT
28 ACT LED
*/

#define JCMK_HOST_BOARD

// Board scope. Build with -DBOARD_XIAO_C5 for a bare Seeed XIAO ESP32-C5
// node (onboard user LED GPIO27, active-low, no JCMK carrier).
#if !defined(BOARD_XIAO_C5) && !defined(JCMK_HOST_BOARD)
  #define JCMK_HOST_BOARD
#endif


//// Firmware info stuff
#define FIRMWARE_VERSION "v2.3.2"
#define DEVICE_NAME      "JCMK C5 Wardriver"

//// Role stuff
#if !defined(SOLO) && !defined(CORE) && !defined(NODE)
  #define SOLO
#endif

#if !defined(SOLO) && !defined(CORE) && !defined(NODE)
  #error "Define exactly one role: SOLO, CORE, or NODE"
#endif
#if defined(SOLO) && defined(CORE) && defined(NODE)
  #error "Define exactly one role: SOLO, CORE, or NODE"
#elif defined(SOLO) && defined(CORE)
  #error "Define exactly one role: SOLO, CORE, or NODE"
#elif defined(CORE) && defined(NODE)
  #error "Define exactly one role: SOLO, CORE, or NODE"
#elif defined(SOLO) && defined(NODE)
  #error "Define exactly one role: SOLO, CORE, or NODE"
#endif

#define SOLO_MODE 1
#define NODE_MODE 2
#define CORE_MODE 3

#define ENOW_KEY_MAX_LEN 32
#define ENOW_TEXT_MAX    200

//// BLE stuff
#define BLE_SCAN_DURATION   1 * 500 // 0.5 second


//// LED stuff
#ifdef BOARD_XIAO_C5
  // Seeed XIAO ESP32-C5 onboard user LED: GPIO27, active-LOW.
  #define LED_PIN 27
  #define LED_ON  LOW
  #define LED_OFF HIGH
  #if LED_PIN != 27
    #error "XIAO C5 user LED is GPIO27"
  #endif
  #if LED_ON != LOW
    #error "XIAO C5 user LED is active-low"
  #endif
#else
  // JCMK carrier board activity LED: GPIO28, active-HIGH.
  #define LED_PIN 28
  #define LED_ON  HIGH
  #define LED_OFF LOW
#endif


//// Display stuff
#define ON  HIGH
#define OFF LOW

#define TFT_HEIGHT 80
#define TFT_WIDTH  160

#define TFT_SPI_SPEED 27000000

#define TFT_CS   23
#define TFT_DC   24
#define TFT_RST  -1
#define TOUCH_CS -1
#define TFT_MOSI 7
#define TFT_SCLK 6
#ifdef BOARD_XIAO_C5
  // GPIO27 is the XIAO user LED on a bare node. No panel/backlight.
  #define TFT_BL -1
  #if TFT_BL >= 0
    #error "XIAO C5 user LED shares GPIO27 with TFT_BL; set TFT_BL=-1"
  #endif
#else
  #define TFT_BL   27
#endif


//// UI Stuff
#define UI_UPDATE_TIME 5 * 1000 // 1 second

#define U_BTN 9
#define D_BTN 8
#ifdef BOARD_XIAO_C5
  // XIAO BOOT button (GPIO28) doubles as "hold at power for admin UI".
  #define C_BTN 28
#else
  #define C_BTN 1
#endif

#define C_PULL false
#define U_PULL false
#define D_PULL false

#define WEB_PAGE_TIMEOUT 60 * 1000 // 60 seconds
#define TIMER_UPDATE 1 * 1000 // 1 second
#define STATION_CONNECT_TIMEOUT 5 * 1000 // 5 seconds
#define WIFI_CONFIG "/settings.json"
#define LOG_FILE_NAME "wardrive"
#define SETTING_SANITY "t_ssid"

#define SMALL_CHAR_HEIGHT 8


//// Buffer stuff
#define BUF_SIZE 2 * 1024
#define SNAP_LEN 2324


//// Battery stuff
#ifndef BOARD_XIAO_C5
  #define HAS_BATTERY
#endif
#define I2C_SCL 4
#define I2C_SDA 5


//// GPS stuff
#define GPS_SERIAL_INDEX 1
#define TX_TO_GPS 13
#define RX_TO_GPS 14


//// SD stuff
#define SPI_SCK  6
#define SPI_MISO 2
#define SPI_MOSI 7
#define SD_CS    10

#define UPDATE_KEY "UpdateFile"

//// Debug log
#define DEBUG_LOG_FILE "/debug.log"


//// Switch stuff



//// Device stuff
#define HAS_PSRAM
#define HAS_GPS
#define HAS_SD


////WiFi stuff
#define mac_history_len 200
#define CHANNEL_TIMER 80
#define SOLO_SCAN_MIN_DWELL_MS 80  // conservative probe-response window
#define SOLO_SCAN_MAX_DWELL_MS 120 // remain longer when an AP responds
#define SOLO_BONUS_CHANNEL_COUNT 8 // 20% bounded exploitation budget per base sweep
#define SOLO_BLE_INTERVAL_MS 4000
#if SOLO_SCAN_MIN_DWELL_MS > SOLO_SCAN_MAX_DWELL_MS
#error "SOLO active-scan minimum dwell cannot exceed maximum dwell"
#endif
#define LOG_ROLL_ENTRIES  10000  // start a new log file after this many entries


// ============================================================
// Chunk 1: Extended feature constants
// ============================================================

//// Geofence stuff
#define MAX_GEOFENCES         5

//// SSID Exclusion stuff
#define MAX_SSID_EXCLUSIONS   10

//// Dock mode stuff
#define DOCK_CONNECT_ATTEMPTS  3
#define DOCK_CONNECT_TIMEOUT   10 * 1000   // 10 seconds per attempt
#define DOCK_SCAN_INTERVAL     30 * 1000   // passive scan every 30s while docked
#define DOCK_DEPART_SCANS      2           // consecutive misses before resuming wardrive
#define DOCK_FAIL_DISPLAY_MS   20 * 1000   // show K1T failure message for 20s
#define STANDBY_SCAN_INTERVAL  30 * 1000   // scan for K1T every 30s while in standby (no GPS)

//// Settings JSON buffer — bumped from 2048 to handle 32 settings entries
#define SETTINGS_JSON_SIZE     8192

// ============================================================
// Chunk 6: Dock mode state constants
// ============================================================
#define DOCK_STATE_NONE       0  // wardriving normally
#define DOCK_STATE_CONNECTING 1  // attempting WiFi connect to trigger SSID
#define DOCK_STATE_UPLOADING  2  // uploading all pending log files
#define DOCK_STATE_MONITORING 3  // watching for trigger SSID departure
#define DOCK_STATE_FAILED     4  // connect failed, showing message before resume

#endif
