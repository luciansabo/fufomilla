// comment/undef this to disable debugging
#define DEBUG 1

// Pins
#define SERVO_PIN 5

// Timing
#define DEFAULT_JERK_TIME 1 * 1000 // controls portion. Time in ms to rotate left/right
#define MQTT_WATCHDOG_INTERVAL  (60 * 1000) // 1m
#define MQTT_HANDLING_INTERVAL  (1 * 1000) // 1s
#define MQTT_KEEP_ALIVE_SEC     60 // 60s

// Config portal
#define CONFIG_PORTAL_AP_NAME   "Fufomilla"
#define CONFIG_PORTAL_AP_PASS   ""
#define CONFIG_PORTAL_TIMEOUT_SEC (10 * 60) // 10 min
#define WIFI_CONNECT_TIMEOUT_SEC 40 // 40s
// ID of the settings block
#define CONFIG_VERSION "FM1"

// Tell it where to store your config data in EEPROM
#define CONFIG_START_ADDR 0

// Other
#define HOSTNAME                "Fufomilla"
#define TIME_TZ                "EET-2EEST,M3.5.0/3,M10.5.0/4"
#define TIME_NTP_SERVER        "pool.ntp.org"

#define DEFAULT_MQTT_BROKER "192.168.0.1"
#define DEFAULT_MQTT_PORT "1883"
#define DEFAULT_MQTT_USERNAME "homeassistant"
#define DEFAULT_MQTT_PASSWORD ""
