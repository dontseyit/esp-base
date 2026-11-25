// Minimal EspBase consumer. Builds for every env in platformio.ini.
//
// Serial: the project owns Serial (and any USB CDC flags); the library only
// logs through cfg.logOutput. Commands typed on Serial are passed to the same
// registry the web console uses.
#include <Arduino.h>
#include <EspBase.h>

#include "Frame.h"

// The project supplies pins. Pass -DLED_PIN=<gpio> in build_flags to use one.
#ifndef LED_PIN
#define LED_PIN -1
#endif

static EspBaseConfig cfg;
static EspBase base;

#ifndef ESPBASE_NO_APPS
static Frame frame;

// "Display driver" for the example: log the frame whenever it changed.
static void presentFrame(Frame& f) {
  static Frame shown;
  if (f.equals(shown)) {
    return;
  }
  shown = f;
  LOG_I("frame: |%s|%s|%s|%s|", f.cell[0], f.cell[1], f.cell[2], f.cell[3]);
}
#endif

static void ledCommand(const CmdArgs& args, Print& out) {
  if (LED_PIN < 0) {
    out.println("no LED configured (build with -DLED_PIN=<gpio>)");
    return;
  }
  if (args.is(0, "on") || args.is(0, "off")) {
    digitalWrite(LED_PIN, args.is(0, "on") ? HIGH : LOW);
    base.status().set("led", args.is(0, "on") ? "on" : "off");
    out.printf("led %s\r\n", args[0].c_str());
  } else {
    out.println("usage: led <on|off>");
  }
}

// Reads command lines from Serial without blocking.
static void pollSerialCommands() {
  static String line;
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\n' || c == '\r') {
      if (!line.isEmpty()) {
        base.console().execute(line, Serial);
        line = "";
      }
    } else if (line.length() < 200) {
      line += c;
    }
  }
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  // USB CDC blocks each write for up to 100 ms while no host is attached;
  // without this every LOG_x would stall the loop on an unplugged board.
  Serial.setTxTimeoutMs(0);
#endif

  cfg.hostname = "esp-base-demo";   // also the mDNS name: esp-base-demo.local
  cfg.apSsidPrefix = "EspBase";     // AP SSID = EspBase-XXXX (last 4 hex of the MAC)
  cfg.apPassword = "";              // empty = open AP
  cfg.staTimeoutMs = 20000;         // STA attempt before AP fallback
  cfg.statusLedPin = LED_PIN;       // -1 = none
  cfg.httpPort = 80;
  cfg.otaEnabled = true;
  cfg.otaPassword = "";             // set one before shipping
  cfg.logOutput = &Serial;          // serial sink for LOG_x
  cfg.appPage = "/app/";            // the page below, linked from the tab bar
  cfg.appPageTitle = "Demo";

  base.begin(cfg);

  // Values and buttons of the project's own, in the page's Device tab. The
  // buttons run console commands; the values are set from loop() and ledCommand().
  base.status().add("uptime", "Uptime");
  base.status().add("heap", "Free heap");
  base.status().add("led", "LED");
  base.status().addButton("LED on", "led on");
  base.status().addButton("LED off", "led off");
  base.status().set("led", LED_PIN < 0 ? "none (build with -DLED_PIN=<gpio>)" : "off");

  // A page of the project's own. Everything under /app/ gets the console's
  // login and origin check, like /api/.
  base.web().on("/app/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "text/html",
                  "<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width'><title>Demo</title>"
                  "<h1>esp-base demo</h1><p>A page of the project's own, served from <code>/app/</code>.</p>"
                  "<p><a href='/api/state'>/api/state</a> &middot; <a href='/'>back to the console</a></p>");
  });

  // A setting of the project's own. It shows in the page's Settings tab and
  // in the "settings" and "config" commands.
  static const SettingDef kGreeting = {.key = "greeting", .label = "Greeting", .def = "hello", .hint = "any text", .maxLen = 20};
  base.settings().add(kGreeting);

  base.console().addCommand("led", ledCommand, "led <on|off>");

  base.web().on("/api/state", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(200, "application/json", "{\"led\":" + String(LED_PIN >= 0 && digitalRead(LED_PIN) ? "true" : "false") + "}");
  });

  base.wifi().onConnected([]() { LOG_I("app: network up, http://%s.local/", base.wifi().hostname().c_str()); });
  base.wifi().onApStarted([]() { LOG_I("app: join \"%s\" and open http://%s/", base.wifi().apSsid().c_str(), base.wifi().apIP().toString().c_str()); });

  LOG_I("app: %s with %d core(s), ble=%d, usb-cdc=%d, heap %lu", ChipInfo::name(), ChipInfo::cores(), ChipInfo::hasBle(), ChipInfo::hasUsbCdc(),
        static_cast<unsigned long>(ESP.getFreeHeap()));

#ifndef ESPBASE_NO_APPS
  // Apps (see apps.cpp). Drive them with "app key up|down|select|back" from
  // the console; a real project feeds base.apps().input() from its buttons.
  base.apps().setIdleTimeout(120000);  // back to the launcher after 2 min without input
  base.apps().start(&frame, presentFrame, "home");
#endif
}

void loop() {
  base.loop();
  pollSerialCommands();

  static uint32_t lastStatus = 0;
  if (millis() - lastStatus >= 1000) {
    lastStatus = millis();
    base.status().set("uptime", String(millis() / 1000) + " s");
    base.status().set("heap", String(ESP.getFreeHeap() / 1024) + " KB");
  }

  static uint32_t lastTick = 0;
  if (millis() - lastTick >= 60000) {
    lastTick = millis();
    LOG_D("app: uptime %lu s, heap %lu", static_cast<unsigned long>(millis() / 1000), static_cast<unsigned long>(ESP.getFreeHeap()));
  }
}
