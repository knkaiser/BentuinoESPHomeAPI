# SPDX-FileCopyrightText: 2026 Karl Kaiser
# SPDX-License-Identifier: AGPL-3.0-or-later
# Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

"""Test registry, execution context, and results reporting."""
from __future__ import annotations

import base64
import json
import socket
import time
from dataclasses import dataclass, field
from typing import Callable, List, Optional

from .client import APIClient
from .transport import (
    HandshakeError,
    NoiseTransport,
    PlaintextTransport,
    ProtocolError,
    connect,
)

# Test "mode" controls when a test is eligible to run.
MODE_OFFLINE = "offline"        # no device required
MODE_ONLINE = "online"          # needs a reachable device
MODE_DISRUPTIVE = "disruptive"  # needs device + --allow-disruptive (may reboot it)


class SkipTest(Exception):
    """Raise inside a test to report SKIP with a reason."""


@dataclass
class TestCase:
    id: str
    level: str          # MUST / SHOULD / MAY
    mode: str
    desc: str
    func: Callable


REGISTRY: List[TestCase] = []


def test(id: str, level: str, mode: str, desc: str):
    def deco(fn: Callable) -> Callable:
        REGISTRY.append(TestCase(id, level, mode, desc, fn))
        return fn

    return deco


@dataclass
class Config:
    host: Optional[str] = None
    port: int = 6053
    encryption_key: Optional[str] = None  # base64
    password: str = ""
    timeout: float = 10.0
    allow_disruptive: bool = False
    expected_entities: dict = field(default_factory=dict)  # object_id -> type_name
    mdns_name: Optional[str] = None  # ESPHome node name for TS-15.4/15.5
    expected_manufacturer: Optional[str] = None  # optional check for TS-15.3

    @property
    def psk(self) -> Optional[bytes]:
        if not self.encryption_key:
            return None
        return base64.b64decode(self.encryption_key)


class Context:
    """Provides connections to tests."""

    def __init__(self, cfg: Config):
        self.cfg = cfg

    @property
    def device_available(self) -> bool:
        return bool(self.cfg.host)

    def connect_transport(self, do_handshake: bool = True, psk: Optional[bytes] = None):
        if not self.cfg.host:
            raise SkipTest("no device configured")
        sock = connect(self.cfg.host, self.cfg.port, self.cfg.timeout)
        use_psk = psk if psk is not None else self.cfg.psk
        if use_psk is not None:
            t = NoiseTransport(sock, use_psk)
            if do_handshake:
                t.do_handshake()
        else:
            t = PlaintextTransport(sock)
            if do_handshake:
                t.do_handshake()
        return t

    def new_client(self, authenticate: bool = True) -> APIClient:
        t = self.connect_transport(do_handshake=True)
        c = APIClient(t)
        c.hello()
        if authenticate:
            invalid = c.authenticate(self.cfg.password)
            if invalid:
                c.close()
                raise ProtocolError("authentication rejected (check password)")
        return c


@dataclass
class Result:
    id: str
    level: str
    mode: str
    status: str  # PASS / FAIL / SKIP
    note: str


def run(cfg: Config, only_prefix: Optional[str] = None,
        offline_only: bool = False) -> List[Result]:
    ctx = Context(cfg)
    results: List[Result] = []
    for tc in REGISTRY:
        if only_prefix and not tc.id.startswith(only_prefix):
            continue

        if tc.mode in (MODE_ONLINE, MODE_DISRUPTIVE):
            if offline_only or not ctx.device_available:
                results.append(Result(tc.id, tc.level, tc.mode, "SKIP",
                                      "device not available / offline-only"))
                continue
        if tc.mode == MODE_DISRUPTIVE and not cfg.allow_disruptive:
            results.append(Result(tc.id, tc.level, tc.mode, "SKIP",
                                  "disruptive (use --allow-disruptive)"))
            continue

        try:
            out = tc.func(ctx)
            note = out if isinstance(out, str) else ""
            results.append(Result(tc.id, tc.level, tc.mode, "PASS", note))
        except SkipTest as exc:
            results.append(Result(tc.id, tc.level, tc.mode, "SKIP", str(exc)))
        except AssertionError as exc:
            results.append(Result(tc.id, tc.level, tc.mode, "FAIL", str(exc) or "assertion failed"))
        except Exception as exc:  # noqa: BLE001
            results.append(Result(tc.id, tc.level, tc.mode, "FAIL",
                                  f"{type(exc).__name__}: {exc}"))
    return results


# ---------------------------------------------------------------- reporting

_COLOR = {"PASS": "\033[92m", "FAIL": "\033[91m", "SKIP": "\033[93m"}
_RESET = "\033[0m"


def print_report(results: List[Result], color: bool = True) -> None:
    width = max((len(r.id) for r in results), default=6)
    print()
    print(f"{'TEST':<{width}}  {'LEVEL':<6}  {'STATUS':<6}  NOTE")
    print("-" * (width + 60))
    for r in results:
        status = r.status
        if color and status in _COLOR:
            status_disp = f"{_COLOR[status]}{status:<6}{_RESET}"
        else:
            status_disp = f"{status:<6}"
        print(f"{r.id:<{width}}  {r.level:<6}  {status_disp}  {r.note}")
    print()
    summary(results)


def summary(results: List[Result]) -> None:
    def count(level, status):
        return sum(1 for r in results if r.level == level and r.status == status)

    must_pass = count("MUST", "PASS")
    must_fail = count("MUST", "FAIL")
    must_total = sum(1 for r in results if r.level == "MUST" and r.status != "SKIP")
    should_pass = count("SHOULD", "PASS")
    should_total = sum(1 for r in results if r.level == "SHOULD" and r.status != "SKIP")
    skipped = sum(1 for r in results if r.status == "SKIP")

    print(f"MUST   passed: {must_pass}/{must_total}   (failures: {must_fail})")
    print(f"SHOULD passed: {should_pass}/{should_total}")
    print(f"Skipped: {skipped}")
    if must_fail == 0 and must_total > 0:
        verdict = "CONFORMANT" if should_pass == should_total else "CONFORMANT-WITH-DEVIATIONS"
    elif must_total == 0:
        verdict = "INCONCLUSIVE (no MUST tests executed)"
    else:
        verdict = "NON-CONFORMANT"
    print(f"Overall: {verdict}")


def write_json(results: List[Result], path: str) -> None:
    data = {
        "generated": time.strftime("%Y-%m-%dT%H:%M:%S"),
        "results": [r.__dict__ for r in results],
    }
    with open(path, "w") as fh:
        json.dump(data, fh, indent=2)


def write_markdown(results: List[Result], path: str) -> None:
    lines = [
        "# Compliance Test Run Results",
        "",
        f"Generated: {time.strftime('%Y-%m-%d %H:%M:%S')}",
        "",
        "| Test ID | Level | Mode | Status | Note |",
        "|---|---|---|---|---|",
    ]
    for r in results:
        note = r.note.replace("|", "\\|")
        lines.append(f"| {r.id} | {r.level} | {r.mode} | {r.status} | {note} |")
    with open(path, "w") as fh:
        fh.write("\n".join(lines) + "\n")
