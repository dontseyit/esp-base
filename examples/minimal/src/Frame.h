// The project defines Frame; the library only passes references to it.
// This example has no display, so the frame is a 20x4 character "screen"
// that main.cpp prints to the log whenever its content changes. A real
// project would wrap a sprite / canvas of its graphics library here.
#pragma once

#include <Arduino.h>
#include <stdarg.h>
#include <string.h>

struct Frame {
  static constexpr int W = 20;
  static constexpr int H = 4;
  char cell[H][W + 1];

  Frame() { clear(); }

  void clear() {
    for (int r = 0; r < H; ++r) {
      memset(cell[r], ' ', W);
      cell[r][W] = '\0';
    }
  }

  void text(int row, int col, const char* s) {
    if (row < 0 || row >= H) {
      return;
    }
    for (int c = col; *s != '\0' && c < W; ++c, ++s) {
      if (c >= 0) {
        cell[row][c] = *s;
      }
    }
  }

  void textf(int row, int col, const char* fmt, ...) __attribute__((format(printf, 4, 5))) {
    char buf[W + 1];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    text(row, col, buf);
  }

  bool equals(const Frame& other) const { return memcmp(cell, other.cell, sizeof(cell)) == 0; }
};
