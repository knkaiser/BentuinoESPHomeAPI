// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

#pragma once
// Concrete ESPHome entity types. Included from entity.h inside namespace
// esphome_api.

// --------------------------------------------------------------------------
// Sensor (float)
// --------------------------------------------------------------------------
class Sensor : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_SENSOR_RESPONSE; }
  uint16_t state_type() const override { return MSG_SENSOR_STATE_RESPONSE; }
  const char *kind() const override { return "sensor"; }

  void set_unit_of_measurement(const std::string &u) { unit_ = u; }
  void set_accuracy_decimals(int32_t d) { accuracy_decimals_ = d; }
  void set_device_class(const std::string &dc) { device_class_ = dc; }
  void set_state_class(uint32_t sc) { state_class_ = sc; }
  void set_force_update(bool f) { force_update_ = f; }

  void publish_state(float value) {
    state_ = value;
    has_state_ = !std::isnan(value);
    notify_changed();
  }
  float state() const { return state_; }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, icon_);
    b.encode_string(6, unit_);
    b.encode_int32(7, accuracy_decimals_);
    b.encode_bool(8, force_update_);
    b.encode_string(9, device_class_);
    b.encode_enum(10, state_class_);
    b.encode_bool(12, disabled_by_default_);
    b.encode_enum(13, entity_category_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_float(2, state_);
    b.encode_bool(3, !has_state_);
  }

 private:
  float state_ = NAN;
  bool has_state_ = false;
  bool force_update_ = false;
  int32_t accuracy_decimals_ = 1;
  uint32_t state_class_ = 0;
  std::string unit_;
  std::string device_class_;
};

// --------------------------------------------------------------------------
// BinarySensor (bool)
// --------------------------------------------------------------------------
class BinarySensor : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_BINARY_SENSOR_RESPONSE; }
  uint16_t state_type() const override { return MSG_BINARY_SENSOR_STATE_RESPONSE; }
  const char *kind() const override { return "binary_sensor"; }

  void set_device_class(const std::string &dc) { device_class_ = dc; }

  void publish_state(bool value) {
    state_ = value;
    has_state_ = true;
    notify_changed();
  }
  bool state() const { return state_; }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, device_class_);
    b.encode_string(8, icon_);
    b.encode_enum(9, entity_category_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_bool(2, state_);
    b.encode_bool(3, !has_state_);
  }

 private:
  bool state_ = false;
  bool has_state_ = false;
  std::string device_class_;
};

// --------------------------------------------------------------------------
// TextSensor (string)
// --------------------------------------------------------------------------
class TextSensor : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_TEXT_SENSOR_RESPONSE; }
  uint16_t state_type() const override { return MSG_TEXT_SENSOR_STATE_RESPONSE; }
  const char *kind() const override { return "text_sensor"; }

  void set_device_class(const std::string &dc) { device_class_ = dc; }

  void publish_state(const std::string &value) {
    state_ = value;
    has_state_ = true;
    notify_changed();
  }
  const std::string &state() const { return state_; }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, icon_);
    b.encode_bool(6, disabled_by_default_);
    b.encode_enum(7, entity_category_);
    b.encode_string(8, device_class_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_string(2, state_);
    b.encode_bool(3, !has_state_);
  }

 private:
  std::string state_;
  bool has_state_ = false;
  std::string device_class_;
};

// --------------------------------------------------------------------------
// Switch (controllable bool)
// --------------------------------------------------------------------------
class Switch : public Entity {
 public:
  // How the switch chooses its state at boot. Only RESTORE_* modes persist the
  // value to non-volatile storage; ALWAYS_*/NO_RESTORE never write.
  enum class RestoreMode : uint8_t {
    NO_RESTORE,                    // volatile; boots OFF (default, legacy)
    RESTORE_DEFAULT_OFF,           // restore saved; if none, OFF
    RESTORE_DEFAULT_ON,            // restore saved; if none, ON
    ALWAYS_OFF,                    // ignore saved, force OFF
    ALWAYS_ON,                     // ignore saved, force ON
    RESTORE_INVERTED_DEFAULT_OFF,  // restore inverse of saved; if none, OFF
    RESTORE_INVERTED_DEFAULT_ON,   // restore inverse of saved; if none, ON
  };

  uint16_t list_type() const override { return MSG_LIST_ENTITIES_SWITCH_RESPONSE; }
  uint16_t state_type() const override { return MSG_SWITCH_STATE_RESPONSE; }
  const char *kind() const override { return "switch"; }

  void set_device_class(const std::string &dc) { device_class_ = dc; }
  void set_assumed_state(bool a) { assumed_state_ = a; }
  void set_restore_mode(RestoreMode m) { restore_mode_ = m; }
  std::function<void(bool)> on_control;

  void control(bool value) {
    if (on_control) on_control(value);
    publish_state(value);
  }
  void publish_state(bool value) {
    state_ = value;
    if (persists()) nvs::save_u8(key_, value ? 1 : 0);
    notify_changed();
  }
  bool state() const { return state_; }

  // Apply restore_mode once at startup (called by APIServer::begin()).
  void setup() override {
    bool target;
    switch (restore_mode_) {
      case RestoreMode::NO_RESTORE:
        return;  // keep default state_, never persisted
      case RestoreMode::ALWAYS_OFF:
        target = false;
        break;
      case RestoreMode::ALWAYS_ON:
        target = true;
        break;
      default: {
        const bool inverted =
            restore_mode_ == RestoreMode::RESTORE_INVERTED_DEFAULT_OFF ||
            restore_mode_ == RestoreMode::RESTORE_INVERTED_DEFAULT_ON;
        const bool default_on =
            restore_mode_ == RestoreMode::RESTORE_DEFAULT_ON ||
            restore_mode_ == RestoreMode::RESTORE_INVERTED_DEFAULT_ON;
        uint8_t saved = 0;
        if (nvs::load_u8(key_, saved))
          target = inverted ? (saved == 0) : (saved != 0);
        else
          target = default_on;
        break;
      }
    }
    control(target);  // drives on_control + persists (for RESTORE_* modes)
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    if (msg_type != MSG_SWITCH_COMMAND_REQUEST) return false;
    bool value = false;
    ProtoDecoder dec(p, len);
    ProtoField f;
    while (dec.next(f)) {
      if (f.field == 2 && f.wire == WIRE_VARINT) value = f.as_bool();
    }
    control(value);
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, icon_);
    b.encode_bool(6, assumed_state_);
    b.encode_bool(7, disabled_by_default_);
    b.encode_enum(8, entity_category_);
    b.encode_string(9, device_class_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_bool(2, state_);
  }

 private:
  bool persists() const {
    return restore_mode_ != RestoreMode::NO_RESTORE &&
           restore_mode_ != RestoreMode::ALWAYS_OFF &&
           restore_mode_ != RestoreMode::ALWAYS_ON;
  }

  bool state_ = false;
  bool assumed_state_ = false;
  std::string device_class_;
  RestoreMode restore_mode_ = RestoreMode::NO_RESTORE;
};

// --------------------------------------------------------------------------
// Button (stateless, command only)
// --------------------------------------------------------------------------
class Button : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_BUTTON_RESPONSE; }
  uint16_t state_type() const override { return 0; }
  const char *kind() const override { return "button"; }

  void set_device_class(const std::string &dc) { device_class_ = dc; }
  std::function<void()> on_press;

  void press() {
    if (on_press) on_press();
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    (void)p;
    (void)len;
    if (msg_type != MSG_BUTTON_COMMAND_REQUEST) return false;
    press();
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, icon_);
    b.encode_bool(6, disabled_by_default_);
    b.encode_enum(7, entity_category_);
    b.encode_string(8, device_class_);
  }
  void encode_state(ProtoWriteBuffer &) const override {}

 private:
  std::string device_class_;
};

// --------------------------------------------------------------------------
// Cover
// --------------------------------------------------------------------------
struct CoverCommand {
  bool has_position = false;
  float position = 0;
  bool has_tilt = false;
  float tilt = 0;
  bool stop = false;
  bool has_legacy_command = false;
  uint32_t legacy_command = 0;
};

class Cover : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_COVER_RESPONSE; }
  uint16_t state_type() const override { return MSG_COVER_STATE_RESPONSE; }
  const char *kind() const override { return "cover"; }

  void set_assumed_state(bool v) { assumed_state_ = v; }
  void set_supports_position(bool v) { supports_position_ = v; }
  void set_supports_tilt(bool v) { supports_tilt_ = v; }
  void set_supports_stop(bool v) { supports_stop_ = v; }
  void set_device_class(const std::string &dc) { device_class_ = dc; }
  std::function<void(const CoverCommand &)> on_command;

  void publish_state(float position, float tilt = 0, uint32_t operation = 0) {
    position_ = position;
    tilt_ = tilt;
    operation_ = operation;
    notify_changed();
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    if (msg_type != MSG_COVER_COMMAND_REQUEST) return false;
    CoverCommand cmd;
    ProtoDecoder dec(p, len);
    ProtoField f;
    while (dec.next(f)) {
      if (f.field == 2 && f.wire == WIRE_VARINT) cmd.has_legacy_command = f.as_bool();
      if (f.field == 3 && f.wire == WIRE_VARINT) cmd.legacy_command = (uint32_t)f.as_varint;
      if (f.field == 4 && f.wire == WIRE_VARINT) cmd.has_position = f.as_bool();
      if (f.field == 5 && f.wire == WIRE_FIXED32) cmd.position = f.as_float();
      if (f.field == 6 && f.wire == WIRE_VARINT) cmd.has_tilt = f.as_bool();
      if (f.field == 7 && f.wire == WIRE_FIXED32) cmd.tilt = f.as_float();
      if (f.field == 8 && f.wire == WIRE_VARINT) cmd.stop = f.as_bool();
    }
    if (on_command) on_command(cmd);
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_bool(5, assumed_state_);
    b.encode_bool(6, supports_position_);
    b.encode_bool(7, supports_tilt_);
    b.encode_string(8, device_class_);
    b.encode_bool(9, disabled_by_default_);
    b.encode_string(10, icon_);
    b.encode_enum(11, entity_category_);
    b.encode_bool(12, supports_stop_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_enum(2, 0);  // legacy_state OPEN
    b.encode_float(3, position_);
    b.encode_float(4, tilt_);
    b.encode_enum(5, operation_);
  }

 private:
  bool assumed_state_ = false;
  bool supports_position_ = true;
  bool supports_tilt_ = false;
  bool supports_stop_ = true;
  float position_ = 0;
  float tilt_ = 0;
  uint32_t operation_ = 0;
  std::string device_class_;
};

// --------------------------------------------------------------------------
// Fan
// --------------------------------------------------------------------------
struct FanCommand {
  bool has_state = false;
  bool state = false;
  bool has_oscillating = false;
  bool oscillating = false;
  bool has_direction = false;
  uint32_t direction = 0;
  bool has_speed_level = false;
  int32_t speed_level = 0;
  bool has_preset_mode = false;
  std::string preset_mode;
};

class Fan : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_FAN_RESPONSE; }
  uint16_t state_type() const override { return MSG_FAN_STATE_RESPONSE; }
  const char *kind() const override { return "fan"; }

  void set_supports_oscillation(bool v) { supports_oscillation_ = v; }
  void set_supports_speed(bool v) { supports_speed_ = v; }
  void set_supports_direction(bool v) { supports_direction_ = v; }
  void set_supported_speed_count(int32_t n) { supported_speed_count_ = n; }
  void set_supported_preset_modes(const std::vector<std::string> &m) {
    preset_modes_ = m;
  }
  std::function<void(const FanCommand &)> on_command;

  void publish_state(bool on, bool oscillating = false, int32_t speed_level = 0,
                     uint32_t direction = 0, const std::string &preset = "") {
    state_ = on;
    oscillating_ = oscillating;
    speed_level_ = speed_level;
    direction_ = direction;
    preset_mode_ = preset;
    notify_changed();
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    if (msg_type != MSG_FAN_COMMAND_REQUEST) return false;
    FanCommand cmd;
    ProtoDecoder dec(p, len);
    ProtoField f;
    while (dec.next(f)) {
      if (f.field == 2 && f.wire == WIRE_VARINT) cmd.has_state = f.as_bool();
      if (f.field == 3 && f.wire == WIRE_VARINT) cmd.state = f.as_bool();
      if (f.field == 6 && f.wire == WIRE_VARINT) cmd.has_oscillating = f.as_bool();
      if (f.field == 7 && f.wire == WIRE_VARINT) cmd.oscillating = f.as_bool();
      if (f.field == 8 && f.wire == WIRE_VARINT) cmd.has_direction = f.as_bool();
      if (f.field == 9 && f.wire == WIRE_VARINT) cmd.direction = (uint32_t)f.as_varint;
      if (f.field == 10 && f.wire == WIRE_VARINT) cmd.has_speed_level = f.as_bool();
      if (f.field == 11 && f.wire == WIRE_VARINT) cmd.speed_level = (int32_t)f.as_varint;
      if (f.field == 12 && f.wire == WIRE_VARINT) cmd.has_preset_mode = f.as_bool();
      if (f.field == 13 && f.wire == WIRE_LEN) cmd.preset_mode = f.as_string();
    }
    if (on_command) on_command(cmd);
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_bool(5, supports_oscillation_);
    b.encode_bool(6, supports_speed_);
    b.encode_bool(7, supports_direction_);
    b.encode_int32(8, supported_speed_count_);
    b.encode_bool(9, disabled_by_default_);
    b.encode_string(10, icon_);
    b.encode_enum(11, entity_category_);
    b.encode_repeated_string(12, preset_modes_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_bool(2, state_);
    b.encode_bool(3, oscillating_);
    b.encode_enum(5, direction_);
    b.encode_int32(6, speed_level_);
    b.encode_string(7, preset_mode_);
  }

 private:
  bool state_ = false;
  bool oscillating_ = false;
  int32_t speed_level_ = 0;
  uint32_t direction_ = 0;
  std::string preset_mode_;
  bool supports_oscillation_ = false;
  bool supports_speed_ = true;
  bool supports_direction_ = false;
  int32_t supported_speed_count_ = 3;
  std::vector<std::string> preset_modes_;
};

// --------------------------------------------------------------------------
// Light
// --------------------------------------------------------------------------
struct LightCommand {
  bool has_state = false;
  bool state = false;
  bool has_brightness = false;
  float brightness = 0;
  bool has_color_mode = false;
  uint32_t color_mode = 0;
  bool has_rgb = false;
  float red = 0, green = 0, blue = 0;
  bool has_white = false;
  float white = 0;
  bool has_color_temperature = false;
  float color_temperature = 0;
  bool has_cold_white = false;
  float cold_white = 0;
  bool has_warm_white = false;
  float warm_white = 0;
  bool has_effect = false;
  std::string effect;
};

class Light : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_LIGHT_RESPONSE; }
  uint16_t state_type() const override { return MSG_LIGHT_STATE_RESPONSE; }
  const char *kind() const override { return "light"; }

  void set_supported_color_modes(const std::vector<uint32_t> &modes) {
    color_modes_ = modes;
  }
  void set_min_mireds(float v) { min_mireds_ = v; }
  void set_max_mireds(float v) { max_mireds_ = v; }
  void set_effects(const std::vector<std::string> &e) { effects_ = e; }
  std::function<void(const LightCommand &)> on_command;

  void publish_state(bool on, float brightness = 1.0f, uint32_t color_mode = 1,
                     float r = 0, float g = 0, float b = 0) {
    state_ = on;
    brightness_ = brightness;
    color_mode_ = color_mode;
    red_ = r;
    green_ = g;
    blue_ = b;
    notify_changed();
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    if (msg_type != MSG_LIGHT_COMMAND_REQUEST) return false;
    LightCommand cmd;
    ProtoDecoder dec(p, len);
    ProtoField f;
    while (dec.next(f)) {
      if (f.field == 2 && f.wire == WIRE_VARINT) cmd.has_state = f.as_bool();
      if (f.field == 3 && f.wire == WIRE_VARINT) cmd.state = f.as_bool();
      if (f.field == 4 && f.wire == WIRE_VARINT) cmd.has_brightness = f.as_bool();
      if (f.field == 5 && f.wire == WIRE_FIXED32) cmd.brightness = f.as_float();
      if (f.field == 22 && f.wire == WIRE_VARINT) cmd.has_color_mode = f.as_bool();
      if (f.field == 23 && f.wire == WIRE_VARINT) cmd.color_mode = (uint32_t)f.as_varint;
      if (f.field == 6 && f.wire == WIRE_VARINT) cmd.has_rgb = f.as_bool();
      if (f.field == 7 && f.wire == WIRE_FIXED32) cmd.red = f.as_float();
      if (f.field == 8 && f.wire == WIRE_FIXED32) cmd.green = f.as_float();
      if (f.field == 9 && f.wire == WIRE_FIXED32) cmd.blue = f.as_float();
      if (f.field == 10 && f.wire == WIRE_VARINT) cmd.has_white = f.as_bool();
      if (f.field == 11 && f.wire == WIRE_FIXED32) cmd.white = f.as_float();
      if (f.field == 12 && f.wire == WIRE_VARINT) cmd.has_color_temperature = f.as_bool();
      if (f.field == 13 && f.wire == WIRE_FIXED32) cmd.color_temperature = f.as_float();
      if (f.field == 24 && f.wire == WIRE_VARINT) cmd.has_cold_white = f.as_bool();
      if (f.field == 25 && f.wire == WIRE_FIXED32) cmd.cold_white = f.as_float();
      if (f.field == 26 && f.wire == WIRE_VARINT) cmd.has_warm_white = f.as_bool();
      if (f.field == 27 && f.wire == WIRE_FIXED32) cmd.warm_white = f.as_float();
      if (f.field == 18 && f.wire == WIRE_VARINT) cmd.has_effect = f.as_bool();
      if (f.field == 19 && f.wire == WIRE_LEN) cmd.effect = f.as_string();
    }
    if (on_command) on_command(cmd);
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_repeated_uint32(12, color_modes_);
    b.encode_float(9, min_mireds_);
    b.encode_float(10, max_mireds_);
    b.encode_repeated_string(11, effects_);
    b.encode_bool(13, disabled_by_default_);
    b.encode_string(14, icon_);
    b.encode_enum(15, entity_category_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_bool(2, state_);
    b.encode_float(3, brightness_);
    b.encode_enum(11, color_mode_);
    b.encode_float(4, red_);
    b.encode_float(5, green_);
    b.encode_float(6, blue_);
    b.encode_string(9, effect_);
  }

 private:
  bool state_ = false;
  float brightness_ = 1.0f;
  uint32_t color_mode_ = 1;
  float red_ = 0, green_ = 0, blue_ = 0;
  std::string effect_;
  float min_mireds_ = 153;
  float max_mireds_ = 500;
  std::vector<uint32_t> color_modes_{1};  // ON_OFF
  std::vector<std::string> effects_;
};

// --------------------------------------------------------------------------
// Number
// --------------------------------------------------------------------------
class Number : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_NUMBER_RESPONSE; }
  uint16_t state_type() const override { return MSG_NUMBER_STATE_RESPONSE; }
  const char *kind() const override { return "number"; }

  void set_min_value(float v) { min_value_ = v; }
  void set_max_value(float v) { max_value_ = v; }
  void set_step(float v) { step_ = v; }
  void set_unit_of_measurement(const std::string &u) { unit_ = u; }
  void set_mode(uint32_t m) { mode_ = m; }
  void set_device_class(const std::string &dc) { device_class_ = dc; }
  std::function<void(float)> on_control;

  void control(float value) {
    if (on_control) on_control(value);
    publish_state(value);
  }
  void publish_state(float value) {
    state_ = value;
    has_state_ = true;
    notify_changed();
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    if (msg_type != MSG_NUMBER_COMMAND_REQUEST) return false;
    float value = 0;
    ProtoDecoder dec(p, len);
    ProtoField f;
    while (dec.next(f)) {
      if (f.field == 2 && f.wire == WIRE_FIXED32) value = f.as_float();
    }
    control(value);
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, icon_);
    b.encode_float(6, min_value_);
    b.encode_float(7, max_value_);
    b.encode_float(8, step_);
    b.encode_bool(9, disabled_by_default_);
    b.encode_enum(10, entity_category_);
    b.encode_string(11, unit_);
    b.encode_enum(12, mode_);
    b.encode_string(13, device_class_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_float(2, state_);
    b.encode_bool(3, !has_state_);
  }

 private:
  float state_ = 0;
  bool has_state_ = false;
  float min_value_ = 0;
  float max_value_ = 100;
  float step_ = 1;
  uint32_t mode_ = 0;
  std::string unit_;
  std::string device_class_;
};

// --------------------------------------------------------------------------
// Select
// --------------------------------------------------------------------------
class Select : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_SELECT_RESPONSE; }
  uint16_t state_type() const override { return MSG_SELECT_STATE_RESPONSE; }
  const char *kind() const override { return "select"; }

  void set_options(const std::vector<std::string> &opts) { options_ = opts; }
  std::function<void(const std::string &)> on_control;

  void control(const std::string &value) {
    if (on_control) on_control(value);
    publish_state(value);
  }
  void publish_state(const std::string &value) {
    state_ = value;
    has_state_ = true;
    notify_changed();
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    if (msg_type != MSG_SELECT_COMMAND_REQUEST) return false;
    std::string value;
    ProtoDecoder dec(p, len);
    ProtoField f;
    while (dec.next(f)) {
      if (f.field == 2 && f.wire == WIRE_LEN) value = f.as_string();
    }
    control(value);
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, icon_);
    b.encode_repeated_string(6, options_);
    b.encode_bool(7, disabled_by_default_);
    b.encode_enum(8, entity_category_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_string(2, state_);
    b.encode_bool(3, !has_state_);
  }

 private:
  std::string state_;
  bool has_state_ = false;
  std::vector<std::string> options_;
};

// --------------------------------------------------------------------------
// Lock
// --------------------------------------------------------------------------
class Lock : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_LOCK_RESPONSE; }
  uint16_t state_type() const override { return MSG_LOCK_STATE_RESPONSE; }
  const char *kind() const override { return "lock"; }

  void set_assumed_state(bool v) { assumed_state_ = v; }
  void set_supports_open(bool v) { supports_open_ = v; }
  std::function<void(uint32_t command)> on_command;  // LockCommand enum

  void publish_state(uint32_t state) {
    state_ = state;
    notify_changed();
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    if (msg_type != MSG_LOCK_COMMAND_REQUEST) return false;
    uint32_t command = 0;
    ProtoDecoder dec(p, len);
    ProtoField f;
    while (dec.next(f)) {
      if (f.field == 2 && f.wire == WIRE_VARINT) command = (uint32_t)f.as_varint;
    }
    if (on_command) on_command(command);
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, icon_);
    b.encode_bool(6, disabled_by_default_);
    b.encode_enum(7, entity_category_);
    b.encode_bool(8, assumed_state_);
    b.encode_bool(9, supports_open_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_enum(2, state_);
  }

 private:
  uint32_t state_ = 1;  // LOCKED
  bool assumed_state_ = false;
  bool supports_open_ = false;
};

// --------------------------------------------------------------------------
// Climate
// --------------------------------------------------------------------------
struct ClimateCommand {
  bool has_mode = false;
  uint32_t mode = 0;
  bool has_target_temperature = false;
  float target_temperature = 0;
  bool has_target_temperature_low = false;
  float target_temperature_low = 0;
  bool has_target_temperature_high = false;
  float target_temperature_high = 0;
  bool has_fan_mode = false;
  uint32_t fan_mode = 0;
  bool has_swing_mode = false;
  uint32_t swing_mode = 0;
  bool has_custom_fan_mode = false;
  std::string custom_fan_mode;
  bool has_preset = false;
  uint32_t preset = 0;
  bool has_custom_preset = false;
  std::string custom_preset;
  bool has_target_humidity = false;
  float target_humidity = 0;
};

class Climate : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_CLIMATE_RESPONSE; }
  uint16_t state_type() const override { return MSG_CLIMATE_STATE_RESPONSE; }
  const char *kind() const override { return "climate"; }

  void set_supported_modes(const std::vector<uint32_t> &m) { supported_modes_ = m; }
  void set_visual_min_temperature(float v) { visual_min_ = v; }
  void set_visual_max_temperature(float v) { visual_max_ = v; }
  void set_supports_current_temperature(bool v) { supports_current_temp_ = v; }
  void set_supports_two_point_target_temperature(bool v) { supports_two_point_ = v; }
  std::function<void(const ClimateCommand &)> on_command;

  void publish_state(uint32_t mode, float current_temp, float target_temp) {
    mode_ = mode;
    current_temp_ = current_temp;
    target_temp_ = target_temp;
    notify_changed();
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    if (msg_type != MSG_CLIMATE_COMMAND_REQUEST) return false;
    ClimateCommand cmd;
    ProtoDecoder dec(p, len);
    ProtoField f;
    while (dec.next(f)) {
      if (f.field == 2 && f.wire == WIRE_VARINT) cmd.has_mode = f.as_bool();
      if (f.field == 3 && f.wire == WIRE_VARINT) cmd.mode = (uint32_t)f.as_varint;
      if (f.field == 4 && f.wire == WIRE_VARINT) cmd.has_target_temperature = f.as_bool();
      if (f.field == 5 && f.wire == WIRE_FIXED32) cmd.target_temperature = f.as_float();
      if (f.field == 6 && f.wire == WIRE_VARINT) cmd.has_target_temperature_low = f.as_bool();
      if (f.field == 7 && f.wire == WIRE_FIXED32) cmd.target_temperature_low = f.as_float();
      if (f.field == 8 && f.wire == WIRE_VARINT) cmd.has_target_temperature_high = f.as_bool();
      if (f.field == 9 && f.wire == WIRE_FIXED32) cmd.target_temperature_high = f.as_float();
      if (f.field == 12 && f.wire == WIRE_VARINT) cmd.has_fan_mode = f.as_bool();
      if (f.field == 13 && f.wire == WIRE_VARINT) cmd.fan_mode = (uint32_t)f.as_varint;
      if (f.field == 14 && f.wire == WIRE_VARINT) cmd.has_swing_mode = f.as_bool();
      if (f.field == 15 && f.wire == WIRE_VARINT) cmd.swing_mode = (uint32_t)f.as_varint;
      if (f.field == 16 && f.wire == WIRE_VARINT) cmd.has_custom_fan_mode = f.as_bool();
      if (f.field == 17 && f.wire == WIRE_LEN) cmd.custom_fan_mode = f.as_string();
      if (f.field == 18 && f.wire == WIRE_VARINT) cmd.has_preset = f.as_bool();
      if (f.field == 19 && f.wire == WIRE_VARINT) cmd.preset = (uint32_t)f.as_varint;
      if (f.field == 20 && f.wire == WIRE_VARINT) cmd.has_custom_preset = f.as_bool();
      if (f.field == 21 && f.wire == WIRE_LEN) cmd.custom_preset = f.as_string();
      if (f.field == 22 && f.wire == WIRE_VARINT) cmd.has_target_humidity = f.as_bool();
      if (f.field == 23 && f.wire == WIRE_FIXED32) cmd.target_humidity = f.as_float();
    }
    if (on_command) on_command(cmd);
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_bool(5, supports_current_temp_);
    b.encode_bool(6, supports_two_point_);
    b.encode_repeated_uint32(7, supported_modes_);
    b.encode_float(8, visual_min_);
    b.encode_float(9, visual_max_);
    b.encode_bool(18, disabled_by_default_);
    b.encode_string(19, icon_);
    b.encode_enum(20, entity_category_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_enum(2, mode_);
    b.encode_float(3, current_temp_);
    b.encode_float(4, target_temp_);
  }

 private:
  uint32_t mode_ = 0;
  float current_temp_ = 21;
  float target_temp_ = 21;
  bool supports_current_temp_ = true;
  bool supports_two_point_ = false;
  float visual_min_ = 7;
  float visual_max_ = 35;
  std::vector<uint32_t> supported_modes_{0, 3};  // OFF, HEAT
};

// --------------------------------------------------------------------------
// MediaPlayer
// --------------------------------------------------------------------------
struct MediaPlayerCommand {
  bool has_command = false;
  uint32_t command = 0;
  bool has_volume = false;
  float volume = 0;
  bool has_media_url = false;
  std::string media_url;
  bool has_announcement = false;
  bool announcement = false;
};

class MediaPlayer : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_MEDIA_PLAYER_RESPONSE; }
  uint16_t state_type() const override { return MSG_MEDIA_PLAYER_STATE_RESPONSE; }
  const char *kind() const override { return "media_player"; }

  void set_supports_pause(bool v) { supports_pause_ = v; }
  std::function<void(const MediaPlayerCommand &)> on_command;

  void publish_state(uint32_t state, float volume = 0.5f, bool muted = false) {
    state_ = state;
    volume_ = volume;
    muted_ = muted;
    notify_changed();
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    if (msg_type != MSG_MEDIA_PLAYER_COMMAND_REQUEST) return false;
    MediaPlayerCommand cmd;
    ProtoDecoder dec(p, len);
    ProtoField f;
    while (dec.next(f)) {
      if (f.field == 2 && f.wire == WIRE_VARINT) cmd.has_command = f.as_bool();
      if (f.field == 3 && f.wire == WIRE_VARINT) cmd.command = (uint32_t)f.as_varint;
      if (f.field == 4 && f.wire == WIRE_VARINT) cmd.has_volume = f.as_bool();
      if (f.field == 5 && f.wire == WIRE_FIXED32) cmd.volume = f.as_float();
      if (f.field == 6 && f.wire == WIRE_VARINT) cmd.has_media_url = f.as_bool();
      if (f.field == 7 && f.wire == WIRE_LEN) cmd.media_url = f.as_string();
      if (f.field == 8 && f.wire == WIRE_VARINT) cmd.has_announcement = f.as_bool();
      if (f.field == 9 && f.wire == WIRE_VARINT) cmd.announcement = f.as_bool();
    }
    if (on_command) on_command(cmd);
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, icon_);
    b.encode_bool(6, disabled_by_default_);
    b.encode_enum(7, entity_category_);
    b.encode_bool(8, supports_pause_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_enum(2, state_);
    b.encode_float(3, volume_);
    b.encode_bool(4, muted_);
  }

 private:
  uint32_t state_ = 1;  // IDLE
  float volume_ = 0.5f;
  bool muted_ = false;
  bool supports_pause_ = true;
};

// --------------------------------------------------------------------------
// AlarmControlPanel
// --------------------------------------------------------------------------
class AlarmControlPanel : public Entity {
 public:
  uint16_t list_type() const override {
    return MSG_LIST_ENTITIES_ALARM_CONTROL_PANEL_RESPONSE;
  }
  uint16_t state_type() const override { return MSG_ALARM_CONTROL_PANEL_STATE_RESPONSE; }
  const char *kind() const override { return "alarm_control_panel"; }

  void set_supported_features(uint32_t f) { supported_features_ = f; }
  std::function<void(uint32_t command, const std::string &code)> on_command;

  void publish_state(uint32_t state) {
    state_ = state;
    notify_changed();
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    if (msg_type != MSG_ALARM_CONTROL_PANEL_COMMAND_REQUEST) return false;
    uint32_t command = 0;
    std::string code;
    ProtoDecoder dec(p, len);
    ProtoField f;
    while (dec.next(f)) {
      if (f.field == 2 && f.wire == WIRE_VARINT) command = (uint32_t)f.as_varint;
      if (f.field == 3 && f.wire == WIRE_LEN) code = f.as_string();
    }
    if (on_command) on_command(command, code);
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, icon_);
    b.encode_bool(6, disabled_by_default_);
    b.encode_enum(7, entity_category_);
    b.encode_uint32(8, supported_features_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_enum(2, state_);
  }

 private:
  uint32_t state_ = 0;
  uint32_t supported_features_ = 0;
};

// --------------------------------------------------------------------------
// Text (editable string)
// --------------------------------------------------------------------------
class Text : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_TEXT_RESPONSE; }
  uint16_t state_type() const override { return MSG_TEXT_STATE_RESPONSE; }
  const char *kind() const override { return "text"; }

  void set_min_length(uint32_t v) { min_length_ = v; }
  void set_max_length(uint32_t v) { max_length_ = v; }
  void set_pattern(const std::string &p) { pattern_ = p; }
  void set_mode(uint32_t m) { mode_ = m; }
  std::function<void(const std::string &)> on_control;

  void control(const std::string &value) {
    if (on_control) on_control(value);
    publish_state(value);
  }
  void publish_state(const std::string &value) {
    state_ = value;
    has_state_ = true;
    notify_changed();
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    if (msg_type != MSG_TEXT_COMMAND_REQUEST) return false;
    std::string value;
    ProtoDecoder dec(p, len);
    ProtoField f;
    while (dec.next(f)) {
      if (f.field == 2 && f.wire == WIRE_LEN) value = f.as_string();
    }
    control(value);
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, icon_);
    b.encode_bool(6, disabled_by_default_);
    b.encode_enum(7, entity_category_);
    b.encode_uint32(8, min_length_);
    b.encode_uint32(9, max_length_);
    b.encode_string(10, pattern_);
    b.encode_enum(11, mode_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_string(2, state_);
    b.encode_bool(3, !has_state_);
  }

 private:
  std::string state_;
  bool has_state_ = false;
  uint32_t min_length_ = 0;
  uint32_t max_length_ = 255;
  uint32_t mode_ = 0;
  std::string pattern_;
};

// --------------------------------------------------------------------------
// Date
// --------------------------------------------------------------------------
class Date : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_DATE_RESPONSE; }
  uint16_t state_type() const override { return MSG_DATE_STATE_RESPONSE; }
  const char *kind() const override { return "date"; }

  std::function<void(uint32_t year, uint32_t month, uint32_t day)> on_control;

  void publish_state(uint32_t year, uint32_t month, uint32_t day) {
    year_ = year;
    month_ = month;
    day_ = day;
    has_state_ = true;
    notify_changed();
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    if (msg_type != MSG_DATE_COMMAND_REQUEST) return false;
    uint32_t year = 0, month = 0, day = 0;
    ProtoDecoder dec(p, len);
    ProtoField f;
    while (dec.next(f)) {
      if (f.field == 2 && f.wire == WIRE_VARINT) year = (uint32_t)f.as_varint;
      if (f.field == 3 && f.wire == WIRE_VARINT) month = (uint32_t)f.as_varint;
      if (f.field == 4 && f.wire == WIRE_VARINT) day = (uint32_t)f.as_varint;
    }
    if (on_control) on_control(year, month, day);
    publish_state(year, month, day);
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, icon_);
    b.encode_bool(6, disabled_by_default_);
    b.encode_enum(7, entity_category_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_bool(2, !has_state_);
    b.encode_uint32(3, year_);
    b.encode_uint32(4, month_);
    b.encode_uint32(5, day_);
  }

 private:
  uint32_t year_ = 2024, month_ = 1, day_ = 1;
  bool has_state_ = false;
};

// --------------------------------------------------------------------------
// Time
// --------------------------------------------------------------------------
class TimeEntity : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_TIME_RESPONSE; }
  uint16_t state_type() const override { return MSG_TIME_STATE_RESPONSE; }
  const char *kind() const override { return "time"; }

  std::function<void(uint32_t hour, uint32_t minute, uint32_t second)> on_control;

  void publish_state(uint32_t hour, uint32_t minute, uint32_t second) {
    hour_ = hour;
    minute_ = minute;
    second_ = second;
    has_state_ = true;
    notify_changed();
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    if (msg_type != MSG_TIME_COMMAND_REQUEST) return false;
    uint32_t hour = 0, minute = 0, second = 0;
    ProtoDecoder dec(p, len);
    ProtoField f;
    while (dec.next(f)) {
      if (f.field == 2 && f.wire == WIRE_VARINT) hour = (uint32_t)f.as_varint;
      if (f.field == 3 && f.wire == WIRE_VARINT) minute = (uint32_t)f.as_varint;
      if (f.field == 4 && f.wire == WIRE_VARINT) second = (uint32_t)f.as_varint;
    }
    if (on_control) on_control(hour, minute, second);
    publish_state(hour, minute, second);
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, icon_);
    b.encode_bool(6, disabled_by_default_);
    b.encode_enum(7, entity_category_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_bool(2, !has_state_);
    b.encode_uint32(3, hour_);
    b.encode_uint32(4, minute_);
    b.encode_uint32(5, second_);
  }

 private:
  uint32_t hour_ = 0, minute_ = 0, second_ = 0;
  bool has_state_ = false;
};

// --------------------------------------------------------------------------
// DateTime
// --------------------------------------------------------------------------
class DateTime : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_DATETIME_RESPONSE; }
  uint16_t state_type() const override { return MSG_DATETIME_STATE_RESPONSE; }
  const char *kind() const override { return "datetime"; }

  std::function<void(uint32_t epoch_seconds)> on_control;

  void publish_state(uint32_t epoch_seconds) {
    epoch_ = epoch_seconds;
    has_state_ = true;
    notify_changed();
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    if (msg_type != MSG_DATETIME_COMMAND_REQUEST) return false;
    uint32_t epoch = 0;
    ProtoDecoder dec(p, len);
    ProtoField f;
    while (dec.next(f)) {
      if (f.field == 2 && f.wire == WIRE_FIXED32) epoch = f.as_fixed32;
    }
    if (on_control) on_control(epoch);
    publish_state(epoch);
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, icon_);
    b.encode_bool(6, disabled_by_default_);
    b.encode_enum(7, entity_category_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_bool(2, !has_state_);
    b.encode_fixed32(3, epoch_, true);
  }

 private:
  uint32_t epoch_ = 0;
  bool has_state_ = false;
};

// --------------------------------------------------------------------------
// Valve
// --------------------------------------------------------------------------
struct ValveCommand {
  bool has_position = false;
  float position = 0;
  bool stop = false;
};

class Valve : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_VALVE_RESPONSE; }
  uint16_t state_type() const override { return MSG_VALVE_STATE_RESPONSE; }
  const char *kind() const override { return "valve"; }

  void set_device_class(const std::string &dc) { device_class_ = dc; }
  void set_supports_position(bool v) { supports_position_ = v; }
  void set_supports_stop(bool v) { supports_stop_ = v; }
  std::function<void(const ValveCommand &)> on_command;

  void publish_state(float position, uint32_t operation = 0) {
    position_ = position;
    operation_ = operation;
    notify_changed();
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    if (msg_type != MSG_VALVE_COMMAND_REQUEST) return false;
    ValveCommand cmd;
    ProtoDecoder dec(p, len);
    ProtoField f;
    while (dec.next(f)) {
      if (f.field == 2 && f.wire == WIRE_VARINT) cmd.has_position = f.as_bool();
      if (f.field == 3 && f.wire == WIRE_FIXED32) cmd.position = f.as_float();
      if (f.field == 4 && f.wire == WIRE_VARINT) cmd.stop = f.as_bool();
    }
    if (on_command) on_command(cmd);
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, icon_);
    b.encode_bool(6, disabled_by_default_);
    b.encode_enum(7, entity_category_);
    b.encode_string(8, device_class_);
    b.encode_bool(10, supports_position_);
    b.encode_bool(11, supports_stop_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_float(2, position_);
    b.encode_enum(3, operation_);
  }

 private:
  float position_ = 0;
  uint32_t operation_ = 0;
  bool supports_position_ = true;
  bool supports_stop_ = true;
  std::string device_class_;
};

// --------------------------------------------------------------------------
// Update
// --------------------------------------------------------------------------
class Update : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_UPDATE_RESPONSE; }
  uint16_t state_type() const override { return MSG_UPDATE_STATE_RESPONSE; }
  const char *kind() const override { return "update"; }

  void set_device_class(const std::string &dc) { device_class_ = dc; }
  std::function<void(uint32_t command)> on_command;  // UpdateCommand enum

  void publish_state(const std::string &current, const std::string &latest,
                     bool in_progress = false, float progress = 0) {
    current_version_ = current;
    latest_version_ = latest;
    in_progress_ = in_progress;
    progress_ = progress;
    has_state_ = true;
    notify_changed();
  }

  bool handle_command(uint16_t msg_type, const uint8_t *p, size_t len) override {
    if (msg_type != MSG_UPDATE_COMMAND_REQUEST) return false;
    uint32_t command = 0;
    ProtoDecoder dec(p, len);
    ProtoField f;
    while (dec.next(f)) {
      if (f.field == 2 && f.wire == WIRE_VARINT) command = (uint32_t)f.as_varint;
    }
    if (on_command) on_command(command);
    return true;
  }

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, icon_);
    b.encode_bool(6, disabled_by_default_);
    b.encode_enum(7, entity_category_);
    b.encode_string(8, device_class_);
  }
  void encode_state(ProtoWriteBuffer &b) const override {
    b.encode_fixed32(1, key_, true);
    b.encode_bool(2, !has_state_);
    b.encode_bool(3, in_progress_);
    b.encode_bool(4, has_progress_);
    b.encode_float(5, progress_);
    b.encode_string(6, current_version_);
    b.encode_string(7, latest_version_);
    b.encode_string(8, title_);
  }

 private:
  bool has_state_ = false;
  bool in_progress_ = false;
  bool has_progress_ = false;
  float progress_ = 0;
  std::string current_version_;
  std::string latest_version_;
  std::string title_;
  std::string device_class_;
};

// --------------------------------------------------------------------------
// Event (fires EventResponse, no persistent state)
// --------------------------------------------------------------------------
class Event : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_EVENT_RESPONSE; }
  uint16_t state_type() const override { return 0; }
  const char *kind() const override { return "event"; }

  void set_device_class(const std::string &dc) { device_class_ = dc; }
  void set_event_types(const std::vector<std::string> &types) {
    event_types_ = types;
  }

  void publish_event(const std::string &event_type);

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_string(5, icon_);
    b.encode_bool(6, disabled_by_default_);
    b.encode_enum(7, entity_category_);
    b.encode_string(8, device_class_);
    b.encode_repeated_string(9, event_types_);
  }
  void encode_state(ProtoWriteBuffer &) const override {}

 private:
  std::string device_class_;
  std::vector<std::string> event_types_;
};

// --------------------------------------------------------------------------
// Camera (list only; images sent via send_image())
// --------------------------------------------------------------------------
class Camera : public Entity {
 public:
  uint16_t list_type() const override { return MSG_LIST_ENTITIES_CAMERA_RESPONSE; }
  uint16_t state_type() const override { return 0; }
  const char *kind() const override { return "camera"; }

  std::function<void(bool single, bool stream)> on_image_request;

  void handle_image_request(bool single, bool stream);
  void send_image(const uint8_t *data, size_t len, bool done);

  void encode_list(ProtoWriteBuffer &b) const override {
    encode_common(b);
    b.encode_bool(5, disabled_by_default_);
    b.encode_string(6, icon_);
    b.encode_enum(7, entity_category_);
  }
  void encode_state(ProtoWriteBuffer &) const override {}
};
