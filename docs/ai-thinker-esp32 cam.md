## INTRODUCTION TO AL-CAM THINKER

Source: https://www.electrorules.com/esp32-cam-ai-thinker-pinout-guide-gpios-usage-explained/

For quite some time, the world has revolved around IoTs, or Internet of Things. Everyone is interested in Smart technology, from enthusiasts to innovators, in building various prototypes and goods, and launching them on the market. One of the most common clever modules for IoTs is the ESP32 Series processor. Espressif’s ESP32-CAM AI-Thinker is an enhanced version of the ESP8266-01 with numerous functionalities. Two high-performance 32-bit LX6 CPUs with a 7-stage pipeline architecture are included in the ultra-small, low-power module.

## Applications
ESP32-CAM AI-Thinker module has a never-ending list of applications like home automation, intelligent devices, positioning systems, security systems and perfect for IoT applications.

## POWER PINS
The ESP32-CAM has three GND pins (coloured black) and two power pins (coloured red): 3.3V and 5V, respectively.

The ESP32-CAM can be powered from the 3.3V or 5V pins. However, many individuals have noticed issues while using 3.3V to power the ESP32-CAM, thus we always recommend using the 5V pin.

## POWER OUTPUT PIN
There’s also the VCC pin, which is labelled on the silkscreen (colored with a yellow rectangle). That pin should not be used to power the ESP32-CAM. That is a power output pin. It may produce either 5V or 3.3V.

Whether supplied with 5V or 3.3V, the ESP32-CAM outputs 3.3V in our situation. Two pads are located next to the VCC pin. One is labelled 3.3V and the other is labelled 5V.

## SERIAL PIN
GPIO 1 and GPIO 3 are the serial pins (TX and RX, respectively). Because the ESP32-CAM doesn’t have a built-in programmer, you need to use these pins to communicate with the board and upload code.

You can use GPIO 1 and GPIO 3 to connect other peripherals like outputs or sensors after uploading the code. However, you won’t be able to open the Serial Monitor and see if everything is going well with your setup.

GPIO 0
GPIO 0 determines whether the ESP32 is in flashing mode or not. This GPIO is internally connected to a pull-up 10k Ohm resistor.

When GPIO 0 is connected to GND, the ESP32 goes into flashing mode and you can upload code to the board.

GPIO 0 connected to GND » ESP32-CAM in flashing mode
To make the ESP32 run “normally”, you just need to disconnect GPIO 0 from GND.

## MICROSD CONNECTION
The following pins are used to interface with the microSD card when it is on operation.

Microsd card	ESP32
CLK	GPIO 14
CMD	GPIO15
DATA0	GPIO 2
DATA 1/flashlight	GPIO 4
DATA2	GPIO 12
DATA3	GPIO13

## FLASHLIGHT(GPIO4)
The ESP32-CAM has a very bright built-in LED that can work as a flash when taking

photos. That LED is internally connected to GPIO 4.

That GPIO is also connected to the microSD card slot, so you may have troubles when trying to use both at the same time – the flashlight will light up when using the microSD card.

## GPIO 33 – BUILT- IN RED LIGHT
An on-board red LED is located next to the RST button. Internally, its LED is coupled to GPIO 33. This LED can be used to signal that something is happening. When Wi-Fi is enabled, for example, the LED turns red, and vice versa.

That LED uses inverted logic, thus to turn it on, send a LOW signal, and to turn it off, provide a HIGH signal.

```
void setup() {

  pinMode(33, OUTPUT);

}

void loop() {

  digitalWrite(33, LOW);

}
```

## CAMERA CONNECTIONS
The connections between the camera and the ESP32-CAM AI-Thinker are shown in the following table.

OV2640 CAMERA	ESP32	Variable name in code
DO	GPIO 5	Y2_GPIO_NUM
D1	GPIO 18	Y3_GPIO_NUM
D2	GPIO 19	Y4_GPIO_NUM
D3	GPIO 21	Y5_GPIO_NUM
D4	GPIO 36	Y6_GPIO_NUM
D5	GPIO 39	Y7_GPIO_NUM
D6	GPIO 34	Y8_GPIO_NUM
D7	GPIO 35	Y9_GPIO_NUM
XCLK	GPIO 0	XCLK_GPIO_NUM
PCLK	GPIO 22	PCLK_GPIO_NUM
VSYNC	GPIO 25	VSYNC_GPIO_NUM
HREF	GPIO 23	HREF_GPIO_NUM
SDA	GPIO 26	SIOD_GPIO_NUM
SCL	GPIO 27	SIOC_GPIO_NUM
POWERPIN	GPIO 32	PWDN_GPIO_NUM

