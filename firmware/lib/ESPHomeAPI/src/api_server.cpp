#include "api_server.h"

#include <Arduino.h>
#include <cstdarg>
#include <cstdio>

#include "api_connection.h"
#include "api_log.h"
#include "serial_log_bridge.h"

namespace esphome_api {

// Decode standard base64 into out (max out_len). Returns decoded length or -1.
static int base64_decode(const std::string &in, uint8_t *out, size_t out_len) {
  auto val = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
  };
  int buf = 0, bits = 0;
  size_t n = 0;
  for (char c : in) {
    if (c == '=' || c == '\n' || c == '\r' || c == ' ') continue;
    int v = val(c);
    if (v < 0) return -1;
    buf = (buf << 6) | v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (n >= out_len) return -1;
      out[n++] = (uint8_t)((buf >> bits) & 0xFF);
    }
  }
  return (int)n;
}

void APIServer::add_entity(Entity *entity) {
  entity->set_server(this);
  entities_.push_back(entity);
}

void APIServer::begin() {
  if (cfg_.mac_address.empty()) {
    cfg_.mac_address = std::string(WiFi.macAddress().c_str());
  }
  if (!cfg_.encryption_key.empty()) {
    int len = base64_decode(cfg_.encryption_key, psk_, sizeof(psk_));
    encryption_enabled_ = (len == 32);
  }
  // One-time entity init (e.g. restore persisted switch state).
  for (Entity *e : entities_) e->setup();
  server_ = new WiFiServer(cfg_.port);
  server_->begin();
  server_->setNoDelay(true);
}

void APIServer::loop() {
  if (server_ == nullptr) return;

  // Accept a new client (ESP32/ESP8266 cores: accept() returns new clients).
  WiFiClient nc = server_->accept();
  if (nc) {
    conns_.push_back(new APIConnection(this, nc));
  }

  // Service existing connections, removing dead ones.
  for (size_t i = 0; i < conns_.size();) {
    APIConnection *c = conns_[i];
    c->loop();
    if (!c->alive()) {
      delete c;
      conns_.erase(conns_.begin() + i);
    } else {
      ++i;
    }
  }
}

void APIServer::push_state(Entity *entity) {
  for (APIConnection *c : conns_) {
    if (c->alive()) c->on_state_changed(entity);
  }
}

void APIServer::push_event(Event *entity, const std::string &event_type) {
  for (APIConnection *c : conns_) {
    if (c->alive()) c->send_entity_event(entity, event_type);
  }
}

void APIServer::push_camera_image(Camera *camera, const uint8_t *data,
                                  size_t len, bool done) {
  for (APIConnection *c : conns_) {
    if (c->alive()) c->send_camera_image(camera, data, len, done);
  }
}

void APIServer::call_homeassistant_service(
    const HomeAssistantServiceCall &call) {
  for (APIConnection *c : conns_) {
    if (c->alive()) c->send_homeassistant_service(call);
  }
}

void APIServer::call_homeassistant_service(
    const std::string &service, const std::vector<HAKeyValue> &data) {
  HomeAssistantServiceCall call;
  call.service = service;
  call.data = data;
  call_homeassistant_service(call);
}

void APIServer::fire_homeassistant_event(const std::string &event,
                                         const std::vector<HAKeyValue> &data) {
  HomeAssistantServiceCall call;
  call.service = event;
  call.data = data;
  call.is_event = true;
  call_homeassistant_service(call);
}

void APIServer::subscribe_homeassistant_state(
    const std::string &entity_id, const std::string &attribute,
    std::function<void(const std::string &)> on_state, bool once) {
  HomeAssistantStateSubscription sub;
  sub.entity_id = entity_id;
  sub.attribute = attribute;
  sub.once = once;
  sub.on_state = std::move(on_state);
  ha_state_subs_.push_back(std::move(sub));
}

void APIServer::on_homeassistant_state(const std::string &entity_id,
                                       const std::string &attribute,
                                       const std::string &state) {
  for (auto &sub : ha_state_subs_) {
    if (sub.entity_id == entity_id && sub.attribute == attribute &&
        sub.on_state) {
      sub.on_state(state);
    }
  }
}

void APIServer::log(uint32_t level, const std::string &message) {
  for (APIConnection *c : conns_) {
    if (c->alive()) c->send_log_message(static_cast<int>(level), message);
  }
}

void APIServer::logf(uint32_t level, const char *fmt, ...) {
  char buf[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  log(level, buf);
}

void APIServer::enable_serial_log_bridge(uint32_t level) {
  ApiSerial.attach(this, level);
}

void APIServer::disable_serial_log_bridge() { ApiSerial.detach(); }

void APIServer::dump_config() {
  const DeviceConfig &cfg = config();
  log(LOG_LEVEL_CONFIG, "ESPHome API:");
  log(LOG_LEVEL_CONFIG, "  Name: " + cfg.name);
  log(LOG_LEVEL_CONFIG, "  Friendly name: " + cfg.friendly_name);
  log(LOG_LEVEL_CONFIG, "  Port: " + std::to_string(cfg.port));
  log(LOG_LEVEL_CONFIG, "  Encryption: " +
                            std::string(encryption_enabled() ? "yes" : "no"));
  log(LOG_LEVEL_CONFIG, "  Password: " +
                            std::string(cfg.password.empty() ? "no" : "yes"));
  log(LOG_LEVEL_CONFIG, "  Entities: " + std::to_string(entities_.size()));
  for (Entity *e : entities_) {
    log(LOG_LEVEL_CONFIG, "    - " + e->object_id() + " (" + e->kind() + ")");
  }
}

void APIServer::dispatch_camera_image_request(bool single, bool stream) {
  for (Entity *e : entities_) {
    if (std::string(e->kind()) == "camera") {
      static_cast<Camera *>(e)->handle_image_request(single, stream);
    }
  }
}

Entity *APIServer::find_by_key(uint32_t key) {
  for (Entity *e : entities_) {
    if (e->key() == key) return e;
  }
  return nullptr;
}

}  // namespace esphome_api
