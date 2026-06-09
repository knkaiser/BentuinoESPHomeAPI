# ESPHomeAPI examples

One minimal PlatformIO project per entity type (22 total). Each example
registers a **single entity**, demonstrates its API surface, and runs on ESP32
with no extra hardware beyond WiFi (GPIO examples use safe defaults you can
override via `build_flags`).

Shared WiFi boilerplate lives in [`_common/wifi_setup.h`](_common/wifi_setup.h).

**Home Assistant auto-discovery:** the library does not register mDNS itself.
Every example calls [`_common/api_mdns.h`](_common/api_mdns.h) after
`api.begin()` to advertise `_esphomelib._tcp` on port 6053 with the TXT records
Home Assistant expects (`version`, `address`, `mac`, `friendly_name`, and
`api_encryption` when Noise is enabled). Copy that header into your own
project or include it via `-I../_common` in `platformio.ini`.

## Entity examples

| Folder | Entity | What it demonstrates |
|---|---|---|
| [`sensor`](sensor/) | `Sensor` | `publish_state(float)`, unit/device_class |
| [`binary_sensor`](binary_sensor/) | `BinarySensor` | toggling bool state |
| [`text_sensor`](text_sensor/) | `TextSensor` | `publish_state(string)` |
| [`switch`](switch/) | `Switch` | `on_control`, `restore_mode` |
| [`button`](button/) | `Button` | `on_press` (stateless) |
| [`cover`](cover/) | `Cover` | position + stop via `CoverCommand` |
| [`fan`](fan/) | `Fan` | speed level + preset via `FanCommand` |
| [`light`](light/) | `Light` | brightness PWM via `LightCommand` |
| [`number`](number/) | `Number` | min/max/step slider |
| [`select`](select/) | `Select` | fixed option list |
| [`lock`](lock/) | `Lock` | lock/unlock commands |
| [`climate`](climate/) | `Climate` | mode + target temperature |
| [`media_player`](media_player/) | `MediaPlayer` | play/pause + volume |
| [`alarm_control_panel`](alarm_control_panel/) | `AlarmControlPanel` | arm/disarm |
| [`text`](text/) | `Text` | editable string from HA |
| [`date`](date/) | `Date` | year/month/day picker |
| [`time`](time/) | `TimeEntity` | hour/minute/second picker |
| [`datetime`](datetime/) | `DateTime` | Unix epoch picker |
| [`valve`](valve/) | `Valve` | position + stop |
| [`update`](update/) | `Update` | install simulation |
| [`event`](event/) | `Event` | `publish_event()` (no state) |
| [`camera`](camera/) | `Camera` | `send_image()` on snapshot request |

## Build & flash

Pick any example folder:

```bash
cd sensor   # or light, switch, fan, …

# WiFi: edit src/main.cpp, or pass build flags:
#   pio run -e esp32dev --build-flag "-DWIFI_SSID='\"my-ssid\"'" \
#                       --build-flag "-DWIFI_PASSWORD='\"my-pass\"'"

pio run -e esp32dev -t upload
pio device monitor
```

Then add the device in Home Assistant: **Settings → Devices & Services → Add
Integration → ESPHome**. With mDNS enabled (default in these examples), the
node should appear automatically as `<cfg.name>.local`. You can also enter the
device IP and port `6053`. When `cfg.encryption_key` is set, HA prompts for the
same base64 Noise PSK; the `api_encryption` TXT record is added automatically.

## GPIO overrides

Some examples drive a pin for visual feedback:

| Example | Default pin | Build flag |
|---|---|---|
| `light` | GPIO 2 (onboard LED) | `-DLIGHT_PIN=16` |
| `switch` | GPIO 5 (relay/output) | `-DOUTPUT_PIN=17` |
