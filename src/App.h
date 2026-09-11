// App framework. Apps are built into the firmware, registered at static-init
// time (REGISTER_APP) or explicitly (AppRegistry::add), and run cooperatively
// from loop() by AppManager: each app ticks and draws at its declared rates,
// a navigation stack lets an app open a sub-app and get control back, and
// transitions are applied at the end of the loop iteration so an app can ask
// to be closed from inside its own callbacks.
//
// The Frame (drawing surface) and the input source belong to the project; the
// library only passes references through.
#pragma once

#include <Arduino.h>

class EspBase;
class AppManager;
struct Frame;  // defined by the project, never dereferenced by the library

struct InputEvent {
  enum Type : uint8_t { None = 0, Press, Release, LongPress, Turn, Touch, Custom };
  Type type = None;
  uint8_t code = 0;   // InputCode::* or project-defined (>= InputCode::User)
  int32_t value = 0;  // encoder steps, touch coordinate, ...
  uint32_t ms = 0;    // millis() when queued
};

namespace InputCode {
constexpr uint8_t None = 0;
constexpr uint8_t Back = 1;  // an unconsumed Back press pops the foreground app
constexpr uint8_t Select = 2;
constexpr uint8_t Up = 3;
constexpr uint8_t Down = 4;
constexpr uint8_t Left = 5;
constexpr uint8_t Right = 6;
constexpr uint8_t User = 32;  // first project-defined code
}  // namespace InputCode

enum AppFlags : uint8_t {
  AppFlagNone = 0,
  AppTickInBackground = 1 << 0,  // keep ticking while covered by another app
  AppOverlay = 1 << 1,           // draws on top of the app below it (dialogs, toasts)
  AppHidden = 1 << 2,            // service: opened at start, ticks, never on the stack, never draws
};

struct AppInfo {
  const char* id = "";    // stable identifier used by the console, the API and the boot_app key
  const char* name = "";  // human readable
  uint16_t tickHz = 0;    // 0 = every loop iteration
  uint16_t drawHz = 0;    // cap on redraws; 0 = whenever requestDraw() was called
  int16_t order = 100;    // registry order, for launchers
  uint8_t flags = AppFlagNone;
};

class App {
 public:
  explicit App(const AppInfo& info) : _info(info) {}
  virtual ~App() = default;

  const AppInfo& info() const { return _info; }
  const char* id() const { return _info.id; }
  bool hasFlag(uint8_t flag) const { return (_info.flags & flag) != 0; }

  // Lifecycle, all on the loop task.
  virtual void begin(EspBase&) {}      // once, from AppManager::start(); register console commands here
  virtual void open(const String&) {}  // now foreground (services: once at start)
  virtual void pause() {}              // covered by a pushed app
  virtual void resume(int) {}          // the app above closed; its result
  virtual void close() {}              // popped or replaced
  virtual void tick(uint32_t) {}       // milliseconds since the previous tick
  virtual void draw(Frame&) {}
  virtual bool input(const InputEvent&) { return false; }  // true = consumed

  // For apps.
  void requestDraw() { _dirty = true; }
  bool isOpen() const { return _open; }
  bool isForeground() const;
  AppManager& manager() const { return *_mgr; }  // valid from begin() on

  struct Stats {
    uint32_t ticks = 0;
    uint32_t draws = 0;
    uint32_t overruns = 0;
    uint32_t tickLastUs = 0;
    uint32_t tickMaxUs = 0;
    uint32_t drawLastUs = 0;
    uint32_t drawMaxUs = 0;
  };
  const Stats& stats() const { return _stats; }

 private:
  friend class AppManager;
  friend class AppRegistry;

  AppInfo _info;
  App* _next = nullptr;
  AppManager* _mgr = nullptr;
  bool _open = false;
  volatile bool _dirty = true;
  uint32_t _nextTickAt = 0;
  uint32_t _lastTickAt = 0;
  uint32_t _nextDrawAt = 0;
  uint32_t _lastOverrunLogAt = 0;
  Stats _stats;
};

// Intrusive registry: no heap, no dependency on static initialisation order.
// Apps are sorted by (order, id) the first time the list is read.
class AppRegistry {
 public:
  static void add(App& app);  // safe at static-init time; double adds are ignored
  static size_t count();
  static App* find(const char* id);
  static App* first();
  static App* next(const App& app) { return app._next; }

 private:
  static App*& head();
  static bool& unsorted();
  static void sortIfNeeded();
};

class AppRegistrar {
 public:
  explicit AppRegistrar(App& app) { AppRegistry::add(app); }
};

// REGISTER_APP(ClockApp);            -> static ClockApp instance, registered
// REGISTER_APP(MenuApp, "main", 4);  -> constructor arguments
// Works for types in the global namespace; otherwise write the two lines by hand.
#define REGISTER_APP(Type, ...)                     \
  static Type Type##_appInstance{__VA_ARGS__};      \
  static AppRegistrar Type##_appRegistrar(Type##_appInstance)
