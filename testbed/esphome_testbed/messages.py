"""ESPHome native API message-type IDs and entity-type maps."""
from __future__ import annotations

# Core / session
HELLO_REQUEST = 1
HELLO_RESPONSE = 2
CONNECT_REQUEST = 3
CONNECT_RESPONSE = 4
DISCONNECT_REQUEST = 5
DISCONNECT_RESPONSE = 6
PING_REQUEST = 7
PING_RESPONSE = 8
DEVICE_INFO_REQUEST = 9
DEVICE_INFO_RESPONSE = 10
LIST_ENTITIES_REQUEST = 11
LIST_ENTITIES_DONE_RESPONSE = 19
SUBSCRIBE_STATES_REQUEST = 20
SUBSCRIBE_LOGS_REQUEST = 28
SUBSCRIBE_LOGS_RESPONSE = 29
SUBSCRIBE_HOMEASSISTANT_SERVICES_REQUEST = 34
HOMEASSISTANT_SERVICE_RESPONSE = 35
GET_TIME_REQUEST = 36
GET_TIME_RESPONSE = 37
SUBSCRIBE_HOME_ASSISTANT_STATES_REQUEST = 38
SUBSCRIBE_HOME_ASSISTANT_STATE_RESPONSE = 39
HOME_ASSISTANT_STATE_RESPONSE = 40

# Commands
SWITCH_COMMAND_REQUEST = 33
BUTTON_COMMAND_REQUEST = 62

# ListEntities* response IDs -> human label
LIST_ENTITY_TYPES = {
    12: "binary_sensor",
    13: "cover",
    14: "fan",
    15: "light",
    16: "sensor",
    17: "switch",
    18: "text_sensor",
    43: "camera",
    46: "climate",
    49: "number",
    52: "select",
    58: "lock",
    61: "button",
    63: "media_player",
    94: "alarm_control_panel",
    97: "text",
    100: "date",
    103: "time",
    107: "event",
    109: "valve",
    112: "datetime",
    116: "update",
    41: "service",
}

# *StateResponse IDs -> human label
STATE_TYPES = {
    21: "binary_sensor",
    22: "cover",
    23: "fan",
    24: "light",
    25: "sensor",
    26: "switch",
    27: "text_sensor",
    47: "climate",
    50: "number",
    53: "select",
    59: "lock",
    64: "media_player",
    95: "alarm_control_panel",
    98: "text",
    101: "date",
    104: "time",
    108: "event",
    110: "valve",
    113: "datetime",
    117: "update",
}

API_VERSION_MAJOR = 1
API_VERSION_MINOR = 10
