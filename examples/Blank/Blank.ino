// A fresh board on esp-base: WiFi with AP fallback and captive portal, settings
// in NVS, the console on Serial and in the browser, OTA, logging and NTP time.
// Nothing project specific yet. Copy this folder, pick a board in
// platformio.ini (or open it in the Arduino IDE), flash, join the AP.
#include <EspBase.h>

using namespace espbase;

EspBaseConfig cfg;
EspBase base;

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);  // do not stall when no USB host is attached
#endif
  cfg.hostname = "my-device";     // http://my-device.local/
  cfg.apSsidPrefix = "MyDevice";  // fallback AP "MyDevice-XXXX"
  cfg.logOutput = &Serial;        // serial sink for LOG_x
  // cfg.webPassword = "<8+ chars>";  // console password (user "admin"); set before shipping
  base.begin(cfg);  // returns at once; the network comes up from loop()
}

void loop() {
  base.loop();
  base.console().poll(Serial);  // "help" on the serial port lists the commands
}
