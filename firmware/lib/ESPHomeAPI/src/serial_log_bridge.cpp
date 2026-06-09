#include "serial_log_bridge.h"

#include <cstdarg>
#include <cstring>

#include "api_server.h"

namespace esphome_api {

SerialLogBridge ApiSerial;

void SerialLogBridge::attach(APIServer *server, uint32_t level) {
  server_ = server;
  level_ = level;
}

void SerialLogBridge::detach() {
  flush_line();
  server_ = nullptr;
}

void SerialLogBridge::ensure_hw() {
#if defined(ESP32) || defined(ESP8266)
  if (!hw_ready_) {
    // Real UART from the Arduino core (undef macro if app src patched Serial).
#ifdef ESPHOME_API_SERIAL_BRIDGE
#undef Serial
#endif
    hw_ = &::Serial;
#ifdef ESPHOME_API_SERIAL_BRIDGE
#define Serial (::esphome_api::ApiSerial)
#endif
    hw_ready_ = true;
  }
#endif
}

void SerialLogBridge::write_byte(uint8_t b) {
#if defined(ESP32) || defined(ESP8266)
  ensure_hw();
  if (hw_) hw_->write(b);
#else
  std::fputc(b, stdout);
  std::fflush(stdout);
#endif

  if (b == '\n') {
    flush_line();
    return;
  }
  if (b != '\r') {
    if (line_.size() < 512) line_.push_back(static_cast<char>(b));
    else flush_line();
  }
}

void SerialLogBridge::flush_line() {
  if (line_.empty()) return;
  if (server_ != nullptr) server_->log(level_, line_);
  line_.clear();
}

#if defined(ESP32) || defined(ESP8266)

void SerialLogBridge::begin(unsigned long baud) {
  ensure_hw();
  if (hw_) hw_->begin(baud);
}

void SerialLogBridge::begin(unsigned long baud, uint32_t config) {
  ensure_hw();
  if (hw_) hw_->begin(baud, config);
}

void SerialLogBridge::printf(const char *fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  for (const char *p = buf; *p; p++) write_byte(static_cast<uint8_t>(*p));
}

void SerialLogBridge::end() {
  flush_line();
  if (hw_) hw_->end();
}

int SerialLogBridge::available() {
  ensure_hw();
  return hw_ ? hw_->available() : 0;
}

int SerialLogBridge::read() {
  ensure_hw();
  return hw_ ? hw_->read() : -1;
}

int SerialLogBridge::peek() {
  ensure_hw();
  return hw_ ? hw_->peek() : -1;
}

void SerialLogBridge::flush() {
  flush_line();
  ensure_hw();
  if (hw_) hw_->flush();
}

size_t SerialLogBridge::write(uint8_t b) {
  write_byte(b);
  return 1;
}

size_t SerialLogBridge::write(const uint8_t *buffer, size_t size) {
  for (size_t i = 0; i < size; i++) write_byte(buffer[i]);
  return size;
}

SerialLogBridge::operator bool() const { return hw_ready_ && hw_ != nullptr; }

#else  // host / simulator

void SerialLogBridge::begin(unsigned long) {}

void SerialLogBridge::printf(const char *fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  for (const char *p = buf; *p; p++) write_byte(static_cast<uint8_t>(*p));
}

void SerialLogBridge::println(const char *s) {
  print(s);
  write_byte('\n');
}

void SerialLogBridge::print(const char *s) {
  if (!s) return;
  for (const char *p = s; *p; p++) write_byte(static_cast<uint8_t>(*p));
}

#endif

}  // namespace esphome_api
