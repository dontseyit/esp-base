#include "Console.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>

namespace espbase {

const String CmdArgs::_empty;

// ---- CmdArgs ---------------------------------------------------------------

bool CmdArgs::parse(const String& line, String& cmd, CmdArgs& args) {
  args._n = 0;
  cmd = "";
  const size_t n = line.length();
  size_t i = 0;
  auto skipSpace = [&]() {
    while (i < n && isspace(static_cast<unsigned char>(line[i]))) {
      i++;
    }
  };
  auto token = [&](String& out) -> bool {
    skipSpace();
    if (i >= n) {
      return false;
    }
    out = "";
    if (line[i] == '"' || line[i] == '\'') {
      const char q = line[i++];
      while (i < n && line[i] != q) {
        if (line[i] == '\\' && i + 1 < n) {
          i++;
        }
        out += line[i++];
      }
      if (i < n) {
        i++;  // closing quote
      }
    } else {
      while (i < n && !isspace(static_cast<unsigned char>(line[i]))) {
        out += line[i++];
      }
    }
    return true;
  };
  if (!token(cmd)) {
    return false;
  }
  String t;
  while (args._n < kMax && token(t)) {
    args._v[args._n++] = t;
  }
  return true;
}

long CmdArgs::toInt(size_t i, long def) const {
  if (i >= _n || _v[i].isEmpty()) {
    return def;
  }
  char* end = nullptr;
  const long v = strtol(_v[i].c_str(), &end, 0);
  return (end != nullptr && *end == '\0') ? v : def;
}

// ---- Console ----------------------------------------------------------------

void Console::addCommand(const char* name, CmdHandler handler, const char* help) {
  for (auto& c : _commands) {
    if (c.name.equalsIgnoreCase(name)) {
      c.handler = std::move(handler);
      c.help = help ? help : "";
      return;
    }
  }
  _commands.push_back(Command{String(name), String(help ? help : ""), std::move(handler)});
}

bool Console::execute(const String& line, Print& out) {
  String name;
  CmdArgs args;
  if (!CmdArgs::parse(line, name, args)) {
    return true;  // empty line
  }
  for (auto& c : _commands) {
    if (c.name.equalsIgnoreCase(name)) {
      c.handler(args, out);
      return true;
    }
  }
  out.printf("unknown command \"%s\" (try: help)\r\n", name.c_str());
  return false;
}

void Console::printHelp(Print& out) {
  std::vector<const Command*> sorted;
  sorted.reserve(_commands.size());
  for (auto& c : _commands) {
    sorted.push_back(&c);
  }
  std::sort(sorted.begin(), sorted.end(), [](const Command* a, const Command* b) { return strcmp(a->name.c_str(), b->name.c_str()) < 0; });
  for (const Command* c : sorted) {
    out.printf("  %-14s %s\r\n", c->name.c_str(), c->help.c_str());
  }
}

void Console::poll(Stream& io) {
  while (io.available() > 0) {
    const char c = static_cast<char>(io.read());
    if (c == '\n' || c == '\r') {
      if (!_line.isEmpty()) {
        execute(_line, io);
        _line = "";
      }
    } else if (_line.length() < kMaxLine) {
      _line += c;
    }
  }
}

}  // namespace espbase
