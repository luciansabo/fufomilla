/*
   Fufomilla - cat feeder
   Board: ESP32 Dev module
   Minimal SPIFFS
   PSRAM Disabled -> imcopatible with DS1302 RTC module
   Arduino core/events run on Core 1
   Webserver for camera task runs on Core 0
*/

#include <ESP32Servo.h>
#include <WiFiManager.h>
#include <PubSubClient.h> // MQTT Client
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include <ESPmDNS.h>
#include <time.h>
#include <CronAlarms.h>
#include <SimpleTimer.h> // https://playground.arduino.cc/Code/SimpleTimer/

#include "logging.h"

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

ThreeWire threeWire(PIN_RTC_DAT, PIN_RTC_CLK, PIN_RTC_RST); // IO, SCLK, CE
RtcDS1302<ThreeWire> rtc(threeWire);
#endif

// network
WiFiClient espClient;
unsigned long wifiReconnectTime = 0;
// servers
PubSubClient mqtt(espClient);

bool isManualDispense = false;

#ifdef HAS_CAMERA
//#include "esp_camera.h"
#include "src/camera/OV2640.h"
#include "src/camera/camera_pins.h"
OV2640 cam;
WebServer webserver(80);
TaskHandle_t webserverTaskHandle;

const char HEADER[] = "HTTP/1.1 200 OK\r\n" \
                      "Access-Control-Allow-Origin: *\r\n" \
                      "Content-Type: multipart/x-mixed-replace; boundary=123456789000000000000987654321\r\n";
const char BOUNDARY[] = "\r\n--123456789000000000000987654321\r\n";
const char CTNTTYPE[] = "Content-Type: image/jpeg\r\nContent-Length: ";
const int hdrLen = strlen(HEADER);
const int bdrLen = strlen(BOUNDARY);
const int cntLen = strlen(CTNTTYPE);

void handleJpgStream(void)
{
  char buf[32];
  int s;

  WiFiClient client = webserver.client();

  client.write(HEADER, hdrLen);
  client.write(BOUNDARY, bdrLen);

  while (true)
  {        
    if (!client.connected()) break;
    cam.run();
    s = cam.getSize();
    client.write(CTNTTYPE, cntLen);
    sprintf( buf, "%d\r\n\r\n", s );
    client.write(buf, strlen(buf));
    client.write((char *)cam.getfb(), s);
    client.write(BOUNDARY, bdrLen);
  }
}

const char JHEADER[] = "HTTP/1.1 200 OK\r\n" \
                       "Content-disposition: inline; filename=capture.jpg\r\n" \
                       "Content-type: image/jpeg\r\n\r\n";
const int jhdLen = strlen(JHEADER);

// ------------------------------------------------------------------------------------------

void handleJpg(void)
{
  WiFiClient client = webserver.client();

  if (!client.connected()) return;    
  cam.run();
  client.write(JHEADER, jhdLen);
  client.write((char *)cam.getfb(), cam.getSize());
}

// ------------------------------------------------------------------------------------------

void handleNotFound()
{  
  webserver.send(404, "text/plain", "Not found");
}

void webserverTask(void * parameter) {
  for(;;) {
    webserver.handleClient(); 
  }
}
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

void LOG(EventCode code) {
  //time_t tnow = time(nullptr);
  //logs.push(logRecord{tnow, code});
  //Serial.println(code);
}


void setupOTA() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.begin();
  ArduinoOTA.onStart([]() {
    Serial.println("OTA firmware update started");
    //hwTimer.disableTimer();
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("OTA Updated completed");
    // hwTimer.enableTimer();
  });
  ArduinoOTA.onError([](ota_error_t error) {
    //hwTimer.enableTimer();
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
}

// ------------------------------------------------------------------------------------------


void mqttPublish(const char *topic, const char *message, bool retain = false) {
  ensureMqttConnected();

  if (!mqtt.publish(topic, message, retain)) {
    LOG(EventCode::mqttPublishError);
    Serial.println("MQTT Publish failed." );
    Serial.printf("  Topic: %s\n", topic);
    Serial.printf("  Msg: %s\n", message);
    Serial.printf("  State: %d\n", mqtt.state());
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

  LOG(EventCode::mqttDisconnected);

  Serial.print("Connecting to the mqtt broker... ");

  if (mqtt.connect(HOSTNAME, mqttConfig.username, mqttConfig.password, TOPIC_AVAILABILITY, 0, true, "{\"status\": \"offline\", \"upSince\": null}")) {
    Serial.print("connected");
    mqtt.subscribe(TOPIC_COMMAND_FEED_ME);
    LOG(EventCode::mqttConnected);
    char message[200];
    //char timeBuf[sizeof "2011-10-08T07:07:09Z"];
    //strftime(timeBuf, sizeof timeBuf, "%FT%TZ", localtime(&stats.upSince));
    char timeBuf[sizeof "Joi, 30 Aug 2022 09:03"];
    strftime(timeBuf, sizeof timeBuf, "%a, %e %b %Y %R", localtime(&stats.upSince));
    sprintf(message, "{\"status\": \"online\",  \"upSince\": \"%s\"}", timeBuf);
    Serial.println(message);
    mqttPublish(TOPIC_AVAILABILITY, message, true);
  } else {
    LOG(EventCode::mqttConnectFrror);
    Serial.print("failed with state ");
    Serial.print(mqtt.state());
  }
  Serial.println("");
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
void onDispenseStart() {
  
}

// ------------------------------------------------------------------------------------------

void onDispenseCompleted() {
  char message[200];
  char timeBuf[sizeof "Joi, 30 Aug 2022 09:03"];
  struct tm timeinfo;
  
  getLocalTime(&timeinfo);

  strftime(timeBuf, sizeof timeBuf, "%R %e %b", &timeinfo);
  sprintf(message, "{\"event\": \"%s %s\"}",
          isManualDispense ? "Manual feeding at" : "Scheduled feeding at",
          timeBuf
         );
  mqttPublish(TOPIC_EVENTS, message, true);

#ifdef HAS_FOOD_LEVEL_SENSOR
  readFoodLevel();
#endif
}

// ------------------------------------------------------------------------------------------

void dispense() {
  onDispenseStart();
  Serial.println("Scheduled dispense");
  _dispenseMinPortion();
  delay(200);
  _dispenseMinPortion();

  onDispenseCompleted();
}

// ------------------------------------------------------------------------------------------

void manualDispense() {
  onDispenseStart();
  Serial.println("Manually Dispensing");
  isManualDispense = true;
  _dispenseMinPortion();

  onDispenseCompleted();
  isManualDispense = false;
}

// ------------------------------------------------------------------------------------------

void mqttCallback(char* topic, byte* message, unsigned int length) {
  Serial.printf("Message arrived on topic: %s\n", topic);  

  // If a message is received on the topic esp32/output, you check if the message is either "on" or "off".
  // Changes the output state according to the message
  if (String(topic) == TOPIC_COMMAND_FEED_ME) {
    manualDispense();
  }
}

// ------------------------------------------------------------------------------------------

void WiFiStationConnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("Connected to AP successfully!");
  wifiReconnectTime = 0;
}

// ------------------------------------------------------------------------------------------

void WiFiGotIP(WiFiEvent_t event, WiFiEventInfo_t info) {
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  ensureMqttConnected();
}

// ------------------------------------------------------------------------------------------

void wifiReconnect() {
  Serial.println("Trying to Reconnect to Wifi");
  WiFi.disconnect();
  Cron.delay(5000);
  WiFi.reconnect();
}
// ------------------------------------------------------------------------------------------

/**
   Wifi will try to connect as soon as it gets disconnected. It tries for 20 sec
   Then it aborts.
   Waits for 5s before each attempt
   Finally, a watchdog set at 5 mins will retry if still not connected

*/
void WiFiStationDisconnected(WiFiEvent_t event, WiFiEventInfo_t info) {
  if (millis() - wifiReconnectTime > 20 * 1000) {
    return;
  }

  Serial.printf("WiFi lost connection.\n");
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
           TOPIC_COMMAND_FEED_ME
          );
  mqttPublish("homeassistant/button/fufomilla/feedMe/config" , message, true);

#ifdef HAS_FOOD_LEVEL_SENSOR
  // Food Level percentage
  snprintf(message, sizeof(message), "{\"name\": \"Fufomilla Food Level\", \"stat_t\": \"%s\", \"unit_of_meas\": \"%%\", \
    %s, \"val_tpl\": \"{{ value_json.level}}\", \"frc_upd\": true }",
           TOPIC_FOOD_LEVEL,
           availText
          );
  mqttPublish("homeassistant/sensor/fufomilla/foodLevel/config" , message, true);

  // Food level - remaining portions
  snprintf(message, sizeof(message), "{\"name\": \"Fufomilla Remaining Portions\", \"stat_t\": \"%s\", %s, \
    \"val_tpl\": \"{{ value_json.remainingPortions}}\", \"frc_upd\": true }",
           TOPIC_FOOD_LEVEL,
           availText
          );
  mqttPublish("homeassistant/sensor/fufomilla/remainingPortions/config" , message, true);
#endif

  // Last event
  sprintf(message, "{\"name\": \"Fufomilla Last event\", \"stat_t\": \"%s\", \
    \"val_tpl\": \"{{ value_json.event}}\", \"frc_upd\": true }",
          TOPIC_EVENTS
         );
  mqttPublish("homeassistant/sensor/fufomilla/lastEvent/config" , message, true);

  // Up since
  sprintf(message, "{\"name\": \"Fufomilla up since\", \"stat_t\": \"%s\", \
    \"val_tpl\": \"{{ value_json.upSince}}\", \"frc_upd\": true }",
          TOPIC_AVAILABILITY
         );
  mqttPublish("homeassistant/sensor/fufomilla/upSince/config" , message, true);
}

// ------------------------------------------------------------------------------------------
#ifdef HAS_RTC
void setupRTC() {
  rtc.Begin();

  if (!rtc.IsDateTimeValid()) {
    // Common Causes:
    //    1) first time you ran and the device wasn't running yet
    //    2) the battery on the device is low or even missing

    Serial.println("RTC lost confidence in the DateTime!");
    rtc.SetDateTime(RtcDateTime(__DATE__, __TIME__));
  }

  if (rtc.GetIsWriteProtected()) {
    Serial.println("RTC was write protected, enabling writing now");
    rtc.SetIsWriteProtected(false);
  }

  if (!rtc.GetIsRunning()) {
    Serial.println("RTC was not actively running, starting now");
    rtc.SetIsRunning(true);
  }
}
#endif
// ------------------------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  LOG(EventCode::deviceOn);
#ifdef DEBUG
  while (!Serial); // wait for Arduino Serial Monitor
#endif

  Serial.println("Fufomilla Feeder is starting");
  pinMode(PIN_BRIGHT_LED, OUTPUT);
  pinMode(PIN_BUILTIN_LED, OUTPUT);
  
  digitalWrite(PIN_BRIGHT_LED, LOW);
  digitalWrite(PIN_BUILTIN_LED, HIGH);

#ifdef HAS_RTC
  setupRTC();
  RtcDateTime now = rtc.GetDateTime();  
  if (now.IsValid()) {
    struct timeval cTimeNow;
    cTimeNow.tv_sec = now.Epoch64Time();
    settimeofday(&cTimeNow, NULL);
  }
  printLocalTime();
#endif


  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  // Allow allocation of all timers
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  myservo.setPeriodHertz(50);    // standard 50 hz servo
  // using default min/max of 1000us and 2000us
  myservo.attach(PIN_SERVO, 1000, 2000);

  wifiManager.setHostname(HOSTNAME);
  wifiManager.setWiFiAutoReconnect(true);
  wifiManager.setConnectTimeout(WIFI_CONNECT_TIMEOUT_SEC);
  wifiManager.setConnectRetries(3);
  wifiManager.setDebugOutput(false);
  wifiManager.setEnableConfigPortal(false);
  wifiManager.setConfigPortalBlocking(true);
  wifiManager.setConfigPortalTimeout(CONFIG_PORTAL_TIMEOUT_SEC);
  //wifiManager.setSaveConfigCallback(saveConfigCallback);
  wifiManager.setBreakAfterConfig(true); // call save callback even if empty wifi or failed
  // custom params
  wifiManager.addParameter(&paramMqttBroker);
  wifiManager.addParameter(&paramMqttPort);
  wifiManager.addParameter(&paramMqttUsername);
  wifiManager.addParameter(&paramMqttPassword);
  Serial.println("Connecting to Wifi...");
  wifiManager.autoConnect();

  // must be before logging starts
  configTzTime(TIME_TZ, TIME_NTP_SERVER);

  struct tm timeinfo;
  getLocalTime(&timeinfo);

#ifndef HAS_RTC
  // wait until we have acquired the time
  while (!getLocalTime(&timeinfo)) {
    delay(100);
  }
#endif

  stats.upSince = mktime(&timeinfo);

  mqtt.setServer(mqttConfig.broker, atoi(mqttConfig.port));
  mqtt.setKeepAlive(MQTT_KEEP_ALIVE_SEC);
  mqtt.setCallback(mqttCallback);
  mqtt.setBufferSize(512); // increase/double buffer size so larger messages can be sent without reallocation
  ensureMqttConnected();

  WiFi.onEvent(WiFiStationConnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiStationDisconnected, WiFiEvent_t::ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  setupOTA();   

  // wifi watchdog at every 5 min
  Cron.create((char*)"0 */5 * * * *", wifiWatchdog, false);

  // setup feeeding schedule
  Cron.create((char*)"0 0 7 * * *", dispense, false); // 07:00 each day
  Cron.create((char*)"0 0 19 * * *", dispense, false); // 19:00 each day

  publishMQTTDiscoveryMessages();

#ifdef HAS_FOOD_LEVEL_SENSOR
  simpleTimer.setTimeout(FOOD_LEVEL_READING_INTERVAL, readFoodLevel);
  readFoodLevel();
#endif

  // 10s wifi check
  simpleTimer.setInterval(10 * 1000, []() {
    digitalWrite(PIN_BUILTIN_LED, WiFi.status() == WL_CONNECTED);
  });

#ifdef HAS_RTC
  simpleTimer.setInterval(RTC_SYNC_INTERVAL_WITH_NTC, []() {
    // adjust RTC time from NTP
    RtcDateTime utc;
    utc.InitWithEpoch64Time(time(NULL));
    rtc.SetDateTime(utc);
    RtcDateTime now = rtc.GetDateTime();
  });
#endif 

#ifdef HAS_CAMERA    
  initCamera();
  webserver.on("/mjpeg/1", HTTP_GET, handleJpgStream);
  webserver.on("/jpg", HTTP_GET, handleJpg);
  webserver.onNotFound(handleNotFound);
  webserver.begin();  

  if(!MDNS.begin(HOSTNAME)) {
     Serial.println("Error encountered while starting mDNS");
     return;
  }

  xTaskCreatePinnedToCore(
    webserverTask,   // function
    "webserverTask",     // task name
    10000,       // stack size
    NULL,        // params
    2,           // priority
    // when priority was set to 100 then network scan would not complete
    &webserverTaskHandle,      // Task handle to keep track of created task
    0 // core number
  );
#endif

  //LOG(EventCode::deviceReady);
}

// ------------------------------------------------------------------------------------------

void initCamera() {     
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;

  // Frame parameters
  //  config.frame_size = FRAMESIZE_UXGA;
  config.frame_size = FRAMESIZE_QVGA;
  config.jpeg_quality = 10;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_DRAM;

  cam.init(config);
}

/*
  #define countof(a) (sizeof(a) / sizeof(a[0]))
  void printDateTime(const RtcDateTime& dt)
  {
    char datestring[20];

    snprintf_P(datestring,
            countof(datestring),
            PSTR("%02u/%02u/%04u %02u:%02u:%02u"),
            dt.Month(),
            dt.Day(),
            dt.Year(),
            dt.Hour(),
            dt.Minute(),
            dt.Second() );
    Serial.print(datestring);
  }*/

// ------------------------------------------------------------------------------------------
#ifdef HAS_FOOD_LEVEL_SENSOR
void readFoodLevel() {
  char message[200];
  int distance = distanceSensor.measureDistanceCm();
  Serial.println(distance);
  if (distance <= 0) {
    return;
  }


  if (distance > FOOD_CONTAINER_HEIGHT_CM) {
    distance = FOOD_CONTAINER_HEIGHT_CM;
  }

  uint8_t level = 100 - ((distance * 100) / FOOD_CONTAINER_HEIGHT_CM);

  sprintf(message, "{\"level\": %d, \"remainingPortions\": %d}",
          level,
          (level * FOOD_CONTAINER_PORTIONS) / 100
         );

  mqttPublish(TOPIC_FOOD_LEVEL, message, true);
}
#endif

// ------------------------------------------------------------------------------------------

void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}


// ------------------------------------------------------------------------------------------

void loop() {
  mqtt.loop();
  ArduinoOTA.handle();
  simpleTimer.run();
  Cron.delay(0);
}
