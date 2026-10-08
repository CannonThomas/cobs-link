#!/usr/bin/env python3
"""Check the C library against the independent Python implementation.

Random payloads are framed by both; the bytes on the wire must be identical,
and each side must decode what the other produced.
"""

import pathlib
import random
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))

import cobs_link  # noqa: E402

CLI = ROOT / "build" / "cl_cli"
CASES = 3000


def make_payloads(rng: random.Random) -> list[bytes]:
    payloads = [b"", b"\x00", b"\x00" * 300, bytes(range(1, 255)), bytes(range(256)) * 3]
    # every length around the 254-byte block boundary, with and without zeros
    for n in range(250, 262):
        payloads.append(bytes([0x55]) * n)
        payloads.append(bytes([0x55]) * n + b"\x00")
    while len(payloads) < CASES:
        n = rng.randrange(0, 1200)
        zero_bias = rng.choice([0.0, 0.01, 0.5])
        payloads.append(bytes(0 if rng.random() < zero_bias else rng.randrange(256)
                              for _ in range(n)))
    return payloads


def run_cli(mode: str, lines: list[str]) -> list[str]:
    result = subprocess.run([str(CLI), mode], input="\n".join(lines) + "\n",
                            capture_output=True, text=True, check=True)
    return result.stdout.splitlines()


def main() -> int:
    payloads = make_payloads(random.Random(1234))

    c_frames = run_cli("encode", [p.hex() for p in payloads])
    assert len(c_frames) == len(payloads)

    failures = 0
    for payload, c_hex in zip(payloads, c_frames):
        py_frame = cobs_link.encode_frame(payload)
        if bytes.fromhex(c_hex) != py_frame:
            failures += 1
            print(f"encode mismatch for {len(payload)}-byte payload")
        elif cobs_link.decode_frame(py_frame) != payload:
            failures += 1
            print(f"python decode mismatch for {len(payload)}-byte payload")

    c_decoded = run_cli("decode", [cobs_link.encode_frame(p).hex() for p in payloads])
    for payload, got in zip(payloads, c_decoded):
        if got != payload.hex():
            failures += 1
            print(f"C decode mismatch for {len(payload)}-byte payload")

    print(f"cross-check: {len(payloads)} payloads, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
