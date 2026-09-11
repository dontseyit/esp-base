// Example apps: a launcher (root), a clock, an overlay dialog and a hidden
// service. Drive them from the console with "app key up|down|select|back".
#include <EspBase.h>

#include "Frame.h"

// ---- Hidden service: ticks at 1 Hz, never draws, never on the stack ----------

class HeartbeatService : public App {
 public:
  HeartbeatService() : App({.id = "heartbeat", .name = "Heartbeat", .tickHz = 1, .order = 1000, .flags = AppHidden}) {}
  void tick(uint32_t) override { seconds++; }
  uint32_t seconds = 0;
};
REGISTER_APP(HeartbeatService);

// ---- Overlay dialog: draws over the app below, returns a result on close ------

class ConfirmApp : public App {
 public:
  ConfirmApp() : App({.id = "confirm", .name = "Confirm", .order = 900, .flags = AppOverlay}) {}

  void open(const String& args) override {
    _question = args.isEmpty() ? "Sure?" : args;
    requestDraw();
  }
  bool input(const InputEvent& ev) override {
    if (ev.type != InputEvent::Press) {
      return false;
    }
    if (ev.code == InputCode::Select) {
      manager().pop(1);  // applied at the end of this loop iteration
      return true;
    }
    if (ev.code == InputCode::Back) {
      manager().pop(0);
      return true;
    }
    return false;
  }
  void draw(Frame& f) override {
    f.textf(1, 0, "[%-18.18s]", _question.c_str());
    f.text(2, 0, "[select=yes back=no]");
  }

 private:
  String _question;
};
REGISTER_APP(ConfirmApp);

// ---- Clock: redraws once a second, opens the dialog on select ----------------

class ClockApp : public App {
 public:
  ClockApp() : App({.id = "clock", .name = "Clock", .tickHz = 1, .drawHz = 2, .order = 10}) {}

  void begin(EspBase& base) override {
    _base = &base;
    _heartbeat = static_cast<HeartbeatService*>(AppRegistry::find("heartbeat"));
    base.console().addCommand(
        "clock", [this](const CmdArgs&, Print& out) { out.printf("uptime %lu s, heartbeat %lu\r\n", static_cast<unsigned long>(millis() / 1000),
                                                                 static_cast<unsigned long>(_heartbeat ? _heartbeat->seconds : 0)); },
        "clock app: show uptime");
  }
  void open(const String&) override { requestDraw(); }
  void resume(int result) override {
    requestDraw();
    if (result == 1) {
      LOG_I("clock: reboot confirmed");
      _base->reboot(500);
    }
  }
  void tick(uint32_t) override { requestDraw(); }
  bool input(const InputEvent& ev) override {
    if (ev.type == InputEvent::Press && ev.code == InputCode::Select) {
      manager().push("confirm", "Reboot now?");
      return true;
    }
    return false;  // Back is handled by the manager (pop)
  }
  void draw(Frame& f) override {
    const uint32_t s = millis() / 1000;
    f.clear();
    f.textf(0, 0, "Up %02lu:%02lu:%02lu", static_cast<unsigned long>(s / 3600), static_cast<unsigned long>((s / 60) % 60),
            static_cast<unsigned long>(s % 60));
    f.textf(1, 0, "Beat %lu", static_cast<unsigned long>(_heartbeat ? _heartbeat->seconds : 0));
    f.textf(2, 0, "%.20s", _base->wifi().isConnected() ? _base->wifi().localIP().toString().c_str() : _base->wifi().stateName());
    f.text(3, 0, "select: reboot");
  }

 private:
  EspBase* _base = nullptr;
  HeartbeatService* _heartbeat = nullptr;
};
REGISTER_APP(ClockApp);

// ---- Launcher: the root app, lists the visible apps ----------------------------

class HomeApp : public App {
 public:
  HomeApp() : App({.id = "home", .name = "Home", .order = 0}) {}

  void open(const String&) override { requestDraw(); }
  void resume(int) override { requestDraw(); }
  bool input(const InputEvent& ev) override {
    if (ev.type != InputEvent::Press) {
      return false;
    }
    const int n = count();
    if (ev.code == InputCode::Up && _sel > 0) {
      _sel--;
    } else if (ev.code == InputCode::Down && _sel + 1 < n) {
      _sel++;
    } else if (ev.code == InputCode::Select) {
      const App* a = at(_sel);
      if (a != nullptr) {
        manager().push(a->id());
      }
    } else {
      return false;
    }
    requestDraw();
    return true;
  }
  void draw(Frame& f) override {
    f.clear();
    f.text(0, 0, "esp-base apps");
    const int n = count();
    const int first = _sel >= Frame::H - 1 ? _sel - (Frame::H - 2) : 0;
    for (int i = first, row = 1; i < n && row < Frame::H; ++i, ++row) {
      f.textf(row, 0, "%c %s", i == _sel ? '>' : ' ', at(i)->info().name);
    }
  }

 private:
  // Visible, launchable apps: not this one, not services, not overlays.
  static bool launchable(const App& a) { return !a.hasFlag(AppHidden) && !a.hasFlag(AppOverlay) && strcmp(a.id(), "home") != 0; }
  static int count() {
    int n = 0;
    for (const App* a = AppRegistry::first(); a != nullptr; a = AppRegistry::next(*a)) {
      n += launchable(*a) ? 1 : 0;
    }
    return n;
  }
  static const App* at(int index) {
    for (const App* a = AppRegistry::first(); a != nullptr; a = AppRegistry::next(*a)) {
      if (launchable(*a) && index-- == 0) {
        return a;
      }
    }
    return nullptr;
  }
  int _sel = 0;
};
REGISTER_APP(HomeApp);
