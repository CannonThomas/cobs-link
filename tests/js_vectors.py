#!/usr/bin/env python3
"""Emit 'payload-hex frame-hex' lines for tests/js_check.cjs."""
import pathlib
import random
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "tools"))
import cobs_link  # noqa: E402

rng = random.Random(99)
payloads = [b"", b"\x00", bytes(range(1, 255)), bytes(254) + b"\x01", b"\x07" * 254 + b"\x00"]
for _ in range(1000):
    n = rng.randrange(0, 700)
    bias = rng.choice([0.0, 0.02, 0.5])
    payloads.append(bytes(0 if rng.random() < bias else rng.randrange(256) for _ in range(n)))
for p in payloads:
    print(p.hex() or "-", cobs_link.encode_frame(p).hex())
