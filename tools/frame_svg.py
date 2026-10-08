#!/usr/bin/env python3
"""Render docs/frame.svg: how one payload turns into bytes on the wire.

The diagram is generated from the real encoder, so it cannot drift from the
code:  python3 tools/frame_svg.py
"""

import pathlib

import cobs_link

PAYLOAD = bytes.fromhex("48 69 00 21 00 00 2A")

CELL, GAP, LEFT, TOP, ROW = 46, 6, 150, 58, 96
BG, FG, MUTED = "#0d1117", "#e6edf3", "#8b949e"
DATA, ZERO, CRC, CODE, DELIM = "#1f6feb", "#da3633", "#8957e5", "#d29922", "#3fb950"


def cell(x, y, text, fill, label=None):
    out = [f'<rect x="{x}" y="{y}" width="{CELL}" height="{CELL}" rx="7" fill="{fill}"/>',
           f'<text x="{x + CELL / 2}" y="{y + 29}" class="b">{text}</text>']
    if label:
        out.append(f'<text x="{x + CELL / 2}" y="{y + CELL + 15}" class="s">{label}</text>')
    return out


def main():
    crc = cobs_link.crc16(PAYLOAD).to_bytes(2, "big")
    body = PAYLOAD + crc
    wire = cobs_link.encode_frame(PAYLOAD)

    # Which wire bytes are COBS code bytes?
    codes, i = set(), 0
    while i < len(wire) - 1:
        codes.add(i)
        i += wire[i]

    width = LEFT + len(wire) * (CELL + GAP) + 24
    height = TOP + 3 * ROW + 48
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" '
           f'width="{width}" height="{height}" font-family="ui-monospace,SFMono-Regular,Menlo,monospace">',
           f'<style>.b{{fill:#fff;font-size:15px;font-weight:600;text-anchor:middle}}'
           f'.s{{fill:{MUTED};font-size:10px;text-anchor:middle}}'
           f'.r{{fill:{FG};font-size:13px;font-weight:600}}.m{{fill:{MUTED};font-size:11px}}</style>',
           f'<rect width="{width}" height="{height}" rx="12" fill="{BG}"/>',
           f'<text x="24" y="32" class="r" font-size="15">One frame, from payload to wire</text>']

    rows = [("1  payload", "what you send"),
            ("2  + CRC-16", "inverted, big-endian"),
            ("3  COBS + 0x00", "no zeros until the end")]
    for n, (title, sub) in enumerate(rows):
        y = TOP + n * ROW
        svg.append(f'<text x="24" y="{y + 24}" class="r">{title}</text>')
        svg.append(f'<text x="24" y="{y + 40}" class="m">{sub}</text>')

    for n, b in enumerate(PAYLOAD):
        svg += cell(LEFT + n * (CELL + GAP), TOP, f"{b:02X}", ZERO if b == 0 else DATA)
    for n, b in enumerate(body):
        fill = CRC if n >= len(PAYLOAD) else (ZERO if b == 0 else DATA)
        svg += cell(LEFT + n * (CELL + GAP), TOP + ROW, f"{b:02X}", fill)
    for n, b in enumerate(wire):
        if n == len(wire) - 1:
            fill, label = DELIM, "end"
        elif n in codes:
            fill, label = CODE, f"+{b}"
        else:
            fill, label = (CRC if n > len(PAYLOAD) else DATA), None
        svg += cell(LEFT + n * (CELL + GAP), TOP + 2 * ROW, f"{b:02X}", fill, label)

    legend = [(DATA, "data"), (ZERO, "zero byte"), (CRC, "CRC"),
              (CODE, "COBS code: distance to next zero"), (DELIM, "frame delimiter")]
    x, y = 24, height - 22
    for colour, name in legend:
        svg.append(f'<rect x="{x}" y="{y - 10}" width="12" height="12" rx="3" fill="{colour}"/>')
        svg.append(f'<text x="{x + 18}" y="{y}" class="m">{name}</text>')
        x += 30 + 7 * len(name)
    svg.append("</svg>")

    out = pathlib.Path(__file__).resolve().parent.parent / "docs" / "frame.svg"
    out.write_text("\n".join(svg) + "\n")
    print(f"wrote {out.relative_to(out.parent.parent)}: wire = {wire.hex(' ')}")


if __name__ == "__main__":
    main()
