// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

// Minimal Arduino.h shim for the HOST simulator build only.
// Lets the real api_connection.cpp / api_server.cpp compile on a POSIX host so
// the protocol code can be exercised by the Python test bed over a TCP socket.
#pragma once
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

inline unsigned long millis() {
  static const auto start = std::chrono::steady_clock::now();
  auto now = std::chrono::steady_clock::now();
  return (unsigned long)std::chrono::duration_cast<std::chrono::milliseconds>(
             now - start)
      .count();
}

inline void delay(unsigned long ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

#define HIGH 1
#define LOW 0
#define OUTPUT 1
#define INPUT 0

inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return 0; }

struct SerialStub {
  void begin(unsigned long) {}
  template <typename... Args>
  void printf(const char *fmt, Args... args) {
    std::printf(fmt, args...);
  }
  void println(const char *s = "") { std::printf("%s\n", s); }
  void print(const char *s) { std::printf("%s", s); }
};
static SerialStub Serial;

struct ESPClass {
  // Simulator: do not actually exit, just log (keeps the suite running).
  void restart() { std::printf("[sim] ESP.restart() called (ignored)\n"); }
};
static ESPClass ESP;
