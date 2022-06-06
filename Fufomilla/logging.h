enum EventCode {
  deviceOn,
  deviceReady,
  mqttDisconnected,
  mqttConnected,
  mqttConnectFrror,
  mqttPublishError,
  manualFeedingCompleted,
  scheduledFeedingCompleted,
};

typedef struct {
  time_t time;
  EventCode code;    
} logRecord;
