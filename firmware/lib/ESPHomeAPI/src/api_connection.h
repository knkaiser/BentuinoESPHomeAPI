// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

#pragma once
#include <cstdint>
#include <vector>

#include "crypto/noise.h"
#include "protobuf.h"

#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include "host_wifi.h"  // host simulator build (test/host_sim on the include path)
#endif

namespace esphome_api {

class APIServer;
class Entity;
class Event;
class Camera;
struct HomeAssistantServiceCall;

// One TCP client session. Implements plaintext framing and the connection
// lifecycle state machine (Hello -> Connect -> authenticated).
class APIConnection {
 public:
  APIConnection(APIServer *server, WiFiClient client);

  void loop();
  bool alive() const { return alive_; }

  // Push an entity state to this client (if subscribed and authenticated).
  void on_state_changed(Entity *entity);
  void send_entity_event(Event *entity, const std::string &event_type);
  void send_camera_image(Camera *camera, const uint8_t *data, size_t len,
                         bool done);
  bool send_log_message(int level, const std::string &message);
  // Send a HomeassistantServiceResponse (35) if this client subscribed.
  void send_homeassistant_service(const HomeAssistantServiceCall &call);

 private:
  enum class State : uint8_t { WAIT_HELLO, WAIT_CONNECT, CONNECTED };
  enum class Transport : uint8_t { UNKNOWN, PLAINTEXT, NOISE };
  enum class NoiseStage : uint8_t { CLIENT_HELLO, HANDSHAKE, READY };

  void read_into_buffer();
  void process_buffer();
  void process_plaintext();
  void process_noise();
  void handle_noise_frame(const uint8_t *body, size_t len);
  void send_server_hello();
  void send_noise_frame(const uint8_t *body, size_t len);
  void handle_message(uint16_t type, const uint8_t *payload, size_t len);
  void keepalive();

  void send_message(uint16_t type, const ProtoWriteBuffer &body);
  void send_empty(uint16_t type);

  void on_hello(const uint8_t *p, size_t len);
  void on_connect(const uint8_t *p, size_t len);
  void on_device_info();
  void on_list_entities();
  void on_subscribe_states();
  void on_entity_command(uint16_t type, const uint8_t *p, size_t len);
  void on_camera_image_request(const uint8_t *p, size_t len);
  void on_subscribe_logs(const uint8_t *p, size_t len);
  void on_subscribe_ha_services();
  void on_subscribe_ha_states();
  void on_ha_state_response(const uint8_t *p, size_t len);
  void on_get_time();
  void on_get_time_response(const uint8_t *p, size_t len);
  void request_time_from_client();
  void send_entity_state(Entity *entity);
  bool send_message_checked(uint16_t type, const ProtoWriteBuffer &body);

  void close();
  // Reply with Noise indicator so HA raises RequiresEncryptionAPIError.
  void reject_requires_encryption();

  APIServer *server_;
  WiFiClient client_;
  std::vector<uint8_t> rx_;

  State state_ = State::WAIT_HELLO;
  Transport transport_ = Transport::UNKNOWN;
  NoiseStage noise_stage_ = NoiseStage::CLIENT_HELLO;
  crypto::NoiseResponder noise_;
  bool alive_ = true;
  bool authenticated_ = false;
  bool states_subscribed_ = false;
  bool ha_services_subscribed_ = false;  // client wants service calls
  int log_subscription_ = -1;  // -1 = not subscribed; else minimum LogLevel

  uint32_t last_rx_ms_ = 0;
  uint32_t last_ping_ms_ = 0;
};

}  // namespace esphome_api
