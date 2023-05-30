#include "Esp32CamAsyncWebserver.h"
    
// ==== Memory allocator that takes advantage of PSRAM if present =======================
char* Esp32CamWebserver::allocateMemory(char* aPtr, size_t aSize) {

  //  Since current buffer is too smal, free it
  if (aPtr != NULL) free(aPtr);

  char* ptr = NULL;
  ptr = (char*) malloc(aSize); // use ps_malloc if PSRAM is enabled

  // If the memory pointer is NULL, we were not able to allocate any memory, and that is a terminal condition.
  if (ptr == NULL) {
    log_e("Out of memory!");
    delay(5000);
    ESP.restart();
  }
  return ptr;
}

// ==== RTOS task to grab frames from the camera =========================
void Esp32CamWebserver::camCB(void* pvParameters) {

  TickType_t xLastWakeTime;
  Esp32CamWebserver* camServer = (Esp32CamWebserver *) pvParameters;

  //  A running interval associated with currently desired frame rate
  const TickType_t xFrequency = pdMS_TO_TICKS(1000 / Esp32CamWebserver::FPS);

  //  Pointers to the 2 frames, their respective sizes and index of the current frame
  char* fbs[2] = { NULL, NULL };
  size_t fSize[2] = { 0, 0 };
  int ifb = 0;
  camServer->frameNumber = 0;

  //=== loop() section  ===================
  xLastWakeTime = xTaskGetTickCount();

  for (;;) {

    //  Grab a frame from the camera and query its size
    camera_fb_t* fb = NULL;

    fb = esp_camera_fb_get();
    size_t s = fb->len;

    //  If frame size is more that we have previously allocated - request  125% of the current frame space
    if (s > fSize[ifb]) {
      fSize[ifb] = s + s;
      fbs[ifb] = Esp32CamWebserver::allocateMemory(fbs[ifb], fSize[ifb]);
    }

    //  Copy current frame into local buffer    
    memcpy(fbs[ifb], (char *)fb->buf, s);
    esp_camera_fb_return(fb);

    //  Let other tasks run and wait until the end of the current frame rate interval (if any time left)
    taskYIELD();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);

    //  Only switch frames around if no frame is currently being streamed to a client
    //  Wait on a semaphore until client operation completes
    //    xSemaphoreTake( camServer->frameSync, portMAX_DELAY );

    //  Do not allow frame copying while switching the current frame
    xSemaphoreTake( camServer->frameSync, xFrequency );
    camServer->camBuf = fbs[ifb];
    camServer->camSize = s;
    ifb++;
    ifb &= 1;  // this should produce 1, 0, 1, 0, 1 ... sequence
    camServer->frameNumber++;
    //  Let anyone waiting for a frame know that the frame is ready
    xSemaphoreGive( camServer->frameSync );

    //  Immediately let other (streaming) tasks run
    taskYIELD();

    //  If streaming task has suspended itself (no active clients to stream to)
    //  there is no need to grab frames from the camera. We can save some juice
    //  by suspedning the tasks
    if ( camServer->noActiveClients == 0 ) {
      vTaskSuspend(NULL);  // passing NULL means "suspend yourself"
    }
  }
}


// ======== Server Connection Handler Task ==========================
void Esp32CamWebserver::mjpegCB(void* pvParameters) {

  Esp32CamWebserver* camServer = (Esp32CamWebserver *) pvParameters;
  TickType_t xLastWakeTime;
  const TickType_t xFrequency = pdMS_TO_TICKS(Esp32CamWebserver::WSINTERVAL);

  // Creating frame synchronization semaphore and initializing it
  camServer->frameSync = xSemaphoreCreateBinary();
  xSemaphoreGive( camServer->frameSync );

  //=== setup section  ==================

  //  Creating RTOS task for grabbing frames from the camera
  int rc = xTaskCreatePinnedToCore(
    camServer->camCB,        // callback
    "cam",        // name
    6 * 1024,       // stack size
    (void *)camServer,         // parameters
    2,            // priority
    &camServer->tCam,        // RTOS task handle
    Esp32CamWebserver::PRO_CPU);     // core

  if ( rc != pdPASS ) {
    log_e("mjpegCB: error creating RTOS task cam. rc = %d\n", rc);
    log_i("handleJPGSstream: free heap  : %d\n", ESP.getFreeHeap());    
    vTaskDelete(NULL);
    return;
  }

  camServer->noActiveClients = 0;

  log_i("\nmjpegCB: free heap (start)  : %d\n", ESP.getFreeHeap());
  //=== loop() section  ===================
  xLastWakeTime = xTaskGetTickCount();
  for (;;) {
    camServer->webserver->handleClient();

    //  After every server client handling request, we let other tasks run and then pause
    taskYIELD();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

// ==== Actually stream content to all connected clients ========================
void Esp32CamWebserver::streamCB(void * pvParameters) {
  char buf[16];
  TickType_t xLastWakeTime;
  TickType_t xFrequency;

  streamInfo* info = (streamInfo*) pvParameters;
  WiFiClient client = info->client;  

  //  Immediately send this client a header
  client.write(Esp32CamWebserver::HEADER, Esp32CamWebserver::hdrLen);
  client.write(Esp32CamWebserver::BOUNDARY, Esp32CamWebserver::bdrLen);
  taskYIELD();

  xLastWakeTime = xTaskGetTickCount();
  xFrequency = pdMS_TO_TICKS(1000 / Esp32CamWebserver::FPS);

  for (;;) {
    //  Only bother to send anything if there is someone watching
    if ( client.connected() ) {

      if ( info->frame != info->camServer->frameNumber) {
        xSemaphoreTake( info->camServer->frameSync, portMAX_DELAY );
        if ( info->buffer == NULL ) {
          info->buffer = allocateMemory (info->buffer, info->camServer->camSize);
          info->len = info->camServer->camSize;
        }
        else {
          if ( info->camServer->camSize > info->len ) {
            info->buffer = allocateMemory (info->buffer, info->camServer->camSize);
            info->len = info->camServer->camSize;
          }
        }
        memcpy(info->buffer, (const void*) info->camServer->camBuf, info->len);
        xSemaphoreGive( info->camServer->frameSync );
        taskYIELD();

        info->frame = info->camServer->frameNumber;
        client.write(Esp32CamWebserver::CTNTTYPE, Esp32CamWebserver::cntLen);
        sprintf(buf, "%d\r\n\r\n", info->len);
        client.write(buf, strlen(buf));
        client.write((char*) info->buffer, (size_t)info->len);
        client.write(Esp32CamWebserver::BOUNDARY, Esp32CamWebserver::bdrLen);
        client.flush();
      }
    }
    else {
      //  client disconnected - clean up.
      info->camServer->noActiveClients--;
      log_i("streamCB: Stream Task stack wtrmark  : %d\n", uxTaskGetStackHighWaterMark(info->task));      
      client.flush();
      client.stop();
      if ( info->buffer ) {
        free( info->buffer );
        info->buffer = NULL;
      }
      delete info;
      info = NULL;
      vTaskDelete(NULL);
    }
    //  Let other tasks run after serving every client
    taskYIELD();
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
  }
}

WebServer* Esp32CamWebserver::start(camera_config_t cameraConfig) {
    memset(&_camConfig, 0, sizeof(_camConfig));
    memcpy(&_camConfig, &cameraConfig, sizeof(cameraConfig));

    esp_err_t err = esp_camera_init(&_camConfig);
    if (err != ESP_OK) {
        return NULL;
    }

    log_i("Camera was iniitalized");
    
    // Start mainstreaming RTOS task
    int rc = xTaskCreatePinnedToCore(
      this->mjpegCB,
      "mjpeg",
      4 * 1024,
      this,
      2,
      &this->tMjpeg,
      Esp32CamWebserver::APP_CPU
    );  

  if ( rc != pdPASS ) {
    log_e("start: error creating RTOS task mjpegCB. rc = %d\n", rc);
    log_i("handleJPGSstream: free heap  : %d\n", ESP.getFreeHeap());    
  }
      
    this->webserver->on("/mjpeg/1", HTTP_GET, std::bind(&Esp32CamWebserver::handleMjpeg, this));
    this->webserver->on("/jpg", HTTP_GET, std::bind(&Esp32CamWebserver::handleJpg, this));
    this->webserver->onNotFound(std::bind(&Esp32CamWebserver::handleNotFound, this));
    this->webserver->begin();

    return this->webserver;  
}

// ==== Handle connection request from clients ===============================
void Esp32CamWebserver::handleMjpeg(void) {
  if ( this->noActiveClients >= MAX_CLIENTS ) return;
  log_i("handleMjpeg start: free heap  : %d\n", ESP.getFreeHeap());

  streamInfo* info = new streamInfo;

  info->frame = this->frameNumber - 1;
  info->camServer = this;
  info->client = this->webserver->client();
  info->buffer = NULL;
  info->len = 0;

  //  Creating task to push the stream to all connected clients
  int rc = xTaskCreatePinnedToCore(
             this->streamCB,
             "strmCB",
             4 * 1024,
             (void*) info,
             2,
             &info->task,
             Esp32CamWebserver::APP_CPU);
  if ( rc != pdPASS ) {
    log_e("handleMjpeg: error creating RTOS task streamCB. rc = %d\n", rc);
    log_i("handleMjpeg: free heap  : %d\n", ESP.getFreeHeap());
    //    Serial.printf("stk high wm: %d\n", uxTaskGetStackHighWaterMark(tSend));
    delete info;
  }

  this->noActiveClients++;

  // Wake up streaming tasks, if they were previously suspended:
  if ( eTaskGetState( this->tCam ) == eSuspended ) vTaskResume( this->tCam );
}

// ==== Serve up one JPEG frame =============================================
void Esp32CamWebserver::handleJpg(void)
{
  WiFiClient client = this->webserver->client();

  if (!client.connected()) return;
  camera_fb_t* fb = esp_camera_fb_get();
  client.write(JHEADER, jhdLen);
  client.write((char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
}

void Esp32CamWebserver::handleNotFound(void)
{    
  this->webserver->send(404, "text/plain", "404: Not found"); // Send HTTP status 404 (Not Found) when there's no handler for the URI in the request
}

void Esp32CamWebserver::run(void) {
  // this seems to be necessary to let IDLE task run and do GC
  vTaskDelay(1000);
}

void Esp32CamWebserver::disable(void) {
  this->webserver->stop();
}

void Esp32CamWebserver::enable(void) {
  this->webserver->begin();
}
