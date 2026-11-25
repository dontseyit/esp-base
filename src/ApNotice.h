// When to tell the project that the access point came up. Pure, so it is
// tested on the host (test/test_ap_notice).
//
// One notice for each time the library brings the AP up. The driver's own
// events are not enough to go by: hosted WiFi (a second chip) restarts the AP
// while softAP() configures it and sends start, stop, start.
#pragma once

struct ApNotice {
  bool active = false;     // the driver says the AP is up
  bool announced = false;  // the notice was given since the library last took the AP down

  void started() { active = true; }  // the driver's start event
  void stopped() { active = false; }  // the driver's stop event: may be half of a restart
  void takenDown() {                  // the library stopped the AP or the radio
    active = false;
    announced = false;
  }
  // True once for each AP the library brought up. Ask after handling events.
  bool due() {
    if (!active || announced) {
      return false;
    }
    announced = true;
    return true;
  }
};
