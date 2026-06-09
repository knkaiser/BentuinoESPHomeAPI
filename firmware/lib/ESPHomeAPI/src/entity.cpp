#include "entity.h"

#include "api_server.h"

namespace esphome_api {

void Entity::notify_changed() {
  if (server_ != nullptr) server_->push_state(this);
}

void Event::publish_event(const std::string &event_type) {
  if (server_ != nullptr) server_->push_event(this, event_type);
}

void Camera::handle_image_request(bool single, bool stream) {
  if (on_image_request) on_image_request(single, stream);
}

void Camera::send_image(const uint8_t *data, size_t len, bool done) {
  if (server_ != nullptr) server_->push_camera_image(this, data, len, done);
}

}  // namespace esphome_api
