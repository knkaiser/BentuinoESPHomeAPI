# SPDX-FileCopyrightText: 2026 Karl Kaiser
# SPDX-License-Identifier: AGPL-3.0-or-later
# Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

"""High-level ESPHome native API client (CLIENT role).

Wraps a transport and implements the session lifecycle plus the message
builders/parsers needed by the compliance test bed.
"""
from __future__ import annotations

import socket
import time
from dataclasses import dataclass, field
from typing import Dict, List, Optional, Tuple

from . import messages as M
from . import proto
from .transport import ProtocolError


@dataclass
class HelloResult:
    api_version_major: int
    api_version_minor: int
    server_info: str
    name: str


@dataclass
class DeviceInfo:
    name: str
    mac_address: str
    esphome_version: str
    model: str
    manufacturer: str
    uses_password: bool
    friendly_name: str
    webserver_port: Optional[int]
    raw: proto.DecodedFields


@dataclass
class Entity:
    type_id: int
    type_name: str
    object_id: str
    key: int
    name: str
    unique_id: str


@dataclass
class State:
    type_id: int
    type_name: str
    key: int
    value: object
    missing_state: bool = False


class APIClient:
    def __init__(self, transport):
        self.t = transport
        self.closed = False

    # ------------------------------------------------------------------ recv
    def recv(self, auto: bool = True) -> Tuple[int, bytes]:
        """Receive one message. When auto=True, transparently answers
        PingRequest and GetTimeRequest and keeps reading."""
        while True:
            msg_type, payload = self.t.recv_message()
            if auto and msg_type == M.PING_REQUEST:
                self.t.send_message(M.PING_RESPONSE, b"")
                continue
            if auto and msg_type == M.GET_TIME_REQUEST:
                resp = proto.enc_fixed32(1, int(time.time()), force=True)
                self.t.send_message(M.GET_TIME_RESPONSE, resp)
                continue
            if auto and msg_type == M.DISCONNECT_REQUEST:
                self.t.send_message(M.DISCONNECT_RESPONSE, b"")
                self.closed = True
            return msg_type, payload

    def recv_expect(self, expected_type: int, auto: bool = True) -> proto.DecodedFields:
        msg_type, payload = self.recv(auto=auto)
        if msg_type != expected_type:
            raise ProtocolError(
                f"expected message type {expected_type}, got {msg_type}"
            )
        return proto.decode_message(payload)

    # ------------------------------------------------------------- lifecycle
    def hello(
        self,
        client_info: str = "Bentuino-Testbed",
        major: int = M.API_VERSION_MAJOR,
        minor: int = M.API_VERSION_MINOR,
    ) -> HelloResult:
        payload = (
            proto.enc_string(1, client_info)
            + proto.enc_uint32(2, major)
            + proto.enc_uint32(3, minor)
        )
        self.t.send_message(M.HELLO_REQUEST, payload)
        f = self.recv_expect(M.HELLO_RESPONSE)
        return HelloResult(
            api_version_major=proto.get_uint32(f, 1),
            api_version_minor=proto.get_uint32(f, 2),
            server_info=proto.get_string(f, 3),
            name=proto.get_string(f, 4),
        )

    def authenticate(self, password: str = "") -> bool:
        """Send ConnectRequest. Returns True if password was INVALID."""
        payload = proto.enc_string(1, password) if password else b""
        self.t.send_message(M.CONNECT_REQUEST, payload)
        f = self.recv_expect(M.CONNECT_RESPONSE)
        return proto.get_bool(f, 1)

    def device_info(self) -> DeviceInfo:
        self.t.send_message(M.DEVICE_INFO_REQUEST, b"")
        f = self.recv_expect(M.DEVICE_INFO_RESPONSE)
        return DeviceInfo(
            uses_password=proto.get_bool(f, 1),
            name=proto.get_string(f, 2),
            mac_address=proto.get_string(f, 3),
            esphome_version=proto.get_string(f, 4),
            model=proto.get_string(f, 6),
            manufacturer=proto.get_string(f, 12),
            friendly_name=proto.get_string(f, 13),
            webserver_port=proto.get_uint32(f, 10) or None,
            raw=f,
        )

    def ping(self) -> None:
        self.t.send_message(M.PING_REQUEST, b"")
        self.recv_expect(M.PING_RESPONSE)

    # -------------------------------------------------------------- entities
    def list_entities(self, timeout_messages: int = 500) -> List[Entity]:
        self.t.send_message(M.LIST_ENTITIES_REQUEST, b"")
        entities: List[Entity] = []
        for _ in range(timeout_messages):
            msg_type, payload = self.recv()
            if msg_type == M.LIST_ENTITIES_DONE_RESPONSE:
                return entities
            if msg_type in M.LIST_ENTITY_TYPES:
                f = proto.decode_message(payload)
                entities.append(
                    Entity(
                        type_id=msg_type,
                        type_name=M.LIST_ENTITY_TYPES[msg_type],
                        object_id=proto.get_string(f, 1),
                        key=proto.get_fixed32(f, 2),
                        name=proto.get_string(f, 3),
                        unique_id=proto.get_string(f, 4),
                    )
                )
            # ignore other message types during discovery
        raise ProtocolError("ListEntitiesDoneResponse not received")

    def subscribe_states(self) -> None:
        self.t.send_message(M.SUBSCRIBE_STATES_REQUEST, b"")

    def subscribe_logs(self, level: int = 3, dump_config: bool = False) -> None:
        payload = proto.enc_uint32(1, level, force=True)
        if dump_config:
            payload += proto.enc_bool(2, True)
        self.t.send_message(M.SUBSCRIBE_LOGS_REQUEST, payload)

    def recv_log(self) -> tuple:
        """Return (level, message, send_failed) from SubscribeLogsResponse."""
        while True:
            msg_type, payload = self.recv()
            if msg_type != M.SUBSCRIBE_LOGS_RESPONSE:
                continue
            f = proto.decode_message(payload)
            return (
                proto.get_uint32(f, 1),
                proto.get_string(f, 3),
                proto.get_bool(f, 4),
            )

    # ----------------------------------------------------- Home Assistant
    def subscribe_ha_services(self) -> None:
        """Tell the device it may send HomeassistantServiceResponse (35)."""
        self.t.send_message(M.SUBSCRIBE_HOMEASSISTANT_SERVICES_REQUEST, b"")

    def recv_ha_service(self) -> dict:
        """Return the next HomeassistantServiceResponse (35) as a dict."""
        while True:
            msg_type, payload = self.recv()
            if msg_type != M.HOMEASSISTANT_SERVICE_RESPONSE:
                continue
            f = proto.decode_message(payload)

            def maps(field: int):
                out = []
                for wire, raw in f.get(field, []):
                    mf = proto.decode_message(bytes(raw))
                    out.append(
                        (proto.get_string(mf, 1), proto.get_string(mf, 2))
                    )
                return out

            return {
                "service": proto.get_string(f, 1),
                "data": maps(2),
                "data_template": maps(3),
                "variables": maps(4),
                "is_event": proto.get_bool(f, 5),
            }

    def subscribe_ha_states(self, settle: float = 1.0) -> List[dict]:
        """Send SubscribeHomeAssistantStatesRequest (38) and collect the
        device's SubscribeHomeAssistantStateResponse (39) entries. Reads until
        the stream goes quiet for `settle` seconds."""
        self.t.send_message(M.SUBSCRIBE_HOME_ASSISTANT_STATES_REQUEST, b"")
        subs: List[dict] = []
        deadline = time.time() + settle
        self.t.raw_socket.settimeout(settle)
        while time.time() < deadline:
            try:
                msg_type, payload = self.recv()
            except (socket.timeout, OSError):
                break
            if msg_type != M.SUBSCRIBE_HOME_ASSISTANT_STATE_RESPONSE:
                continue
            f = proto.decode_message(payload)
            subs.append(
                {
                    "entity_id": proto.get_string(f, 1),
                    "attribute": proto.get_string(f, 2),
                    "once": proto.get_bool(f, 3),
                }
            )
        return subs

    def send_ha_state(self, entity_id: str, state: str, attribute: str = "") -> None:
        """Push a HomeAssistantStateResponse (40) to the device."""
        payload = proto.enc_string(1, entity_id) + proto.enc_string(2, state)
        if attribute:
            payload += proto.enc_string(3, attribute)
        self.t.send_message(M.HOME_ASSISTANT_STATE_RESPONSE, payload)

    def read_state(self) -> State:
        msg_type, payload = self.recv()
        if msg_type not in M.STATE_TYPES:
            raise ProtocolError(f"expected a *StateResponse, got {msg_type}")
        f = proto.decode_message(payload)
        key = proto.get_fixed32(f, 1)
        if msg_type == 25:  # sensor -> float
            value: object = proto.get_float(f, 2)
        elif msg_type in (21, 26):  # binary_sensor / switch -> bool
            value = proto.get_bool(f, 2)
        else:
            value = proto.get_uint32(f, 2)
        return State(
            type_id=msg_type,
            type_name=M.STATE_TYPES[msg_type],
            key=key,
            value=value,
            missing_state=proto.get_bool(f, 3),
        )

    # -------------------------------------------------------------- commands
    def switch_command(self, key: int, state: bool) -> None:
        payload = proto.enc_fixed32(1, key, force=True) + proto.enc_bool(2, state)
        self.t.send_message(M.SWITCH_COMMAND_REQUEST, payload)

    def button_command(self, key: int) -> None:
        payload = proto.enc_fixed32(1, key, force=True)
        self.t.send_message(M.BUTTON_COMMAND_REQUEST, payload)

    # ------------------------------------------------------------------ misc
    def send_raw(self, msg_type: int, payload: bytes = b"") -> None:
        self.t.send_message(msg_type, payload)

    def disconnect(self) -> None:
        try:
            self.t.send_message(M.DISCONNECT_REQUEST, b"")
            # best-effort wait for DisconnectResponse
            self.t.raw_socket.settimeout(2.0)
            self.t.recv_message()
        except Exception:  # noqa: BLE001
            pass
        finally:
            self.close()

    def close(self) -> None:
        self.closed = True
        self.t.close()
