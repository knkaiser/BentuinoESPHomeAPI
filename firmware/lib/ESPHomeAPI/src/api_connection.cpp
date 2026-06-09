// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

#include "api_connection.h"

#include <Arduino.h>

#include "api_log.h"
#include "api_message_types.h"
#include "api_server.h"
#include "entity.h"
#include "platform_time.h"

namespace esphome_api {

// Connection is dropped if no inbound traffic for this long.
static constexpr uint32_t KEEPALIVE_TIMEOUT_MS = 90000;
// Server sends a PingRequest at this interval.
static constexpr uint32_t PING_INTERVAL_MS = 20000;
// Reject absurdly large frames (defensive).
static constexpr size_t MAX_FRAME_PAYLOAD = 8192;

APIConnection::APIConnection(APIServer *server, WiFiClient client)
    : server_(server), client_(client) {
  client_.setNoDelay(true);
  last_rx_ms_ = millis();
  last_ping_ms_ = millis();
  rx_.reserve(256);
}

void APIConnection::loop() {
  if (!client_.connected()) {
    close();
    return;
  }
  read_into_buffer();
  process_buffer();
  keepalive();
}

void APIConnection::read_into_buffer() {
  // Read in bounded chunks to avoid starving other connections.
  uint8_t tmp[256];
  int budget = 4;
  while (budget-- > 0) {
    int avail = client_.available();
    if (avail <= 0) break;
    int want = avail > (int)sizeof(tmp) ? (int)sizeof(tmp) : avail;
    int got = client_.read(tmp, want);
    if (got <= 0) break;
    rx_.insert(rx_.end(), tmp, tmp + got);
    last_rx_ms_ = millis();
  }
}

void APIConnection::process_buffer() {
  // Detect transport from the first indicator byte the client sends.
  if (transport_ == Transport::UNKNOWN) {
    if (rx_.empty()) return;
    uint8_t ind = rx_[0];
    if (ind == 0x00) {
      if (server_->encryption_enabled()) {
        reject_requires_encryption();
        return;
      }
      transport_ = Transport::PLAINTEXT;
    } else if (ind == 0x01) {
      if (!server_->encryption_enabled()) {
        close();  // device has no PSK; cannot do Noise
        return;
      }
      transport_ = Transport::NOISE;
      noise_stage_ = NoiseStage::CLIENT_HELLO;
    } else {
      close();
      return;
    }
  }

  if (transport_ == Transport::PLAINTEXT)
    process_plaintext();
  else
    process_noise();
}

void APIConnection::process_plaintext() {
  size_t consumed = 0;
  while (alive_) {
    if (rx_.size() - consumed < 1) break;
    if (rx_[consumed] != 0x00) {
      close();
      return;
    }
    size_t pos = consumed + 1;
    uint32_t length = 0;
    uint32_t type = 0;
    if (!stream_varint(rx_.data(), rx_.size(), pos, length)) break;  // wait
    if (!stream_varint(rx_.data(), rx_.size(), pos, type)) break;    // wait
    if (length > MAX_FRAME_PAYLOAD) {
      close();
      return;
    }
    if (rx_.size() - pos < length) break;  // wait for full payload
    handle_message(static_cast<uint16_t>(type),
                   length ? &rx_[pos] : nullptr, length);
    consumed = pos + length;
  }
  if (consumed > 0)
    rx_.erase(rx_.begin(), rx_.begin() + consumed);
}

void APIConnection::process_noise() {
  size_t consumed = 0;
  while (alive_) {
    if (rx_.size() - consumed < 3) break;  // need indicator + uint16 length
    if (rx_[consumed] != 0x01) {
      close();
      return;
    }
    size_t length = (static_cast<size_t>(rx_[consumed + 1]) << 8) |
                    rx_[consumed + 2];
    if (length > MAX_FRAME_PAYLOAD) {
      close();
      return;
    }
    if (rx_.size() - consumed - 3 < length) break;  // wait for full frame
    const uint8_t *body = length ? &rx_[consumed + 3] : nullptr;
    handle_noise_frame(body, length);
    consumed += 3 + length;
  }
  if (consumed > 0)
    rx_.erase(rx_.begin(), rx_.begin() + consumed);
}

void APIConnection::send_noise_frame(const uint8_t *body, size_t len) {
  if (!client_.connected()) {
    alive_ = false;
    return;
  }
  std::vector<uint8_t> frame;
  frame.reserve(len + 3);
  frame.push_back(0x01);
  frame.push_back((len >> 8) & 0xFF);
  frame.push_back(len & 0xFF);
  if (len) frame.insert(frame.end(), body, body + len);
  client_.write(frame.data(), frame.size());
}

void APIConnection::send_server_hello() {
  std::vector<uint8_t> body;
  body.push_back(0x01);  // chosen protocol byte
  const std::string &name = server_->config().name;
  body.insert(body.end(), name.begin(), name.end());
  body.push_back(0x00);
  const std::string &mac = server_->config().mac_address;
  body.insert(body.end(), mac.begin(), mac.end());
  body.push_back(0x00);
  send_noise_frame(body.data(), body.size());
}

void APIConnection::handle_noise_frame(const uint8_t *body, size_t len) {
  switch (noise_stage_) {
    case NoiseStage::CLIENT_HELLO: {
      // Prologue = "NoiseAPIInit" + uint16_be(client_hello_len) + client_hello
      std::vector<uint8_t> prologue;
      const char *tag = "NoiseAPIInit";
      prologue.insert(prologue.end(), tag, tag + 12);
      prologue.push_back((len >> 8) & 0xFF);
      prologue.push_back(len & 0xFF);
      if (len) prologue.insert(prologue.end(), body, body + len);

      send_server_hello();
      noise_.init(server_->psk(), prologue.data(), prologue.size());
      noise_stage_ = NoiseStage::HANDSHAKE;
      break;
    }
    case NoiseStage::HANDSHAKE: {
      if (len < 1 || body[0] != 0x00) {  // expect success-prefixed handshake
        close();
        return;
      }
      if (!noise_.read_message_1(body + 1, len - 1)) {
        close();
        return;
      }
      std::vector<uint8_t> msg2;
      noise_.write_message_2(msg2);
      std::vector<uint8_t> framed;
      framed.reserve(msg2.size() + 1);
      framed.push_back(0x00);  // success prefix
      framed.insert(framed.end(), msg2.begin(), msg2.end());
      send_noise_frame(framed.data(), framed.size());
      noise_.split();
      noise_stage_ = NoiseStage::READY;
      break;
    }
    case NoiseStage::READY: {
      std::vector<uint8_t> inner;
      if (!noise_.decrypt(body, len, inner)) {
        close();
        return;
      }
      if (inner.size() < 4) {
        close();
        return;
      }
      uint16_t type = (static_cast<uint16_t>(inner[0]) << 8) | inner[1];
      uint16_t plen = (static_cast<uint16_t>(inner[2]) << 8) | inner[3];
      if (4 + (size_t)plen > inner.size()) {
        close();
        return;
      }
      handle_message(type, inner.data() + 4, plen);
      break;
    }
  }
}

void APIConnection::handle_message(uint16_t type, const uint8_t *p,
                                   size_t len) {
  // Pre-authentication: handshake + setup-level messages only (TS-4).
  if (!authenticated_) {
    switch (type) {
      case MSG_HELLO_REQUEST:
      case MSG_CONNECT_REQUEST:
      case MSG_PING_REQUEST:
      case MSG_PING_RESPONSE:
      case MSG_DISCONNECT_REQUEST:
      case MSG_DISCONNECT_RESPONSE:
        break;
      case MSG_DEVICE_INFO_REQUEST:
      case MSG_GET_TIME_REQUEST:
        if (state_ == State::WAIT_HELLO) {
          close();
          return;
        }
        break;
      default:
        close();
        return;
    }
  }

  switch (type) {
    case MSG_HELLO_REQUEST:
      on_hello(p, len);
      break;
    case MSG_CONNECT_REQUEST:
      on_connect(p, len);
      break;
    case MSG_DISCONNECT_REQUEST:
      send_empty(MSG_DISCONNECT_RESPONSE);
      close();
      break;
    case MSG_DISCONNECT_RESPONSE:
      close();
      break;
    case MSG_PING_REQUEST:
      send_empty(MSG_PING_RESPONSE);
      break;
    case MSG_PING_RESPONSE:
      break;  // keepalive acknowledged
    case MSG_DEVICE_INFO_REQUEST:
      on_device_info();
      break;
    case MSG_LIST_ENTITIES_REQUEST:
      on_list_entities();
      break;
    case MSG_SUBSCRIBE_STATES_REQUEST:
      on_subscribe_states();
      break;
    case MSG_COVER_COMMAND_REQUEST:
    case MSG_FAN_COMMAND_REQUEST:
    case MSG_LIGHT_COMMAND_REQUEST:
    case MSG_SWITCH_COMMAND_REQUEST:
    case MSG_CLIMATE_COMMAND_REQUEST:
    case MSG_NUMBER_COMMAND_REQUEST:
    case MSG_SELECT_COMMAND_REQUEST:
    case MSG_LOCK_COMMAND_REQUEST:
    case MSG_BUTTON_COMMAND_REQUEST:
    case MSG_MEDIA_PLAYER_COMMAND_REQUEST:
    case MSG_ALARM_CONTROL_PANEL_COMMAND_REQUEST:
    case MSG_TEXT_COMMAND_REQUEST:
    case MSG_DATE_COMMAND_REQUEST:
    case MSG_TIME_COMMAND_REQUEST:
    case MSG_VALVE_COMMAND_REQUEST:
    case MSG_DATETIME_COMMAND_REQUEST:
    case MSG_UPDATE_COMMAND_REQUEST:
      on_entity_command(type, p, len);
      break;
    case MSG_CAMERA_IMAGE_REQUEST:
      on_camera_image_request(p, len);
      break;
    case MSG_SUBSCRIBE_LOGS_REQUEST:
      on_subscribe_logs(p, len);
      break;
    case MSG_SUBSCRIBE_HOMEASSISTANT_SERVICES_REQUEST:
      on_subscribe_ha_services();
      break;
    case MSG_SUBSCRIBE_HOME_ASSISTANT_STATES_REQUEST:
      on_subscribe_ha_states();
      break;
    case MSG_HOME_ASSISTANT_STATE_RESPONSE:
      on_ha_state_response(p, len);
      break;
    case MSG_GET_TIME_REQUEST:
      on_get_time();
      break;
    case MSG_GET_TIME_RESPONSE:
      on_get_time_response(p, len);
      break;
    default:
      // Unknown / unsupported message: ignore, keep the connection alive.
      break;
  }
}

void APIConnection::on_hello(const uint8_t *p, size_t len) {
  std::string client_info;
  ProtoDecoder dec(p, len);
  ProtoField f;
  while (dec.next(f)) {
    if (f.field == 1 && f.wire == WIRE_LEN) client_info = f.as_string();
  }

  ProtoWriteBuffer b;
  b.encode_uint32(1, API_VERSION_MAJOR, true);
  b.encode_uint32(2, API_VERSION_MINOR, true);
  b.encode_string(3, std::string("esphome-api-cpp"));
  b.encode_string(4, server_->config().name);
  send_message(MSG_HELLO_RESPONSE, b);
  state_ = State::WAIT_CONNECT;
  // ESPHome 2026.1+ / current aioesphomeapi: passwordless devices are usable
  // immediately after Hello. ConnectRequest (id 3) is optional for compat.
  if (server_->config().password.empty()) {
    authenticated_ = true;
    state_ = State::CONNECTED;
    if (server_->config().time_sync_from_ha && epoch_seconds() == 0)
      request_time_from_client();
  }
}

void APIConnection::on_connect(const uint8_t *p, size_t len) {
  std::string password;
  ProtoDecoder dec(p, len);
  ProtoField f;
  while (dec.next(f)) {
    if (f.field == 1 && f.wire == WIRE_LEN) password = f.as_string();
  }

  const std::string &expected = server_->config().password;
  bool ok = expected.empty() || (password == expected);

  ProtoWriteBuffer b;
  b.encode_bool(1, !ok);  // invalid_password
  send_message(MSG_CONNECT_RESPONSE, b);

  if (ok) {
    authenticated_ = true;
    state_ = State::CONNECTED;
    if (server_->config().time_sync_from_ha && epoch_seconds() == 0)
      request_time_from_client();
  } else {
    close();
  }
}

void APIConnection::on_device_info() {
  const DeviceConfig &cfg = server_->config();
  ProtoWriteBuffer b;
  b.encode_bool(1, !cfg.password.empty());     // uses_password
  b.encode_string(2, cfg.name);                // name
  b.encode_string(3, cfg.mac_address);         // mac_address
  b.encode_string(4, cfg.esphome_version);     // esphome_version
  b.encode_string(5, cfg.compilation_time);    // compilation_time
  b.encode_string(6, cfg.model);               // model
  b.encode_string(12, cfg.manufacturer);       // manufacturer
  b.encode_string(13, cfg.friendly_name);      // friendly_name
  b.encode_uint32(10, 80);                     // webserver_port (HA dashboard hint)
  send_message(MSG_DEVICE_INFO_RESPONSE, b);
}

void APIConnection::on_list_entities() {
  ProtoWriteBuffer b;
  for (Entity *e : server_->entities()) {
    b.clear();
    e->encode_list(b);
    send_message(e->list_type(), b);
  }
  send_empty(MSG_LIST_ENTITIES_DONE_RESPONSE);
}

void APIConnection::on_subscribe_states() {
  states_subscribed_ = true;
  // Immediately push the current state of every entity.
  for (Entity *e : server_->entities()) {
    if (e->state_type() != 0) send_entity_state(e);
  }
}

void APIConnection::send_entity_state(Entity *entity) {
  if (entity->state_type() == 0) return;
  ProtoWriteBuffer b;
  entity->encode_state(b);
  send_message(entity->state_type(), b);
}

void APIConnection::on_entity_command(uint16_t type, const uint8_t *p,
                                    size_t len) {
  uint32_t key = 0;
  ProtoDecoder dec(p, len);
  ProtoField f;
  while (dec.next(f)) {
    if (f.field == 1 && f.wire == WIRE_FIXED32) {
      key = f.as_fixed32;
      break;
    }
  }
  Entity *e = server_->find_by_key(key);
  if (e != nullptr) e->handle_command(type, p, len);
}

void APIConnection::on_subscribe_logs(const uint8_t *p, size_t len) {
  uint32_t level = LOG_LEVEL_NONE;
  bool dump_config = false;
  ProtoDecoder dec(p, len);
  ProtoField f;
  while (dec.next(f)) {
    if (f.field == 1 && f.wire == WIRE_VARINT) level = (uint32_t)f.as_varint;
    if (f.field == 2 && f.wire == WIRE_VARINT) dump_config = f.as_bool();
  }
  log_subscription_ = static_cast<int>(level);
  if (dump_config) server_->dump_config();
}

void APIConnection::on_subscribe_ha_services() {
  ha_services_subscribed_ = true;
}

void APIConnection::on_subscribe_ha_states() {
  // Reply with one SubscribeHomeAssistantStateResponse per registered entity.
  for (const auto &sub : server_->ha_state_subscriptions()) {
    ProtoWriteBuffer b;
    b.encode_string(1, sub.entity_id);
    b.encode_string(2, sub.attribute);
    b.encode_bool(3, sub.once);
    send_message(MSG_SUBSCRIBE_HOME_ASSISTANT_STATE_RESPONSE, b);
  }
}

void APIConnection::on_ha_state_response(const uint8_t *p, size_t len) {
  std::string entity_id, state, attribute;
  ProtoDecoder dec(p, len);
  ProtoField f;
  while (dec.next(f)) {
    if (f.wire != WIRE_LEN) continue;
    if (f.field == 1) entity_id = f.as_string();
    else if (f.field == 2) state = f.as_string();
    else if (f.field == 3) attribute = f.as_string();
  }
  server_->on_homeassistant_state(entity_id, attribute, state);
}

void APIConnection::send_homeassistant_service(
    const HomeAssistantServiceCall &call) {
  if (!authenticated_ || !ha_services_subscribed_) return;
  ProtoWriteBuffer b;
  b.encode_string(1, call.service);
  auto encode_map = [&b](uint32_t field, const std::vector<HAKeyValue> &kv) {
    for (const auto &pair : kv) {
      ProtoWriteBuffer m;  // HomeassistantServiceMap { key=1, value=2 }
      m.encode_string(1, pair.first);
      m.encode_string(2, pair.second);
      b.encode_bytes(field, m.data.data(), m.data.size(), true);
    }
  };
  encode_map(2, call.data);
  encode_map(3, call.data_template);
  encode_map(4, call.variables);
  b.encode_bool(5, call.is_event);
  send_message(MSG_HOMEASSISTANT_SERVICE_RESPONSE, b);
}

void APIConnection::on_get_time() {
  ProtoWriteBuffer b;
  b.encode_fixed32(1, epoch_seconds(), true);
  send_message(MSG_GET_TIME_RESPONSE, b);
}

void APIConnection::on_get_time_response(const uint8_t *p, size_t len) {
  uint32_t epoch = 0;
  ProtoDecoder dec(p, len);
  ProtoField f;
  while (dec.next(f)) {
    if (f.field == 1 && f.wire == WIRE_FIXED32) epoch = f.as_fixed32;
  }
  if (epoch != 0) set_epoch_seconds(epoch);
}

void APIConnection::request_time_from_client() { send_empty(MSG_GET_TIME_REQUEST); }

bool APIConnection::send_log_message(int level, const std::string &message) {
  if (log_subscription_ < 0 || log_subscription_ < level) return false;
  ProtoWriteBuffer b;
  b.encode_uint32(1, static_cast<uint32_t>(level), true);
  b.encode_string(3, message);
  if (!send_message_checked(MSG_SUBSCRIBE_LOGS_RESPONSE, b)) {
    ProtoWriteBuffer fail;
    fail.encode_bool(4, true);
    send_message(MSG_SUBSCRIBE_LOGS_RESPONSE, fail);
    return false;
  }
  return true;
}

void APIConnection::on_camera_image_request(const uint8_t *p, size_t len) {
  bool single = false;
  bool stream = false;
  ProtoDecoder dec(p, len);
  ProtoField f;
  while (dec.next(f)) {
    if (f.field == 1 && f.wire == WIRE_VARINT) single = f.as_bool();
    if (f.field == 2 && f.wire == WIRE_VARINT) stream = f.as_bool();
  }
  server_->dispatch_camera_image_request(single, stream);
}

void APIConnection::send_entity_event(Event *entity,
                                      const std::string &event_type) {
  if (!authenticated_ || !states_subscribed_) return;
  ProtoWriteBuffer b;
  b.encode_fixed32(1, entity->key(), true);
  b.encode_string(2, event_type);
  send_message(MSG_EVENT_RESPONSE, b);
}

void APIConnection::send_camera_image(Camera *camera, const uint8_t *data,
                                      size_t len, bool done) {
  if (!authenticated_) return;
  ProtoWriteBuffer b;
  b.encode_fixed32(1, camera->key(), true);
  b.encode_bytes(2, data, len);
  b.encode_bool(3, done);
  send_message(MSG_CAMERA_IMAGE_RESPONSE, b);
}

void APIConnection::on_state_changed(Entity *entity) {
  if (!authenticated_ || !states_subscribed_) return;
  send_entity_state(entity);
}

void APIConnection::keepalive() {
  uint32_t now = millis();
  if (now - last_rx_ms_ > KEEPALIVE_TIMEOUT_MS) {
    close();
    return;
  }
  if (authenticated_ && now - last_ping_ms_ > PING_INTERVAL_MS) {
    send_empty(MSG_PING_REQUEST);
    last_ping_ms_ = now;
  }
}

bool APIConnection::send_message_checked(uint16_t type,
                                       const ProtoWriteBuffer &body) {
  if (!client_.connected()) {
    alive_ = false;
    return false;
  }
  if (transport_ == Transport::NOISE) {
    // Inner: uint16_be(type) + uint16_be(len) + protobuf payload, encrypted.
    std::vector<uint8_t> inner;
    inner.reserve(body.data.size() + 4);
    inner.push_back((type >> 8) & 0xFF);
    inner.push_back(type & 0xFF);
    inner.push_back((body.data.size() >> 8) & 0xFF);
    inner.push_back(body.data.size() & 0xFF);
    inner.insert(inner.end(), body.data.begin(), body.data.end());
    std::vector<uint8_t> ct;
    if (!noise_.encrypt(inner.data(), inner.size(), ct)) {
      close();
      return false;
    }
    std::vector<uint8_t> frame;
    frame.reserve(ct.size() + 3);
    frame.push_back(0x01);
    frame.push_back((ct.size() >> 8) & 0xFF);
    frame.push_back(ct.size() & 0xFF);
    frame.insert(frame.end(), ct.begin(), ct.end());
    size_t n = client_.write(frame.data(), frame.size());
    if (n != frame.size()) {
      alive_ = false;
      return false;
    }
    return true;
  }
  std::vector<uint8_t> frame;
  frame.reserve(body.data.size() + 8);
  frame.push_back(0x00);  // plaintext indicator
  append_varint(frame, body.data.size());
  append_varint(frame, type);
  frame.insert(frame.end(), body.data.begin(), body.data.end());
  size_t n = client_.write(frame.data(), frame.size());
  if (n != frame.size()) {
    alive_ = false;
    return false;
  }
  return true;
}

void APIConnection::send_message(uint16_t type, const ProtoWriteBuffer &body) {
  (void)send_message_checked(type, body);
}

void APIConnection::send_empty(uint16_t type) {
  ProtoWriteBuffer empty;
  send_message(type, empty);
}

void APIConnection::reject_requires_encryption() {
  if (alive_ && client_.connected()) {
    const uint8_t ind = 0x01;
    client_.write(&ind, 1);
    client_.flush();
  }
  close();
}

void APIConnection::close() {
  if (alive_) {
    client_.stop();
    alive_ = false;
  }
}

}  // namespace esphome_api
