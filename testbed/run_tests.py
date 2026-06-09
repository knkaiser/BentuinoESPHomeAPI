#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Karl Kaiser
# SPDX-License-Identifier: AGPL-3.0-or-later
# Part of BentuinoESPHomeAPI — https://github.com/knkaiser/BentuinoESPHomeAPI

"""ESPHome native API compliance test bed - CLI entry point.

Examples
--------
Offline (no device) unit tests only:
    python run_tests.py --offline-only

Against an encrypted device:
    python run_tests.py --host 192.168.1.50 \
        --encryption-key "BASE64KEY==" --out results

Against a plaintext+password device:
    python run_tests.py --host 192.168.1.50 --password mypass

Run a single suite:
    python run_tests.py --only TS-9 --offline-only
"""
from __future__ import annotations

import argparse
import json
import os
import sys

# Make the package importable when run directly.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from esphome_testbed import runner
from esphome_testbed import tests  # noqa: F401  (registers tests via decorators)


def load_config(args) -> runner.Config:
    cfg = runner.Config()
    if args.config:
        with open(args.config) as fh:
            data = json.load(fh)
        cfg.host = data.get("host")
        cfg.port = int(data.get("port", 6053))
        cfg.encryption_key = data.get("encryption_key")
        cfg.password = data.get("password", "")
        cfg.timeout = float(data.get("timeout", 10.0))
        cfg.expected_entities = data.get("expected_entities", {})
        cfg.mdns_name = data.get("mdns_name")
        cfg.expected_manufacturer = data.get("expected_manufacturer")
    # CLI overrides
    if args.host:
        cfg.host = args.host
    if args.port:
        cfg.port = args.port
    if args.encryption_key:
        cfg.encryption_key = args.encryption_key
    if args.password is not None:
        cfg.password = args.password
    if args.timeout:
        cfg.timeout = args.timeout
    if args.mdns_name:
        cfg.mdns_name = args.mdns_name
    if args.expected_manufacturer:
        cfg.expected_manufacturer = args.expected_manufacturer
    cfg.allow_disruptive = args.allow_disruptive
    return cfg


def main() -> int:
    p = argparse.ArgumentParser(description="ESPHome native API compliance test bed")
    p.add_argument("--host", help="device IP/hostname")
    p.add_argument("--port", type=int, default=0, help="device port (default 6053)")
    p.add_argument("--encryption-key", help="base64 Noise PSK (api.encryption.key)")
    p.add_argument("--password", default=None, help="API password (api.password)")
    p.add_argument("--config", help="path to JSON config file")
    p.add_argument("--timeout", type=float, default=0.0, help="socket timeout seconds")
    p.add_argument("--only", help="only run tests whose ID starts with this prefix (e.g. TS-9)")
    p.add_argument("--offline-only", action="store_true", help="run only offline tests")
    p.add_argument("--allow-disruptive", action="store_true",
                   help="permit tests that may reboot the device")
    p.add_argument("--mdns-name", help="ESPHome node name for mDNS tests (TS-15.4/15.5)")
    p.add_argument("--expected-manufacturer",
                   help="expected DeviceInfo.manufacturer for TS-15.3")
    p.add_argument("--out", help="write results to <out>.json and <out>.md")
    p.add_argument("--no-color", action="store_true")
    args = p.parse_args()

    cfg = load_config(args)

    if not args.offline_only and not cfg.host:
        print("Note: no --host given; running offline tests only.\n")

    results = runner.run(cfg, only_prefix=args.only, offline_only=args.offline_only)
    runner.print_report(results, color=not args.no_color)

    if args.out:
        runner.write_json(results, f"{args.out}.json")
        runner.write_markdown(results, f"{args.out}.md")
        print(f"\nWrote {args.out}.json and {args.out}.md")

    must_failures = sum(1 for r in results if r.level == "MUST" and r.status == "FAIL")
    return 1 if must_failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
