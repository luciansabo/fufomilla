/*
   Fufomilla - cat feeder
   Board: Doit ESP32 Dev Kit v1 or compatible
*/

#include <ESP32Servo.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>
#include <time.h>
#include <CronAlarms.h>

#include "logging.h"

// app config
#include "config.h"
#include "config.local.h"

// network
WiFiClient espClient;
unsigned long wifiReconnectTime = 0;
// servers
PubSubClient mqtt(espClient);

bool isManualDispense = false;

// MQTT Broker
struct MqttConfigStruct {
  char configVersion[5] = CONFIG_VERSION;
  char broker[100] = DEFAULT_MQTT_BROKER;
  char port[5] = DEFAULT_MQTT_PORT;
  char username[30] = DEFAULT_MQTT_USERNAME;
  char password[100] = DEFAULT_MQTT_PASSWORD;
  const char *feedCommandTopic = "saboiot/fufomilla/feed";  
  const char *eventsTopic = "saboiot/fufomilla/events";
  const char *availabilityTopic = "saboiot/fufomilla/availability";
  uint8_t jerkTime = DEFAULT_JERK_TIME;
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
  time_t tnow = time(nullptr);
  //logs.push(logRecord{tnow, code});
  Serial.println(code);
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

  if (mqtt.connect(HOSTNAME, mqttConfig.username, mqttConfig.password, mqttConfig.availabilityTopic, 0, true, "{\"status\": \"offline\"}")) {
    Serial.print("connected");
    mqtt.subscribe(mqttConfig.feedCommandTopic);
    LOG(EventCode::mqttConnected);
    char message[200];
    //char timeBuf[sizeof "2011-10-08T07:07:09Z"];
    //strftime(timeBuf, sizeof timeBuf, "%FT%TZ", localtime(&stats.upSince));
    char timeBuf[sizeof "Joi, 30 Aug 2022 09:03"];
    strftime(timeBuf, sizeof timeBuf, "%a, %e %b %Y %R", localtime(&stats.upSince));
    sprintf(message, "{\"status\": \"online\",  \"upSince\": \"%s\"}", timeBuf);
    Serial.println(message);    
    mqttPublish(mqttConfig.availabilityTopic, message, true);
  } else {
    LOG(EventCode::mqttConnectFrror);
    Serial.print("failed with state ");
    Serial.print(mqtt.state());
  }
  Serial.println("");
}

// ------------------------------------------------------------------------------------------

void _dispenseMinPortion() {
  myservo.write(180);
  delay(1000);
  myservo.write(90);
  delay(200);
  myservo.write(0);
  delay(1000);
  myservo.write(90);
}

// ------------------------------------------------------------------------------------------

void onDispenseCompleted() {  
  char message[200];
  sprintf(message, "{\"event\": \"%s\"}",
    isManualDispense ? "Manual feeding" : "Scheduled feeding"    
  );
  mqttPublish(mqttConfig.eventsTopic, message, true);
}

// ------------------------------------------------------------------------------------------

void dispense() {
  printLocalTime();
  Serial.println("Dispensing");
  _dispenseMinPortion();
  delay(200);
  _dispenseMinPortion();
  
  onDispenseCompleted();
}

// ------------------------------------------------------------------------------------------

void manualDispense() {  
  Serial.println("Manually Dispensing");
  isManualDispense = true;
  _dispenseMinPortion();
     
  onDispenseCompleted();
  isManualDispense = false;
}

// ------------------------------------------------------------------------------------------

void mqttCallback(char* topic, byte* message, unsigned int length) {
  Serial.print("Message arrived on topic: ");
  Serial.print(topic);
  Serial.println();

  // If a message is received on the topic esp32/output, you check if the message is either "on" or "off".
  // Changes the output state according to the message
  if (String(topic) == mqttConfig.feedCommandTopic) {
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

  Serial.printf("WiFi lost connection. Reason: %d\n", info.disconnected.reason);
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

void setup() {
  Serial.begin(115200);
  LOG(EventCode::deviceOn);
#ifdef DEBUG
  while (!Serial); // wait for Arduino Serial Monitor
#endif

  Serial.println("Fufomilla Feeder is starting");

  pinMode(LED_BUILTIN, OUTPUT);

  WiFi.mode(WIFI_STA); // explicitly set mode, esp defaults to STA+AP

  // Allow allocation of all timers
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);
  myservo.setPeriodHertz(50);    // standard 50 hz servo
  // using default min/max of 1000us and 2000us
  myservo.attach(SERVO_PIN, 1000, 2000);

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

  // wait until we have acquired the time
  struct tm timeinfo;
  while (!getLocalTime(&timeinfo)) {
    delay(100);
  }  

  stats.upSince = mktime(&timeinfo); 

  mqtt.setServer(mqttConfig.broker, atoi(mqttConfig.port));
  mqtt.setKeepAlive(MQTT_KEEP_ALIVE_SEC);
  mqtt.setCallback(mqttCallback);
  ensureMqttConnected();
 
  WiFi.onEvent(WiFiStationConnected, SYSTEM_EVENT_STA_CONNECTED);
  WiFi.onEvent(WiFiGotIP, SYSTEM_EVENT_STA_GOT_IP);
  WiFi.onEvent(WiFiStationDisconnected, SYSTEM_EVENT_STA_DISCONNECTED);
  
  setupOTA();

  // wifi watchdog at every 5 min
  Cron.create("0 */5 * * * *", wifiWatchdog, false);

  // setup feeeding schedule
  Cron.create("0 0 7 * * *", dispense, false); // 07:00 each day
  Cron.create("0 0 19 * * *", dispense, false); // 19:00 each day
  LOG(EventCode::deviceReady);
}

// ------------------------------------------------------------------------------------------

void printLocalTime()
{
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
}

// ------------------------------------------------------------------------------------------

void loop() {
  Cron.delay(0);
  mqtt.loop();
  ArduinoOTA.handle();
  digitalWrite(LED_BUILTIN, WiFi.status() == WL_CONNECTED);
}
