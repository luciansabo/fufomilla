## Fufomilla

Automated Cat/Dog/Pet Feeder

*Created for ESP32/ESP32 CAM, but should work with minimal changes with other boards*

### Features
- based on a popular manual cereal dispenser + 3d printed parts 
- Wifi control/monitoring and automatic reconnects
- configuration portal (Wifi manager) on double reset
- supports ESP32 CAM (tested on the AI Thinker version) with 3 simultaneous camera streams and 20 fps, should work on any ESP32 though
- MQTT communication (can be easily integrated with Home Assistant) with integrated sensors like status, food level, remaining portions
- supports an optional ultrasonic food level sensor
- scheduled feeding based on NTP time and optionally a real time clock module
- manual feeding possible through MQTT a command
