#!/usr/bin/env python3
"""Validate batch envelopes produced by the firmware's real C++ path.

Runs tools/batch_dump against a field log, then checks every envelope and
item against the wardrift.net ingest schema. Exit 0 only if every batch is
well-formed and the sequence is strictly increasing.
"""
import json
import re
import subprocess
import sys

UUID = re.compile(r"^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")
ISO = re.compile(r"^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}Z$")
REQUIRED = {"ts", "lat", "lon", "kind", "bssid", "source_ts", "rssi", "ssid", "metadata"}


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


def main():
    log = sys.argv[1]
    out = subprocess.run(["./batch_dump", log], check=True,
                         capture_output=True, text=True)
    batches = [json.loads(line) for line in out.stdout.splitlines() if line.strip()]
    if not batches:
        print("PASS: empty log (headers only)")
        return
    last_seq = None
    total = 0
    for bi, b in enumerate(batches):
        if set(b) != {"idempotency_key", "sequence_no", "device_ts", "signature", "items"}:
            fail(f"batch {bi}: envelope keys {sorted(b)}")
        if not UUID.match(b["idempotency_key"]):
            fail(f"batch {bi}: bad uuid {b['idempotency_key']}")
        if not isinstance(b["sequence_no"], int):
            fail(f"batch {bi}: sequence not int")
        if last_seq is not None and b["sequence_no"] <= last_seq:
            fail(f"batch {bi}: sequence not increasing")
        last_seq = b["sequence_no"]
        if not ISO.match(b["device_ts"]):
            fail(f"batch {bi}: bad device_ts")
        if b["signature"] != "":
            fail(f"batch {bi}: signature not empty")
        if not (1 <= len(b["items"]) <= 250):
            fail(f"batch {bi}: item count {len(b['items'])}")
        for it in b["items"]:
            if set(it) != REQUIRED:
                fail(f"batch {bi}: item keys {sorted(it)}")
            if not ISO.match(it["ts"]) or not ISO.match(it["source_ts"]):
                fail(f"batch {bi}: item ts")
            if it["kind"] not in ("wifi", "bluetooth"):
                fail(f"batch {bi}: kind {it['kind']}")
            if not re.match(r"^([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}$", it["bssid"]):
                fail(f"batch {bi}: bssid {it['bssid']}")
            if not (-90 <= it["lat"] <= 90 and -180 <= it["lon"] <= 180):
                fail(f"batch {bi}: coords")
            if it["lat"] == 0 and it["lon"] == 0:
                fail(f"batch {bi}: zero fix")
            if not (-127 <= it["rssi"] <= 10):
                fail(f"batch {bi}: rssi")
            if not isinstance(it["ssid"], str):
                fail(f"batch {bi}: ssid type")
            if it["metadata"].get("source") != "wardriver-c5v2":
                fail(f"batch {bi}: metadata")
            total += 1
    print(f"PASS: {len(batches)} batches, {total} items "
          f"({out.stderr.strip()})")


if __name__ == "__main__":
    main()
