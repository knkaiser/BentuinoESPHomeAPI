#pragma once
// Portable entity model for the ESPHome native API (device/server side).
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "api_message_types.h"
#include "fnv1.h"
#include "platform_nvs.h"
#include "protobuf.h"

namespace esphome_api {

class APIServer;

class Entity {
 public:
  virtual ~Entity() = default;

  void set_object_id(const std::string &oid) {
    object_id_ = oid;
    key_ = fnv1_hash(oid);
    if (unique_id_.empty()) unique_id_ = oid;
  }
  void set_name(const std::string &name) { name_ = name; }
  void set_unique_id(const std::string &uid) { unique_id_ = uid; }
  void set_icon(const std::string &icon) { icon_ = icon; }
  void set_entity_category(uint32_t cat) { entity_category_ = cat; }
  void set_disabled_by_default(bool d) { disabled_by_default_ = d; }

  const std::string &object_id() const { return object_id_; }
  const std::string &name() const { return name_; }
  uint32_t key() const { return key_; }

  virtual uint16_t list_type() const = 0;
  virtual void encode_list(ProtoWriteBuffer &b) const = 0;
  virtual uint16_t state_type() const = 0;
  virtual void encode_state(ProtoWriteBuffer &b) const = 0;
  virtual const char *kind() const = 0;

  // One-time initialization after registration (e.g. restore persisted state).
  // Called once by APIServer::begin(). Default: nothing.
  virtual void setup() {}

  // Handle an incoming *CommandRequest for this entity. Returns true if handled.
  virtual bool handle_command(uint16_t msg_type, const uint8_t *payload,
                              size_t len) {
    (void)msg_type;
    (void)payload;
    (void)len;
    return false;
  }

  void set_server(APIServer *s) { server_ = s; }

 protected:
  void encode_common(ProtoWriteBuffer &b) const {
    b.encode_string(1, object_id_);
    b.encode_fixed32(2, key_, true);
    b.encode_string(3, name_);
    b.encode_string(4, unique_id_);
  }
  void notify_changed();

  std::string object_id_;
  std::string name_;
  std::string unique_id_;
  std::string icon_;
  uint32_t key_ = 0;
  uint32_t entity_category_ = 0;
  bool disabled_by_default_ = false;
  APIServer *server_ = nullptr;
};

// All concrete entity classes.
#include "entity_types.h"

}  // namespace esphome_api
