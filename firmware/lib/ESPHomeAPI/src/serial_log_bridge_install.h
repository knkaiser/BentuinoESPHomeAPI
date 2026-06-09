#pragma once
// Optional compile-time hook: redirect Arduino Serial to ApiSerial so every
// Serial.print/println/printf also streams to subscribed HA log clients.
// Use build_src_flags (not build_flags) so library .cpp/.c files keep the
// real hardware UART:
//   build_src_flags =
//     -DESPHOME_API_SERIAL_BRIDGE
//     -Ilib/ESPHomeAPI/src
//     -include serial_log_bridge_install.h
#ifdef __cplusplus
#include "serial_log_bridge.h"

#undef Serial
#define Serial (::esphome_api::ApiSerial)
#endif
