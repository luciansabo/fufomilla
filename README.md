## Fufomilla

Automated Cat/Dog/Pet Feeder

*Created for ESP32, but should work with minimal changes with other boards*

This is a work in progress and for now only scheduled feeding is implemented

### Features
- based on a popular manual cereal dispenser + 3d printed parts
- Wifi control/monitoring
- supports ESP32 CAM (tested on the AI Thinker version) with 3 simultaneous streams and 20 fps, should work on any ESP32 though
- MQTT communication (can be easily integrated with Home Assistant)
- Wifi connection monitoring and automatic reconnects
- configuration portal (Wifi manager) on double reset
- scheduled feeding based on NTP time and optionally a real time clock module
- support an optional ultrasonic food level sensor
