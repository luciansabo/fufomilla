/*
   Fufomilla - cat feeder
   Board: ESP32 Dev module
   Minimal SPIFFS
   PSRAM Disabled -> incompatible with DS1302 RTC module
   Arduino run on Core 1, events on Core 0
*/

#include <ESP32Servo.h>
#include <WiFiManager.h>
#include <PubSubClient.h>  // MQTT Client
#include <ArduinoOTA.h>
#include <ESPmDNS.h>
#include <time.h>
#include <CronAlarms.h>
#include <SimpleTimer.h>  // https://playground.arduino.cc/Code/SimpleTimer/

// ESP32 needs EEPROM
#if defined(ESP32)
#define USE_SPIFFS true
#define ESP_DRD_USE_EEPROM true
#else
#error This code is intended to run on the ESP32 platform! Please check your Tools->Board setting.
#endif
#include <ESP_DoubleResetDetector.h>  //https://github.com/khoih-prog/ESP_DoubleResetDetector

// app config
#include "config.h"
#include "config.local.h"

#ifdef HAS_FOOD_LEVEL_SENSOR
#include <HCSR04.h>
UltraSonicDistanceSensor distanceSensor(PIN_HCSR04_TRIG, PIN_HCSR04_ECHO);
#endif

#ifdef HAS_RTC
#include <ThreeWire.h>
#include <RtcDS1302.h>

ThreeWire threeWire(PIN_RTC_DAT, PIN_RTC_CLK, PIN_RTC_RST);  // IO, SCLK, CE
RtcDS1302<ThreeWire> rtc(threeWire);
#endif

DoubleResetDetector* drd;

// network
WiFiClient espClient;
unsigned long wifiReconnectTime = 0;
// servers
PubSubClient mqtt(espClient);

bool isManualDispense = false;

#ifdef HAS_CAMERA
#include "Esp32CamAsyncWebserver.h"
camera_config_t esp32cam_aithinker_config{

  .pin_pwdn = 32,
  .pin_reset = -1,

  .pin_xclk = 0,

  .pin_sscb_sda = 26,
  .pin_sscb_scl = 27,

  // Note: LED GPIO is apparently 4 not sure where that goes
  // per https://github.com/donny681/ESP32_CAMERA_QR/blob/e4ef44549876457cd841f33a0892c82a71f35358/main/led.c
  .pin_d7 = 35,
  .pin_d6 = 34,
  .pin_d5 = 39,
  .pin_d4 = 36,
  .pin_d3 = 21,
  .pin_d2 = 19,
  .pin_d1 = 18,
  .pin_d0 = 5,
  .pin_vsync = 25,
  .pin_href = 23,
  .pin_pclk = 22,
  .xclk_freq_hz = 20000000,  // faster fps
  .ledc_timer = LEDC_TIMER_1,
  .ledc_channel = LEDC_CHANNEL_1,
  .pixel_format = PIXFORMAT_JPEG,
  // .frame_size = FRAMESIZE_UXGA, // needs 234K of framebuffer space
  // .frame_size = FRAMESIZE_SXGA, // needs 160K for framebuffer
  // .frame_size = FRAMESIZE_XGA, // needs 96K or even smaller FRAMESIZE_SVGA - can work if using only 1 fb
  .frame_size = FRAMESIZE_QVGA,      // 240x176
  .jpeg_quality = 15,                //0-63 lower numbers are higher quality
  .fb_count = 1,                     // if more than one i2s runs in continous mode.  Use only with jpeg
  .fb_location = CAMERA_FB_IN_DRAM,  // disable PSRAM
  .grab_mode = CAMERA_GRAB_WHEN_EMPTY
};

Esp32CamWebserver camServer;
#endif

// software timer
SimpleTimer simpleTimer;

// MQTT Broker
struct MqttConfigStruct {
  char configVersion[5] = CONFIG_VERSION;
  char broker[100] = DEFAULT_MQTT_BROKER;
  char port[5] = DEFAULT_MQTT_PORT;
  char username[30] = DEFAULT_MQTT_USERNAME;
  char password[100] = DEFAULT_MQTT_PASSWORD;
  uint16_t jerkTime = DEFAULT_JERK_TIME;
} mqttConfig;

// Wifimanager variables
WiFiManager wifiManager;
WiFiManagerParameter paramMqttBroker("mqttBroker", "MQTT broker", mqttConfig.broker, 40);
WiFiManagerParameter paramMqttPort("mqttPort", "MQTT port", mqttConfig.port, 10);
WiFiManagerParameter paramMqttUsername("mqttUser", "MQTT user", mqttConfig.username, 40);
WiFiManagerParameter paramMqttPassword("mqttPassword", "MQTT password", mqttConfig.password, 40);

Servo myservo;  // create servo object to control a servo
// 16 servo objects can be created on the ESP32

struct TStats {
  time_t upSince;
  char lastOperation[100];
} stats;

// ------------------------------------------------------------------------------------------

void setupOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.begin();
  ArduinoOTA.onStart([]() {
    log_i("OTA firmware update started");
    //hwTimer.disableTimer();
  });
  ArduinoOTA.onEnd([]() {
    log_i("OTA Updated completed");
    // hwTimer.enableTimer();
  });
  ArduinoOTA.onError([](ota_error_t error) {
    //hwTimer.enableTimer();
    log_e("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      log_e("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      log_e("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      log_e("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      log_e("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      log_e("End Failed");
    }
  });
}

// ------------------------------------------------------------------------------------------


void mqttPublish(const char* topic, const char* message, bool retain = false) {
  ensureMqttConnected();

  if (!mqtt.publish(topic, message, retain)) {
    log_e("MQTT Publish failed. Topic: %s\nMsg: %s\nState: %d", topic, message, mqtt.state());
  }
}

// ------------------------------------------------------------------------------------------

void ensureMqttConnected() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (mqtt.connected()) {
    return;
  }

  log_i("Connecting to the mqtt broker... ");

  if (mqtt.connect(HOSTNAME, mqttConfig.username, mqttConfig.password, TOPIC_AVAILABILITY, 0, true, "{\"status\": \"offline\", \"upSince\": null}")) {
    log_i("Connected to MQTT");
    mqtt.subscribe(TOPIC_COMMAND_FEED_ME);
    mqtt.subscribe(TOPIC_COMMAND_SWITCH_LED);

    char message[200];
    //char timeBuf[sizeof "2011-10-08T07:07:09Z"];
    //strftime(timeBuf, sizeof timeBuf, "%FT%TZ", localtime(&stats.upSince));
    char timeBuf[sizeof "Joi, 30 Aug 2022 09:03"];
    strftime(timeBuf, sizeof timeBuf, "%a, %e %b %Y %R", localtime(&stats.upSince));
    sprintf(message, "{\"status\": \"online\",  \"upSince\": \"%s\"}", timeBuf);
    log_i("Publish availability message: %s", message);
    mqttPublish(TOPIC_AVAILABILITY, message, true);
  } else {
    log_e("failed with state %d", mqtt.state());
  }
}

// ------------------------------------------------------------------------------------------

void _dispenseMinPortion() {
  // try to fix issue with bright led that lits up
  digitalWrite(PIN_BRIGHT_LED, LOW);
  myservo.write(180);
  delay(DEFAULT_JERK_TIME);
  myservo.write(90);
  delay(200);
  myservo.write(0);
  delay(DEFAULT_JERK_TIME);
  myservo.write(90);
  digitalWrite(PIN_BRIGHT_LED, LOW);
}

// ------------------------------------------------------------------------------------------
/*void onDispenseStart() {
  
}*/

// ------------------------------------------------------------------------------------------

void onDispenseCompleted() {
  char message[200];
  char timeBuf[sizeof "Joi, 30 Aug 2022 09:03"];
  struct tm timeinfo;

  getLocalTime(&timeinfo);

  strftime(timeBuf, sizeof timeBuf, "%R %e %b", &timeinfo);
  sprintf(message, "{\"event\": \"%s %s\"}",
          isManualDispense ? "Manual feeding at" : "Scheduled feeding at",
          timeBuf);
  mqttPublish(TOPIC_EVENTS, message, true);

#ifdef HAS_FOOD_LEVEL_SENSOR
  readFoodLevel();
#endif
}

// ------------------------------------------------------------------------------------------

void dispense() {
  //onDispenseStart();
  log_i("Scheduled dispense");
  _dispenseMinPortion();
  onDispenseCompleted();
}

// ------------------------------------------------------------------------------------------

void manualDispense() {
  //onDispenseStart();
  log_i("Manually Dispensing");
  isManualDispense = true;
  _dispenseMinPortion();

  onDispenseCompleted();
  isManualDispense = false;
}

// ------------------------------------------------------------------------------------------

void publishNightLedStatus() {
  char message[100];
  sprintf(message, "{\"status\": \"%s\"}",
          digitalRead(PIN_BRIGHT_LED) ? "ON" : "OFF");

  mqttPublish(TOPIC_LED, message, true);
}

// ------------------------------------------------------------------------------------------

void turnLedOn() {
  digitalWrite(PIN_BRIGHT_LED, HIGH);
  publishNightLedStatus();
}

// ------------------------------------------------------------------------------------------

void turnLedOff() {
  digitalWrite(PIN_BRIGHT_LED, LOW);
  publishNightLedStatus();
}


// ------------------------------------------------------------------------------------------

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  log_i("Message arrived on topic: %s", topic);

  if (String(topic) == TOPIC_COMMAND_FEED_ME) {
    manualDispense();
  } else if (String(topic) == TOPIC_COMMAND_SWITCH_LED) {
    char message[100];
    strncpy(message, (char*)payload, length);

    if (strcmp(message, "ON") == 0) {
      turnLedOn();
    } else {
      turnLedOff();
    }
  }
}

// ------------------------------------------------------------------------------------------

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  log_i("Connected to AP successfully");
  wifiReconnectTime = 0;
}

// ------------------------------------------------------------------------------------------

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
  log_i("WiFi connected. IP address: %s", WiFi.localIP().toString().c_str());
  ensureMqttConnected();
}

// ------------------------------------------------------------------------------------------

void wifiReconnect() {
  log_i("Trying to Reconnect to Wifi");
  WiFi.disconnect();
  Cron.delay(5000);
  WiFi.reconnect();
}
// ------------------------------------------------------------------------------------------

void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (millis() - wifiReconnectTime > 20 * 1000) {
    return;
  }

  log_w("WiFi lost connection.\n");
  wifiReconnectTime = millis();
  wifiReconnect();
}

// ------------------------------------------------------------------------------------------

void wifiWatchdog() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiReconnectTime = millis();
    wifiReconnect();
  }
}

// ------------------------------------------------------------------------------------------
void publishMQTTDiscoveryMessages() {
  char message[512];
  char availText[200];
  snprintf(availText, sizeof(availText), "\"avty_t\": \"%s\", \"avty_tpl\": \"{{ value_json.status }}\"", TOPIC_AVAILABILITY);

  // Feed me command
  snprintf(message, sizeof(message), "{\"name\": \"Fufomilla Feed Me\", %s, \"cmd_t\": \"%s\", \"frc_upd\": true }",
           availText,
           TOPIC_COMMAND_FEED_ME);
  mqttPublish("homeassistant/button/fufomilla/feedMe/config", message, true);

  // Switch Night Vision LED command
  snprintf(message, sizeof(message), "{\"name\": \"Fufomilla night vision LED\", %s, \"dev_cla\": \"switch\", \"cmd_t\": \"%s\", \"stat_t\": \"%s\", \"val_tpl\": \"{{ value_json.status}}\", \"frc_upd\": true }",
           availText,
           TOPIC_COMMAND_SWITCH_LED,
           TOPIC_LED);
  mqttPublish("homeassistant/switch/fufomilla/led/config", message, true);


#ifdef HAS_FOOD_LEVEL_SENSOR
  // Food Level percentage
  snprintf(message, sizeof(message), "{\"name\": \"Fufomilla Food Level\", \"stat_t\": \"%s\", \"unit_of_meas\": \"%%\", \
    %s, \"val_tpl\": \"{{ value_json.level}}\", \"frc_upd\": true }",
           TOPIC_FOOD_LEVEL,
           availText);
  mqttPublish("homeassistant/sensor/fufomilla/foodLevel/config", message, true);

  // Food level - remaining portions
  snprintf(message, sizeof(message), "{\"name\": \"Fufomilla Remaining Portions\", \"stat_t\": \"%s\", %s, \
    \"val_tpl\": \"{{ value_json.remainingPortions}}\", \"frc_upd\": true }",
           TOPIC_FOOD_LEVEL,
           availText);
  mqttPublish("homeassistant/sensor/fufomilla/remainingPortions/config", message, true);
#endif

  // Last event
  sprintf(message, "{\"name\": \"Fufomilla Last event\", \"stat_t\": \"%s\", \
    \"val_tpl\": \"{{ value_json.event}}\", \"frc_upd\": true }",
          TOPIC_EVENTS);
  mqttPublish("homeassistant/sensor/fufomilla/lastEvent/config", message, true);

  // Up since
  sprintf(message, "{\"name\": \"Fufomilla up since\", \"stat_t\": \"%s\", \
    \"val_tpl\": \"{{ value_json.upSince}}\", \"frc_upd\": true }",
          TOPIC_AVAILABILITY);
  mqttPublish("homeassistant/sensor/fufomilla/upSince/config", message, true);
}

// ------------------------------------------------------------------------------------------
#ifdef HAS_RTC
void setupRTC() {
  rtc.Begin();

  if (!rtc.IsDateTimeValid()) {
    // Common Causes:
    //    1) first time you ran and the device wasn't running yet
    //    2) the battery on the device is low or even missing

    log_w("RTC lost confidence in the DateTime!");
    rtc.SetDateTime(RtcDateTime(__DATE__, __TIME__));
  }

  if (rtc.GetIsWriteProtected()) {
    log_w("RTC was write protected, enabling writing now");
    rtc.SetIsWriteProtected(false);
  }

  if (!rtc.GetIsRunning()) {
    log_w("RTC was not actively running, starting now");
    rtc.SetIsRunning(true);
  }
}
#endif
// ------------------------------------------------------------------------------------------

#ifdef HAS_FOOD_LEVEL_SENSOR
void readFoodLevel() {
  char message[200];
  int distance = distanceSensor.measureDistanceCm();
  log_i("Read food level distance: %d cm", distance);
  if (distance <= 0) {
    return;
  }


  if (distance > FOOD_CONTAINER_HEIGHT_CM) {
    distance = FOOD_CONTAINER_HEIGHT_CM;
  }

  uint8_t level = 100 - ((distance * 100) / FOOD_CONTAINER_HEIGHT_CM);

  sprintf(message, "{\"level\": %d, \"remainingPortions\": %d}",
          level,
          (level * FOOD_CONTAINER_PORTIONS) / 100);

  mqttPublish(TOPIC_FOOD_LEVEL, message, true);
}
#endif


// ------------------------------------------------------------------------------------------

void doWiFiManager() {
  // is configuration portal requested?

  if (drd->detectDoubleReset()) {
    log_i("Starting Config Portal");
    // important - disable webserver to be able to use the config portal routes
    camServer.disable();
    wifiManager.setEnableConfigPortal(true);
    wifiManager.startConfigPortal(CONFIG_PORTAL_AP_NAME, CONFIG_PORTAL_AP_PASS);
    wifiManager.setEnableConfigPortal(false);
    camServer.enable();
  }
}

// ------------------------------------------------------------------------------------------


void setup() {

  log_i("Fufomilla Feeder is starting");

  pinMode(PIN_BRIGHT_LED, OUTPUT);
  digitalWrite(PIN_BRIGHT_LED, LOW);
  drd = new DoubleResetDetector(DRD_TIMEOUT, DRD_ADDRESS);
  doWiFiManager();

#ifdef HAS_RTC
  log_i("Initialize RTC module");
  setupRTC();
  RtcDateTime now = rtc.GetDateTime();
  if (now.IsValid()) {
    char datestring[20];

    snprintf_P(datestring,
               sizeof(datestring),
               PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
               now.Month(),
               now.Day(),
               now.Year(),
               now.Hour(),
               now.Minute(),
               now.Second());

    log_i("RTC time: %s", datestring);

    struct timeval cTimeNow;
    cTimeNow.tv_sec = now.Epoch64Time();
    settimeofday(&cTimeNow, NULL);
  } else {
    log_w("Invalid RTC time");
  }
#endif

  WiFi.mode(WIFI_STA);  // explicitly set mode, esp defaults to STA+AP

  // Allow allocation of all timers
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  myservo.setPeriodHertz(50);  // standard 50 hz servo
  // using default min/max of 1000us and 2000us
  myservo.attach(PIN_SERVO, 1000, 2000);

  wifiManager.setHostname(HOSTNAME);
  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.setConnectTimeout(WIFI_CONNECT_TIMEOUT_SEC);
  wifiManager.setConnectRetries(10);
  wifiManager.setDebugOutput(false);
  wifiManager.setEnableConfigPortal(false);
  wifiManager.setConfigPortalBlocking(true);
  wifiManager.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT_SEC);
  //wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setBreakAfterConfig(true);  // call save callback even if empty wifi or failed
  // custom params
  wifiManager.addParameter(&paramMqttBroker);
  wifiManager.addParameter(&paramMqttPort);
  wifiManager.addParameter(&paramMqttUsername);
  wifiManager.addParameter(&paramMqttPassword);
  log_i("Connecting to Wifi...");
  wifiManager.autoConnect();

  log_i("Trying to sync time with NTP");
  // must be before logging starts
  configTzTime(TIME_TZ, TIME_NTP_SERVER);

  struct tm timeinfo;
  getLocalTime(&timeinfo);

  // wait until we have acquired the time
  while (!getLocalTime(&timeinfo)) {
    delay(100);
  }
  log_i("Successfully aquired time using NTP: %s", asctime(&timeinfo));

  stats.upSince = mktime(&timeinfo);

  mqtt.setServer(mqttConfig.broker, atoi(mqttConfig.port));
  mqtt.setKeepAlive(MQTT_KEEP_ALIVE_SEC);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512);  // increase/double buffer size so larger messages can be sent without reallocation
  ensureMqttConnected();

  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  setupOTA();

  simpleTimer.setInterval(WIFI_WATCHDOG_INTERVAL, wifiWatchdog);
  simpleTimer.setInterval(MQTT_WATCHDOG_INTERVAL, ensureMqttConnected);
  simpleTimer.setInterval(MQTT_HANDLING_INTERVAL, []() {
    mqtt.loop();
  });

  // setup feeeding schedule
  Cron.create((char*)"0 0 7 * * *", dispense, false);   // 07:00 each day
  Cron.create((char*)"0 0 19 * * *", dispense, false);  // 19:00 each day

  publishMQTTDiscoveryMessages();

#ifdef HAS_FOOD_LEVEL_SENSOR
  simpleTimer.setTimeout(FOOD_LEVEL_READING_INTERVAL, readFoodLevel);
  readFoodLevel();
#endif

  publishNightLedStatus();

#ifdef HAS_RTC
  simpleTimer.setInterval(RTC_SYNC_INTERVAL_WITH_NTC, []() {
    log_i("Sync RTC module with NTP time");
    // adjust RTC time from NTP
    RtcDateTime utc;
    utc.InitWithEpoch64Time(time(NULL));
    rtc.SetDateTime(utc);
    RtcDateTime now = rtc.GetDateTime();
  });
#endif

#ifdef HAS_CAMERA
  log_i("Initialize camera server");
  camServer.start(esp32cam_aithinker_config);
#endif

  log_i("Initialize MDNS");
  if (!MDNS.begin(HOSTNAME)) {
    log_e("Error encountered while starting mDNS");
  }
}

// ------------------------------------------------------------------------------------------

void loop() {
  ArduinoOTA.handle();
  simpleTimer.run();
  Cron.delay(0);

#ifdef HAS_CAMERA
  camServer.run();
#endif

  // Call the double reset detector loop method every so often,
  // so that it can recognise when the timeout expires.
  // You can also call drd.stop() when you wish to no longer
  // consider the next reset as a double reset.
  drd->loop();
}
