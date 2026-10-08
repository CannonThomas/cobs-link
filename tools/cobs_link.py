"""Host-side implementation of the cobs-link wire format.

Written independently of the C code so the two can be checked against each
other (see tests/cross_check.py). Also handy on the PC end of a serial link:

    import serial, cobs_link
    port = serial.Serial("/dev/tty.usbmodem1101", 115200)
    port.write(cobs_link.encode_frame(b"\\x01\\x02\\x03"))
"""

from __future__ import annotations

DELIMITER = 0


def crc16(data: bytes) -> int:
    """CRC-16/GENIBUS: poly 0x1021, init 0xFFFF, xorout 0xFFFF."""
    crc = 0xFFFF
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021 if crc & 0x8000 else crc << 1) & 0xFFFF
    return crc ^ 0xFFFF


def cobs_encode(data: bytes) -> bytes:
    out = bytearray()
    # Split on zeros, then cut each zero-free run into blocks of at most 254.
    runs = data.split(b"\x00")
    for i, run in enumerate(runs):
        last_run = i == len(runs) - 1
        if not run:
            out.append(0x01)
            continue
        for start in range(0, len(run), 254):
            block = run[start:start + 254]
            out.append(len(block) + 1)
            out += block
        # A run that ends exactly on a full block carries no implied zero, so
        # an explicit empty block is needed unless the data ends here.
        if len(run) % 254 == 0 and not last_run:
            out.append(0x01)
    return bytes(out)


def cobs_decode(data: bytes) -> bytes:
    out = bytearray()
    i = 0
    while i < len(data):
        code = data[i]
        if code == 0:
            raise ValueError("zero byte inside COBS data")
        block = data[i + 1:i + code]
        if len(block) != code - 1 or 0 in block:
            raise ValueError("malformed COBS block")
        out += block
        i += code
        if code != 0xFF and i < len(data):
            out.append(0)
    return bytes(out)


def encode_frame(payload: bytes) -> bytes:
    body = payload + crc16(payload).to_bytes(2, "big")
    return cobs_encode(body) + bytes([DELIMITER])


def decode_frame(frame: bytes) -> bytes:
    """Decode one frame (trailing delimiter optional). Raises ValueError."""
    if frame.endswith(bytes([DELIMITER])):
        frame = frame[:-1]
    body = cobs_decode(frame)
    if len(body) < 2:
        raise ValueError("frame too short")
    payload, want = body[:-2], int.from_bytes(body[-2:], "big")
    if crc16(payload) != want:
        raise ValueError("CRC mismatch")
    return payload
