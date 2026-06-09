// SPDX-FileCopyrightText: 2026 Karl Kaiser
// SPDX-License-Identifier: AGPL-3.0-or-later
// Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

#pragma once
// Log levels matching api.proto LogLevel.
#include <cstdint>

namespace esphome_api {

enum LogLevel : uint32_t {
  LOG_LEVEL_NONE = 0,
  LOG_LEVEL_ERROR = 1,
  LOG_LEVEL_WARN = 2,
  LOG_LEVEL_INFO = 3,
  LOG_LEVEL_CONFIG = 4,
  LOG_LEVEL_DEBUG = 5,
  LOG_LEVEL_VERBOSE = 6,
  LOG_LEVEL_VERY_VERBOSE = 7,
};

}  // namespace esphome_api
