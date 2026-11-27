// Command registry shared by every line source: the serial port through
// poll(), the web console, and anything else that calls execute(). Register a
// command once and it works everywhere. Arduino only (String, Print, Stream),
// no network dependency, unit tested on the host.
//
//   base.console().addCommand("led", [](const CmdArgs& a, Print& out) { ... }, "led on|off");
//   void loop() { base.loop(); base.console().poll(Serial); }
#pragma once

#include <Arduino.h>

#include <functional>
#include <vector>

namespace espbase {

// Parsed command arguments (everything after the command name).
// Supports "quoted strings" and up to kMax tokens.
class CmdArgs {
 public:
  static constexpr size_t kMax = 8;

  // Splits `line` into the command name and its arguments. Returns false for an empty line.
  static bool parse(const String& line, String& cmd, CmdArgs& args);

  size_t size() const { return _n; }
  bool has(size_t i) const { return i < _n; }
  const String& operator[](size_t i) const { return i < _n ? _v[i] : _empty; }
  bool is(size_t i, const char* word) const { return i < _n && _v[i].equalsIgnoreCase(word); }
  long toInt(size_t i, long def = 0) const;

 private:
  String _v[kMax];
  size_t _n = 0;
  static const String _empty;
};

using CmdHandler = std::function<void(const CmdArgs& args, Print& out)>;

// A Print that collects into a String (used for command output).
class StringPrint : public Print {
 public:
  size_t write(uint8_t c) override {
    buf += static_cast<char>(c);
    return 1;
  }
  size_t write(const uint8_t* data, size_t len) override {
    buf.concat(reinterpret_cast<const char*>(data), len);
    return len;
  }
  String buf;
};

class Console {
 public:
  static constexpr size_t kMaxLine = 200;

  // Registering an existing name replaces its handler.
  void addCommand(const char* name, CmdHandler handler, const char* help = "");
  // Executes one command line synchronously. Returns false if the command is unknown.
  bool execute(const String& line, Print& out);
  void printHelp(Print& out);
  // Reads complete lines from `io` without blocking and executes them with
  // output to `io`. Call from loop(); one call per source.
  void poll(Stream& io);

 private:
  struct Command {
    String name;
    String help;
    CmdHandler handler;
  };
  std::vector<Command> _commands;
  String _line;
};

}  // namespace espbase
