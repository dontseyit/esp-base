#include "AppManager.h"

#include <AsyncJson.h>
#include <string.h>

#include <algorithm>

#include "EspBase.h"

namespace {
constexpr uint32_t kOverrunLogIntervalMs = 10000;
constexpr size_t kInputQueue = 16;

const char* keyName(uint8_t code) {
  switch (code) {
    case InputCode::Back: return "back";
    case InputCode::Select: return "select";
    case InputCode::Up: return "up";
    case InputCode::Down: return "down";
    case InputCode::Left: return "left";
    case InputCode::Right: return "right";
    default: return nullptr;
  }
}

bool parseKey(const String& text, uint8_t& code) {
  for (uint8_t c = InputCode::Back; c <= InputCode::Right; ++c) {
    if (text.equalsIgnoreCase(keyName(c))) {
      code = c;
      return true;
    }
  }
  char* end = nullptr;
  const long v = strtol(text.c_str(), &end, 10);
  if (end == nullptr || *end != '\0' || v < 0 || v > 255) {
    return false;
  }
  code = static_cast<uint8_t>(v);
  return true;
}

String joinFrom(const CmdArgs& a, size_t from) {
  String s;
  for (size_t i = from; i < a.size(); ++i) {
    if (i > from) {
      s += ' ';
    }
    s += a[i];
  }
  return s;
}
}  // namespace

bool App::isForeground() const {
  return _mgr != nullptr && _mgr->foreground() == this;
}

// ---- Setup ---------------------------------------------------------------------

void AppManager::attach(EspBase& base) {
  _base = &base;
  if (_mutex == nullptr) {
    _mutex = xSemaphoreCreateMutex();
  }
  if (_inputs == nullptr) {
    _inputs = xQueueCreate(kInputQueue, sizeof(InputEvent));
  }

  base.console().addCommand(
      "app",
      [this](const CmdArgs& a, Print& out) {
        if (!a.has(0) || a.is(0, "list")) {
          printList(out);
        } else if (a.is(0, "stack")) {
          printStack(out);
        } else if (a.is(0, "stats")) {
          printStats(out);
        } else if (a.is(0, "open") && a.has(1)) {
          out.println(push(a[1].c_str(), joinFrom(a, 2)) ? "opening" : "rejected (see log)");
        } else if (a.is(0, "replace") && a.has(1)) {
          out.println(replace(a[1].c_str(), joinFrom(a, 2)) ? "replacing" : "rejected (see log)");
        } else if (a.is(0, "close")) {
          out.println(pop(static_cast<int>(a.toInt(1, 0))) ? "closing" : "rejected (see log)");
        } else if (a.is(0, "home")) {
          out.println(popToRoot() ? "closing to root" : "already at root");
        } else if (a.is(0, "boot")) {
          if (!a.has(1)) {
            const String id = _base->config().getString(EspBaseKeys::BootApp);
            out.printf("boot app: %s\r\n", id.isEmpty() ? "(none, home is used)" : id.c_str());
          } else if (a[1] == "-") {
            _base->config().remove(EspBaseKeys::BootApp);
            out.println("boot app cleared");
          } else if (AppRegistry::find(a[1].c_str()) != nullptr) {
            _base->config().setString(EspBaseKeys::BootApp, a[1]);
            out.printf("boot app set to %s\r\n", a[1].c_str());
          } else {
            out.println("unknown app");
          }
        } else if (a.is(0, "key") && a.has(1)) {
          uint8_t code = 0;
          if (parseKey(a[1], code)) {
            out.println(key(code, static_cast<int32_t>(a.toInt(2, 0))) ? "sent" : "input queue full");
          } else {
            out.println("usage: app key back|select|up|down|left|right|<code> [value]");
          }
        } else {
          out.println("usage: app list|stack|stats|open <id> [args]|replace <id> [args]|close [result]|home|boot [id|-]|key <name|code> [value]");
        }
      },
      "app list|stack|stats|open|replace|close|home|boot|key");

  base.web().on("/api/apps", HTTP_GET, [this](AsyncWebServerRequest* request) {
    AsyncJsonResponse* response = new AsyncJsonResponse();
    writeJson(response->getRoot().to<JsonObject>());
    response->setLength();
    request->send(response);
  });

  // {"action":"open|replace|close|home|key","id","args","result","code","value"}
  base.web().on("/api/apps", HTTP_POST, [this](AsyncWebServerRequest* request, JsonVariant& json) {
    const String action = json["action"] | "";
    const char* id = json["id"] | "";
    const String args = json["args"] | "";
    bool ok = false;
    if (action == "open") {
      ok = push(id, args);
    } else if (action == "replace") {
      ok = replace(id, args);
    } else if (action == "close") {
      ok = pop(json["result"] | 0);
    } else if (action == "home") {
      ok = popToRoot();
    } else if (action == "key") {
      ok = key(json["code"] | 0, json["value"] | 0);
    } else {
      request->send(400, "application/json", "{\"ok\":false,\"error\":\"action must be open, replace, close, home or key\"}");
      return;
    }
    request->send(ok ? 200 : 409, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false,\"error\":\"rejected, see the log\"}");
  }).setMaxContentLength(1024);
}

bool AppManager::start(Frame* frame, PresentFn present, const char* homeId) {
  if (_base == nullptr) {
    LOG_E("apps: start() before EspBase::begin()");
    return false;
  }
  if (_started) {
    return _depth > 0;
  }
  _frame = frame;
  _present = std::move(present);

  size_t n = 0;
  for (App* a = AppRegistry::first(); a != nullptr; a = AppRegistry::next(*a)) {
    for (App* b = AppRegistry::next(*a); b != nullptr; b = AppRegistry::next(*b)) {
      if (strcmp(a->id(), b->id()) == 0) {
        LOG_E("apps: duplicate id \"%s\"", a->id());
      }
    }
    a->_mgr = this;
    a->begin(*_base);
    n++;
  }
  for (App* a = AppRegistry::first(); a != nullptr; a = AppRegistry::next(*a)) {
    if (a->hasFlag(AppHidden)) {
      a->_open = true;
      a->_lastTickAt = millis();
      a->open(String());
      LOG_D("apps: service %s started", a->id());
    }
  }

  App* rootApp = chooseRoot(homeId);
  _started = true;
  _loopWindowStart = millis();
  _lastInputAt = millis();
  if (rootApp == nullptr) {
    LOG_W("apps: %u app(s) registered, none visible", static_cast<unsigned>(n));
    return false;
  }
  openOnStack(*rootApp, String());  // boot_app is only written when the root itself is replaced
  LOG_I("apps: %u app(s), root \"%s\"%s", static_cast<unsigned>(n), rootApp->id(), _frame ? "" : ", no frame (headless)");
  return true;
}

// The persisted boot_app if it is a visible app, else homeId, else the first visible app.
App* AppManager::chooseRoot(const char* homeId) const {
  const String boot = _base->config().getString(EspBaseKeys::BootApp);
  if (!boot.isEmpty()) {
    App* app = AppRegistry::find(boot.c_str());
    if (app != nullptr && !app->hasFlag(AppHidden)) {
      return app;
    }
    LOG_W("apps: boot_app \"%s\" not found, using home", boot.c_str());
  }
  if (homeId != nullptr) {
    App* app = AppRegistry::find(homeId);
    if (app != nullptr) {
      return app;
    }
    LOG_W("apps: home app \"%s\" not found", homeId);
  }
  for (App* a = AppRegistry::first(); a != nullptr; a = AppRegistry::next(*a)) {
    if (!a->hasFlag(AppHidden)) {
      return a;
    }
  }
  return nullptr;
}

// ---- Navigation ------------------------------------------------------------

App* AppManager::resolve(const char* id, const char* what) const {
  if (!_started) {
    LOG_W("apps: %s rejected, not started", what);
    return nullptr;
  }
  App* app = AppRegistry::find(id);
  if (app == nullptr) {
    LOG_W("apps: %s rejected, unknown app \"%s\"", what, id ? id : "");
    return nullptr;
  }
  if (app->hasFlag(AppHidden)) {
    LOG_W("apps: %s rejected, \"%s\" is a service", what, id);
    return nullptr;
  }
  if (onStack(*app)) {
    LOG_W("apps: %s rejected, \"%s\" is already open", what, id);
    return nullptr;
  }
  return app;
}

bool AppManager::onStack(const App& app) const {
  for (size_t i = 0; i < _depth; ++i) {
    if (_stack[i] == &app) {
      return true;
    }
  }
  return false;
}

bool AppManager::queue(Transition&& t, const char* what) {
  if (_mutex == nullptr || xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) {
    return false;
  }
  bool ok = false;
  if (_hasPending) {
    LOG_W("apps: %s rejected, another transition is pending", what);
  } else {
    _pending = std::move(t);
    _hasPending = true;
    ok = true;
  }
  xSemaphoreGive(_mutex);
  return ok;
}

bool AppManager::push(const char* id, const String& args) {
  App* app = resolve(id, "open");
  if (app == nullptr) {
    return false;
  }
  if (_depth >= kMaxDepth) {
    LOG_W("apps: open rejected, stack full (%u)", static_cast<unsigned>(kMaxDepth));
    return false;
  }
  return queue(Transition{Push, app, args, 0}, "open");
}

bool AppManager::pop(int result) {
  if (!_started) {
    return false;
  }
  if (_depth <= 1) {
    LOG_W("apps: close rejected, the root app stays open");
    return false;
  }
  return queue(Transition{Pop, nullptr, String(), result}, "close");
}

bool AppManager::replace(const char* id, const String& args) {
  App* app = resolve(id, "replace");
  if (app == nullptr) {
    return false;
  }
  return queue(Transition{Replace, app, args, 0}, "replace");
}

bool AppManager::popToRoot() {
  if (!_started || _depth <= 1) {
    return false;
  }
  return queue(Transition{PopToRoot, nullptr, String(), 0}, "home");
}

void AppManager::openOnStack(App& app, const String& args) {
  _stack[_depth++] = &app;
  app._open = true;
  app._dirty = true;
  app._nextTickAt = 0;
  app._lastTickAt = millis();
  app._nextDrawAt = 0;
  app.open(args);
}

void AppManager::closeTop() {
  App* top = _stack[_depth - 1];
  top->close();
  top->_open = false;
  _stack[--_depth] = nullptr;
}

void AppManager::apply(Transition& t) {
  App* fg = foreground();
  switch (t.kind) {
    case Push:
      if (t.app == nullptr || onStack(*t.app) || _depth >= kMaxDepth) {
        LOG_W("apps: open dropped");
        break;
      }
      if (fg != nullptr) {
        fg->pause();
      }
      openOnStack(*t.app, t.args);
      LOG_I("apps: open %s (depth %u)", t.app->id(), static_cast<unsigned>(_depth));
      break;
    case Pop:
      if (_depth <= 1) {
        break;
      }
      LOG_I("apps: close %s (result %d)", fg->id(), t.result);
      closeTop();
      foreground()->_dirty = true;
      foreground()->resume(t.result);
      break;
    case Replace: {
      if (t.app == nullptr || onStack(*t.app) || _depth == 0) {
        LOG_W("apps: replace dropped");
        break;
      }
      const bool rootReplaced = (_depth == 1);
      LOG_I("apps: replace %s with %s", fg->id(), t.app->id());
      closeTop();
      openOnStack(*t.app, t.args);
      if (rootReplaced) {
        _base->config().setString(EspBaseKeys::BootApp, t.app->id());
      }
      break;
    }
    case PopToRoot:
      while (_depth > 1) {
        closeTop();
      }
      LOG_I("apps: back to %s", root()->id());
      root()->_dirty = true;
      root()->resume(0);
      break;
    default:
      break;
  }
}

// ---- Input ------------------------------------------------------------------------

bool AppManager::input(const InputEvent& ev) {
  if (_inputs == nullptr) {
    return false;
  }
  InputEvent copy = ev;
  if (copy.ms == 0) {
    copy.ms = millis();
  }
  return xQueueSend(_inputs, &copy, 0) == pdTRUE;
}

bool AppManager::inputFromISR(const InputEvent& ev) {
  if (_inputs == nullptr) {
    return false;
  }
  BaseType_t woken = pdFALSE;
  const bool ok = xQueueSendFromISR(_inputs, &ev, &woken) == pdTRUE;
  if (woken == pdTRUE) {
    portYIELD_FROM_ISR();
  }
  return ok;
}

bool AppManager::key(uint8_t code, int32_t value) {
  InputEvent ev;
  ev.type = InputEvent::Press;
  ev.code = code;
  ev.value = value;
  return input(ev);
}

void AppManager::handleInput(const InputEvent& ev) {
  _lastInputAt = millis();
  App* fg = foreground();
  if (fg != nullptr && fg->input(ev)) {
    return;
  }
  if (ev.type == InputEvent::Press && ev.code == InputCode::Back && _depth > 1) {
    pop(0);
  }
}

// ---- Scheduling ------------------------------------------------------------------

void AppManager::loop() {
  if (!_started) {
    return;
  }
  const uint32_t now = millis();
  _loopCount++;
  if (now - _loopWindowStart >= 1000) {
    _loopHz = _loopCount;
    _loopCount = 0;
    _loopWindowStart = now;
  }

  InputEvent ev;
  while (_inputs != nullptr && xQueueReceive(_inputs, &ev, 0) == pdTRUE) {
    handleInput(ev);
  }

  runTicks(now);
  runDraw(now);

  if (_hasPending && _mutex != nullptr && xSemaphoreTake(_mutex, portMAX_DELAY) == pdTRUE) {
    Transition t = std::move(_pending);
    _hasPending = false;
    xSemaphoreGive(_mutex);
    apply(t);  // callbacks may queue the next transition; it runs next iteration
  }

  if (_idleTimeoutMs > 0 && _depth > 1 && now - _lastInputAt >= _idleTimeoutMs) {
    _lastInputAt = now;
    LOG_I("apps: idle for %lu s, back to root", static_cast<unsigned long>(_idleTimeoutMs / 1000));
    popToRoot();
  }
}

void AppManager::runTicks(uint32_t now) {
  const App* fg = foreground();
  for (App* a = AppRegistry::first(); a != nullptr; a = AppRegistry::next(*a)) {
    if (!a->_open) {
      continue;
    }
    if (a != fg && !a->hasFlag(AppHidden) && !a->hasFlag(AppTickInBackground)) {
      continue;
    }
    const uint16_t hz = a->info().tickHz;
    if (hz > 0 && !timeReached(now, a->_nextTickAt)) {
      continue;
    }
    const uint32_t dt = now - a->_lastTickAt;
    a->_lastTickAt = now;
    if (hz > 0) {
      a->_nextTickAt = now + 1000 / hz;  // resync after a stall, never catch up
    }
    const uint32_t t0 = micros();
    a->tick(dt);
    const uint32_t us = micros() - t0;
    a->_stats.ticks++;
    a->_stats.tickLastUs = us;
    a->_stats.tickMaxUs = std::max(a->_stats.tickMaxUs, us);
    if (us > _tickBudgetUs) {
      a->_stats.overruns++;
      if (now - a->_lastOverrunLogAt >= kOverrunLogIntervalMs) {
        a->_lastOverrunLogAt = now;
        LOG_W("apps: %s tick took %lu us (budget %lu us, %lu overruns)", a->id(), static_cast<unsigned long>(us),
              static_cast<unsigned long>(_tickBudgetUs), static_cast<unsigned long>(a->_stats.overruns));
      }
    }
  }
}

void AppManager::runDraw(uint32_t now) {
  if (_frame == nullptr || _depth == 0) {
    return;
  }
  // Draw chain: the topmost opaque app and every overlay above it share the frame.
  size_t base = _depth - 1;
  while (base > 0 && _stack[base]->hasFlag(AppOverlay)) {
    base--;
  }
  bool dirty = false;
  for (size_t i = base; i < _depth; ++i) {
    dirty = dirty || _stack[i]->_dirty;
  }
  if (!dirty) {
    return;
  }
  App* fg = foreground();
  const uint16_t hz = fg->info().drawHz;
  if (hz > 0 && !timeReached(now, fg->_nextDrawAt)) {
    return;
  }
  if (hz > 0) {
    fg->_nextDrawAt = now + 1000 / hz;
  }
  for (size_t i = base; i < _depth; ++i) {
    App* a = _stack[i];
    const uint32_t t0 = micros();
    a->draw(*_frame);
    const uint32_t us = micros() - t0;
    a->_dirty = false;
    a->_stats.draws++;
    a->_stats.drawLastUs = us;
    a->_stats.drawMaxUs = std::max(a->_stats.drawMaxUs, us);
  }
  if (_present) {
    const uint32_t t0 = micros();
    _present(*_frame);
    _presentLastUs = micros() - t0;
    _presentMaxUs = std::max(_presentMaxUs, _presentLastUs);
  }
}

// ---- Reporting -------------------------------------------------------------------

const char* AppManager::stateOf(const App& app, const App* fg) {
  if (app.hasFlag(AppHidden)) {
    return app._open ? "service" : "service (stopped)";
  }
  if (&app == fg) {
    return "foreground";
  }
  return app._open ? "paused" : "closed";
}

void AppManager::printList(Print& out) {
  if (!_started) {
    out.println("apps: not started (call base.apps().start())");
  }
  const App* fg = foreground();
  for (const App* a = AppRegistry::first(); a != nullptr; a = AppRegistry::next(*a)) {
    out.printf("%c %-12s %-20s order %4d  tick %3u Hz  draw %3u Hz  %s%s%s %s\r\n", a == fg ? '*' : ' ', a->id(), a->info().name, a->info().order,
               a->info().tickHz, a->info().drawHz, a->hasFlag(AppTickInBackground) ? "[bg] " : "", a->hasFlag(AppOverlay) ? "[overlay] " : "",
               a->hasFlag(AppHidden) ? "[service] " : "", stateOf(*a, fg));
  }
  if (AppRegistry::count() == 0) {
    out.println("  (no apps registered)");
  }
}

void AppManager::printStack(Print& out) {
  for (size_t i = 0; i < _depth; ++i) {
    out.printf("  %u: %s%s\r\n", static_cast<unsigned>(i), _stack[i]->id(), i + 1 == _depth ? " (foreground)" : "");
  }
  if (_depth == 0) {
    out.println("  (empty)");
  }
}

void AppManager::printStats(Print& out) {
  out.printf("loop: %lu iterations/s, present last %lu us, max %lu us\r\n", static_cast<unsigned long>(_loopHz),
             static_cast<unsigned long>(_presentLastUs), static_cast<unsigned long>(_presentMaxUs));
  for (const App* a = AppRegistry::first(); a != nullptr; a = AppRegistry::next(*a)) {
    const App::Stats& s = a->stats();
    out.printf("  %-12s ticks %lu (last %lu us, max %lu us, %lu overruns)  draws %lu (last %lu us, max %lu us)\r\n", a->id(),
               static_cast<unsigned long>(s.ticks), static_cast<unsigned long>(s.tickLastUs), static_cast<unsigned long>(s.tickMaxUs),
               static_cast<unsigned long>(s.overruns), static_cast<unsigned long>(s.draws), static_cast<unsigned long>(s.drawLastUs),
               static_cast<unsigned long>(s.drawMaxUs));
  }
}

void AppManager::writeJson(JsonObject o) {
  o["started"] = _started;
  o["loopHz"] = _loopHz;
  o["tickBudgetUs"] = _tickBudgetUs;
  const App* fg = foreground();
  o["foreground"] = fg != nullptr ? fg->id() : "";
  JsonArray stack = o["stack"].to<JsonArray>();
  const size_t depth = std::min(_depth, kMaxDepth);
  for (size_t i = 0; i < depth; ++i) {
    if (_stack[i] != nullptr) {
      stack.add(_stack[i]->id());
    }
  }
  JsonArray apps = o["apps"].to<JsonArray>();
  for (const App* a = AppRegistry::first(); a != nullptr; a = AppRegistry::next(*a)) {
    JsonObject e = apps.add<JsonObject>();
    e["id"] = a->id();
    e["name"] = a->info().name;
    e["order"] = a->info().order;
    e["tickHz"] = a->info().tickHz;
    e["drawHz"] = a->info().drawHz;
    e["flags"] = a->info().flags;
    e["state"] = stateOf(*a, fg);
    const App::Stats& s = a->stats();
    JsonObject st = e["stats"].to<JsonObject>();
    st["ticks"] = s.ticks;
    st["draws"] = s.draws;
    st["overruns"] = s.overruns;
    st["tickMaxUs"] = s.tickMaxUs;
    st["drawMaxUs"] = s.drawMaxUs;
  }
}
