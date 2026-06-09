#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "entity.h"

#if defined(ESP32)
#include <WiFi.h>
#elif defined(ESP8266)
#include <ESP8266WiFi.h>
#else
#include "host_wifi.h"  // host simulator build (test/host_sim on the include path)
#endif

namespace esphome_api {

class APIConnection;

// A single key/value pair in a Home Assistant service call.
using HAKeyValue = std::pair<std::string, std::string>;

// Describes a Home Assistant service call (or fired event when is_event) that
// the device asks HA to perform. Mirrors HomeassistantServiceResponse (35).
struct HomeAssistantServiceCall {
  std::string service;                     // e.g. "light.turn_on" or event name
  std::vector<HAKeyValue> data;            // static service data
  std::vector<HAKeyValue> data_template;   // Jinja-templated service data
  std::vector<HAKeyValue> variables;       // template variables
  bool is_event = false;                   // true => fire an event, not a call
};

// A Home Assistant entity state the device wants to track. The callback fires
// each time HA pushes a HomeAssistantStateResponse (40) for it.
struct HomeAssistantStateSubscription {
  std::string entity_id;   // e.g. "sun.sun"
  std::string attribute;   // empty => main state; else a specific attribute
  bool once = false;       // request the value once instead of subscribing
  std::function<void(const std::string &state)> on_state;
};

struct DeviceConfig {
  std::string name = "esp-device";
  std::string friendly_name = "ESP Device";
  std::string model = "custom";
  std::string manufacturer = "DIY";
  std::string esphome_version = "2024.1.0-cpp";
  std::string compilation_time = __DATE__ " " __TIME__;
  std::string mac_address;       // filled from WiFi if left empty
  std::string password = "";     // empty = no application password
  // Base64-encoded 32-byte Noise PSK (api.encryption.key). When set, the
  // device requires Noise-encrypted connections and rejects plaintext.
  std::string encryption_key = "";
  // After Connect, send GetTimeRequest to HA so the device can sync its clock.
  bool time_sync_from_ha = true;
  uint16_t port = 6053;
};

// ESPHome native API server (plaintext transport). Register entities, call
// begin() once WiFi is connected, then call loop() from the Arduino loop().
class APIServer {
 public:
  explicit APIServer(const DeviceConfig &cfg) : cfg_(cfg) {}

  void add_entity(Entity *entity);
  // Convenience aliases (all delegate to add_entity).
  void add_sensor(Sensor *e) { add_entity(e); }
  void add_binary_sensor(BinarySensor *e) { add_entity(e); }
  void add_text_sensor(TextSensor *e) { add_entity(e); }
  void add_switch(Switch *e) { add_entity(e); }
  void add_button(Button *e) { add_entity(e); }
  void add_cover(Cover *e) { add_entity(e); }
  void add_fan(Fan *e) { add_entity(e); }
  void add_light(Light *e) { add_entity(e); }
  void add_number(Number *e) { add_entity(e); }
  void add_select(Select *e) { add_entity(e); }
  void add_lock(Lock *e) { add_entity(e); }
  void add_climate(Climate *e) { add_entity(e); }
  void add_media_player(MediaPlayer *e) { add_entity(e); }
  void add_alarm_control_panel(AlarmControlPanel *e) { add_entity(e); }
  void add_text(Text *e) { add_entity(e); }
  void add_date(Date *e) { add_entity(e); }
  void add_time(TimeEntity *e) { add_entity(e); }
  void add_datetime(DateTime *e) { add_entity(e); }
  void add_valve(Valve *e) { add_entity(e); }
  void add_update(Update *e) { add_entity(e); }
  void add_event(Event *e) { add_entity(e); }
  void add_camera(Camera *e) { add_entity(e); }

  void begin();
  void loop();

  // Called by entities when their state changes.
  void push_state(Entity *entity);
  void push_event(Event *entity, const std::string &event_type);
  void push_camera_image(Camera *camera, const uint8_t *data, size_t len,
                         bool done);

  // Stream a log line to clients that subscribed via SubscribeLogsRequest.
  void log(uint32_t level, const std::string &message);
  void logf(uint32_t level, const char *fmt, ...);
  // Send a CONFIG-level device/entity summary to subscribed log clients.
  void dump_config();

  // Mirror complete Serial lines to log() (see serial_log_bridge.h).
  void enable_serial_log_bridge(uint32_t level);
  void disable_serial_log_bridge();

  // --- Home Assistant integration helpers ---------------------------------
  // Ask Home Assistant to call a service (or fire an event). Delivered to
  // every client that issued SubscribeHomeassistantServicesRequest (34).
  void call_homeassistant_service(const HomeAssistantServiceCall &call);
  void call_homeassistant_service(const std::string &service,
                                  const std::vector<HAKeyValue> &data = {});
  // Fire a Home Assistant event (is_event = true).
  void fire_homeassistant_event(const std::string &event,
                                const std::vector<HAKeyValue> &data = {});

  // Track a Home Assistant entity (or one of its attributes). `on_state` is
  // invoked whenever HA pushes the value. Register before begin().
  void subscribe_homeassistant_state(
      const std::string &entity_id, const std::string &attribute,
      std::function<void(const std::string &state)> on_state,
      bool once = false);
  void subscribe_homeassistant_state(
      const std::string &entity_id,
      std::function<void(const std::string &state)> on_state) {
    subscribe_homeassistant_state(entity_id, std::string(), std::move(on_state));
  }

  const std::vector<HomeAssistantStateSubscription> &
  ha_state_subscriptions() const {
    return ha_state_subs_;
  }
  // Dispatch an inbound HomeAssistantStateResponse (40) to matching callbacks.
  void on_homeassistant_state(const std::string &entity_id,
                              const std::string &attribute,
                              const std::string &state);

  Entity *find_by_key(uint32_t key);
  void dispatch_camera_image_request(bool single, bool stream);

  const DeviceConfig &config() const { return cfg_; }
  const std::vector<Entity *> &entities() const { return entities_; }

  // Noise transport configuration.
  bool encryption_enabled() const { return encryption_enabled_; }
  const uint8_t *psk() const { return psk_; }

 private:
  DeviceConfig cfg_;
  WiFiServer *server_ = nullptr;
  std::vector<Entity *> entities_;
  std::vector<APIConnection *> conns_;
  uint8_t psk_[32] = {0};
  bool encryption_enabled_ = false;
  std::vector<HomeAssistantStateSubscription> ha_state_subs_;
};

}  // namespace esphome_api
