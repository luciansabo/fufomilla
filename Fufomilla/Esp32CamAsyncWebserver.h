/*
  This is a simple MJPEG streaming webserver implemented for AI-Thinker ESP32-CAM
  and ESP-EYE modules.
  This is tested to work with VLC and Blynk video widget and can support up to 10
  simultaneously connected streaming clients.
  Simultaneous streaming is implemented with dedicated FreeRTOS tasks.

  Usage:
  Esp32CamWebserver camServer;
  camServer.start(<esp32cam_aithinker_config>);

  in loop(): camServer.run();
  
  Originally inspired from https://github.com/arkhipenko/esp32-mjpeg-multiclient-espcam-drivers/blob/master/esp32-cam-rtos/esp32-cam-rtos.ino  
  Which in turn is inspired by and based on this Instructable: $9 RTSP Video Streamer Using the ESP32-CAM Board
  (https://www.instructables.com/id/9-RTSP-Video-Streamer-Using-the-ESP32-CAM-Board/)

  The code was converted to an OOP style for better encapsulation and reuse.

  Board: AI-Thinker ESP32-CAM or ESP-EYE
  Compile as:
   ESP32 Dev Module
   CPU Freq: 240
   Flash Freq: 80
   Flash mode: QIO
   Flash Size: 4Mb
   Partrition: Minimal SPIFFS
   PSRAM: Disabled -> note the original code uses PSRAM but I disabled it for compatibility reasons as I didn't need a high resolution
*/

#ifndef ESP32CAMWEBSERVER_H_
#define ESP32CAMWEBSERVER_H_

#include <WebServer.h>
#include <WiFiClient.h>
#include "esp_camera.h"
#include <driver/rtc_io.h>

// ESP32 has two cores: APPlication core and PROcess core (the one that runs ESP32 SDK stack)
#define MAX_CLIENTS 3

class Esp32CamWebserver {
public:
  Esp32CamWebserver() {
    this->webserver = new WebServer(80);
  };
  ~Esp32CamWebserver() {
    delete this->webserver;
  };

  WebServer* start(camera_config_t cameraConfig);
  void run(void);
  void disable(void);
  void enable(void);

protected:
  WebServer* webserver;

private:
  static char* allocateMemory(char* aPtr, size_t aSize);
  static void mjpegCB(void* pvParameters);
  static void camCB(void* pvParameters);
  static void streamCB(void* pvParameters);
  void handleJpg(void);
  void handleMjpeg(void);
  void handleNotFound(void);

  camera_config_t _camConfig;

  // ===== rtos task handles =========================
  // Streaming is implemented with 3 tasks:
  TaskHandle_t tMjpeg;  // handles client connections to the webserver
  TaskHandle_t tCam;    // handles getting picture frames from the camera and storing them locally

  volatile uint32_t frameNumber;
  volatile size_t camSize;  // size of the current frame, byte
  volatile char* camBuf;    // pointer to the current frame
  uint8_t noActiveClients;  // number of active clients
  // frameSync semaphore is used to prevent streaming buffer as it is replaced with the next frame
  SemaphoreHandle_t frameSync = NULL;

  static const int APP_CPU = 1;
  static const int PRO_CPU = 0;
  // We will try to achieve 20 FPS frame rate
  static const int FPS = 20;
  // We will handle web client requests every 100 ms (10 Hz)
  static const int WSINTERVAL = 100;

  // ==== STREAMING ======================================================
  static constexpr char* HEADER = "HTTP/1.1 200 OK\r\n"
                                  "Access-Control-Allow-Origin: *\r\n"
                                  "Content-Type: multipart/x-mixed-replace; boundary=123456789000000000000987654321\r\n";
  static constexpr char* BOUNDARY = "\r\n--123456789000000000000987654321\r\n";
  static constexpr char* CTNTTYPE = "Content-Type: image/jpeg\r\nContent-Length: ";
  static const int hdrLen = strlen(HEADER);
  static const int bdrLen = strlen(BOUNDARY);
  static const int cntLen = strlen(CTNTTYPE);

  static constexpr char* JHEADER = "HTTP/1.1 200 OK\r\n"
                                   "Content-disposition: inline; filename=capture.jpg\r\n"
                                   "Content-type: image/jpeg\r\n\r\n";
  static const int jhdLen = strlen(JHEADER);
};


struct streamInfo {
  uint32_t frame;
  Esp32CamWebserver* camServer;
  WiFiClient client;
  TaskHandle_t task;
  char* buffer;
  size_t len;
};

#endif
