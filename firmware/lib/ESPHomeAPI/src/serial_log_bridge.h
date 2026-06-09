// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

#pragma once
// Line-buffered Print wrapper: forwards to hardware UART (or stdout) and
// mirrors complete lines to APIServer::log() for HA log streaming.
#include <cstdint>
#include <string>

#include "api_log.h"

#if defined(ESP32) || defined(ESP8266)
#include <HardwareSerial.h>
#include <Print.h>
#else
#include <cstdio>
#endif

namespace esphome_api {

class APIServer;

#if defined(ESP32) || defined(ESP8266)
class SerialLogBridge : public Print {
#else
class SerialLogBridge {
#endif
 public:
  void attach(APIServer *server, uint32_t level = LOG_LEVEL_INFO);
  void detach();

#if defined(ESP32) || defined(ESP8266)
  void begin(unsigned long baud);
  void begin(unsigned long baud, uint32_t config);
  void printf(const char *fmt, ...);
  void end();
  int available();
  int read();
  int peek();
  void flush() override;
  size_t write(uint8_t b) override;
  size_t write(const uint8_t *buffer, size_t size) override;
  using Print::write;
  explicit operator bool() const;
#else
  void begin(unsigned long baud = 0);
  void printf(const char *fmt, ...);
  void println(const char *s = "");
  void print(const char *s);
#endif

 private:
  void ensure_hw();
  void write_byte(uint8_t b);
  void flush_line();

  APIServer *server_ = nullptr;
  uint32_t level_ = LOG_LEVEL_INFO;
  std::string line_;
  bool hw_ready_ = false;
#if defined(ESP32) || defined(ESP8266)
  HardwareSerial *hw_ = nullptr;
#endif
};

extern SerialLogBridge ApiSerial;

}  // namespace esphome_api
