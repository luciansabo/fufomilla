// comment/undef this to disable debugging
//#define DEBUG 1

#define HAS_FOOD_LEVEL_SENSOR 1
#define HAS_RTC               1
#define HAS_CAMERA            1
// ===================
// Select camera model
// ===================
//#define CAMERA_MODEL_WROVER_KIT // Has PSRAM
//#define CAMERA_MODEL_ESP_EYE // Has PSRAM
//#define CAMERA_MODEL_ESP32S3_EYE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_PSRAM // Has PSRAM
//#define CAMERA_MODEL_M5STACK_V2_PSRAM // M5Camera version B Has PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE // Has PSRAM
//#define CAMERA_MODEL_M5STACK_ESP32CAM // No PSRAM
//#define CAMERA_MODEL_M5STACK_UNITCAM // No PSRAM
#define CAMERA_MODEL_AI_THINKER // Has PSRAM
//#define CAMERA_MODEL_TTGO_T_JOURNAL // No PSRAM
// ** Espressif Internal Boards **
//#define CAMERA_MODEL_ESP32_CAM_BOARD
//#define CAMERA_MODEL_ESP32S2_CAM_BOARD
//#define CAMERA_MODEL_ESP32S3_CAM_LCD

// Pins
#define PIN_SERVO 13
#define PIN_BRIGHT_LED 4
#define PIN_BUILTIN_LED 33

#define PIN_RTC_DAT 2
#define PIN_RTC_CLK 16
#define PIN_RTC_RST 12

#define PIN_HCSR04_TRIG 14
#define PIN_HCSR04_ECHO 15


// Timing
#define DEFAULT_JERK_TIME 1 * 1000 // controls portion. Time in ms to rotate left/right
#define MQTT_WATCHDOG_INTERVAL  (60 * 1000) // 1m
#define MQTT_HANDLING_INTERVAL  (1 * 1000) // 1s
#define MQTT_KEEP_ALIVE_SEC     60 // 60s
#define FOOD_LEVEL_READING_INTERVAL (3600 * 1000) // 1h
#define RTC_SYNC_INTERVAL_WITH_NTC (4 * 60 * 1000) // every 4h

// Config portal
#define CONFIG_PORTAL_AP_NAME   "Fufomilla"
#define CONFIG_PORTAL_AP_PASS   ""
#define CONFIG_PORTAL_TIMEOUT_SEC (10 * 60) // 10 min

// wifi
#define HOSTNAME                "Fufomilla"
#define WIFI_CONNECT_TIMEOUT_SEC 40 // 40s

// ID of the settings block
#define CONFIG_VERSION "FM1"

// Tell it where to store your config data in EEPROM
#define CONFIG_START_ADDR 0

// Time
#define TIME_TZ                "EET-2EEST,M3.5.0/3,M10.5.0/4"
#define TIME_NTP_SERVER        "pool.ntp.org"

// MQTT
#define DEFAULT_MQTT_BROKER   "192.168.0.1"
#define DEFAULT_MQTT_PORT     "1883"
#define DEFAULT_MQTT_USERNAME "homeassistant"
#define DEFAULT_MQTT_PASSWORD ""
#define TOPIC_COMMAND_FEED_ME "saboiot/fufomilla/commands/feed"
#define TOPIC_EVENTS          "saboiot/fufomilla/events"
#define TOPIC_FOOD_LEVEL      "saboiot/fufomilla/food-level"
#define TOPIC_AVAILABILITY    "saboiot/fufomilla/availability"

#define FOOD_CONTAINER_HEIGHT_CM 20
#define FOOD_CONTAINER_PORTIONS 36
