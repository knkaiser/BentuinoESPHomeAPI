"""Wire transports for the ESPHome native API.

- PlaintextTransport : indicator 0x00 framing
- NoiseTransport     : indicator 0x01, Noise_NNpsk0_25519_ChaChaPoly_SHA256

Both expose the same interface:
    send_message(msg_type: int, payload: bytes)
    recv_message() -> (msg_type: int, payload: bytes)
    raw_socket    -> underlying socket (for negative tests)
    close()
"""
from __future__ import annotations

import socket
from typing import Optional, Tuple

from .proto import encode_varint


class ProtocolError(Exception):
    pass


class HandshakeError(Exception):
    pass


class _SocketReader:
    """Buffered exact-byte reader over a blocking socket."""

    def __init__(self, sock: socket.socket):
        self._sock = sock

    def recv_exact(self, n: int) -> bytes:
        chunks = bytearray()
        while len(chunks) < n:
            try:
                part = self._sock.recv(n - len(chunks))
            except socket.timeout as exc:  # noqa: PERF203
                raise TimeoutError("socket read timed out") from exc
            if not part:
                raise ProtocolError("connection closed by peer")
            chunks.extend(part)
        return bytes(chunks)

    def read_varint(self) -> int:
        result = 0
        shift = 0
        while True:
            b = self.recv_exact(1)[0]
            result |= (b & 0x7F) << shift
            if not (b & 0x80):
                return result
            shift += 7
            if shift > 63:
                raise ProtocolError("varint too long")


# ---------------------------------------------------------------------------
# Plaintext
# ---------------------------------------------------------------------------


class PlaintextTransport:
    indicator = 0x00

    def __init__(self, sock: socket.socket):
        self.raw_socket = sock
        self._reader = _SocketReader(sock)

    def do_handshake(self) -> None:  # no-op for plaintext
        return

    def send_message(self, msg_type: int, payload: bytes = b"") -> None:
        frame = (
            bytes([self.indicator])
            + encode_varint(len(payload))
            + encode_varint(msg_type)
            + payload
        )
        self.raw_socket.sendall(frame)

    def recv_message(self) -> Tuple[int, bytes]:
        ind = self._reader.recv_exact(1)[0]
        if ind != self.indicator:
            raise ProtocolError(f"bad plaintext indicator 0x{ind:02X}")
        length = self._reader.read_varint()
        msg_type = self._reader.read_varint()
        payload = self._reader.recv_exact(length) if length else b""
        return msg_type, payload

    def close(self) -> None:
        try:
            self.raw_socket.close()
        except OSError:
            pass


# ---------------------------------------------------------------------------
# Noise
# ---------------------------------------------------------------------------

NOISE_PROTOCOL = b"Noise_NNpsk0_25519_ChaChaPoly_SHA256"
# Prologue for an EMPTY client hello: "NoiseAPIInit" + uint16_be(0)
PROLOGUE = b"NoiseAPIInit\x00\x00"


class NoiseTransport:
    indicator = 0x01

    def __init__(self, sock: socket.socket, psk: bytes):
        if len(psk) != 32:
            raise ValueError(f"PSK must be 32 bytes, got {len(psk)}")
        self.raw_socket = sock
        self.psk = psk
        self._reader = _SocketReader(sock)
        self._noise = None
        self.server_name: str = ""
        self.server_mac: str = ""

    # -- raw frame helpers (0x01 + uint16_be len + body) --

    def _write_frame(self, body: bytes) -> None:
        header = bytes([self.indicator, (len(body) >> 8) & 0xFF, len(body) & 0xFF])
        self.raw_socket.sendall(header + body)

    def _read_frame(self) -> bytes:
        ind = self._reader.recv_exact(1)[0]
        if ind != self.indicator:
            raise HandshakeError(f"bad noise indicator 0x{ind:02X}")
        hi, lo = self._reader.recv_exact(2)
        size = (hi << 8) | lo
        return self._reader.recv_exact(size) if size else b""

    def do_handshake(self) -> None:
        try:
            from noise.connection import NoiseConnection  # lazy import
        except ImportError as exc:  # pragma: no cover
            raise HandshakeError(
                "noiseprotocol not installed (pip install noiseprotocol)"
            ) from exc

        # 1. Send empty client hello
        self._write_frame(b"")

        # 2. Read server hello: [0x01 proto][name\0][mac\0]
        body = self._read_frame()
        if not body:
            raise HandshakeError("empty server hello")
        if body[0] != 0x01:
            raise HandshakeError(f"unexpected server proto byte 0x{body[0]:02X}")
        parts = body[1:].split(b"\x00")
        if len(parts) >= 1:
            self.server_name = parts[0].decode("utf-8", "replace")
        if len(parts) >= 2:
            self.server_mac = parts[1].decode("utf-8", "replace")

        # 3. Noise NNpsk0 handshake (initiator)
        noise = NoiseConnection.from_name(NOISE_PROTOCOL)
        noise.set_as_initiator()
        noise.set_psks(self.psk)
        noise.set_prologue(PROLOGUE)
        noise.start_handshake()

        msg1 = noise.write_message()  # -> psk, e
        self._write_frame(b"\x00" + msg1)

        resp = self._read_frame()  # <- e, ee
        if not resp:
            raise HandshakeError("empty handshake response")
        if resp[0] != 0x00:
            reason = resp[1:].decode("utf-8", "replace")
            raise HandshakeError(f"handshake rejected: {reason}")
        noise.read_message(resp[1:])

        if not noise.handshake_finished:
            raise HandshakeError("handshake did not complete")
        self._noise = noise

    # -- encrypted data frames --

    def send_message(self, msg_type: int, payload: bytes = b"") -> None:
        if self._noise is None:
            raise ProtocolError("noise handshake not complete")
        inner = bytes(
            [
                (msg_type >> 8) & 0xFF,
                msg_type & 0xFF,
                (len(payload) >> 8) & 0xFF,
                len(payload) & 0xFF,
            ]
        ) + payload
        ciphertext = self._noise.encrypt(inner)
        self._write_frame(ciphertext)

    def recv_message(self) -> Tuple[int, bytes]:
        if self._noise is None:
            raise ProtocolError("noise handshake not complete")
        ciphertext = self._read_frame()
        inner = self._noise.decrypt(ciphertext)
        if len(inner) < 4:
            raise ProtocolError("noise data frame too short")
        msg_type = (inner[0] << 8) | inner[1]
        data_len = (inner[2] << 8) | inner[3]
        payload = inner[4 : 4 + data_len]
        return msg_type, payload

    def close(self) -> None:
        try:
            self.raw_socket.close()
        except OSError:
            pass


def connect(host: str, port: int, timeout: float = 10.0) -> socket.socket:
    sock = socket.create_connection((host, port), timeout=timeout)
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    sock.settimeout(timeout)
    return sock
