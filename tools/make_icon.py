"""Write images/antlia_10px.png, the 10x10 app icon.

Hand-plotted rather than drawn in an editor: at ten pixels square every pixel is
a decision, and a generated file keeps the source of truth in the repo instead of
in a binary nobody can edit. `#` is an ink pixel.

The glyph is a tag with an arrow leaving it — the tag is read, the ID goes out to
the host. Antlia is Lacaille's air pump: the instrument that moves the contents
of one vessel into another.
"""

import struct
import zlib
from pathlib import Path

GLYPH = [
    "..........",
    ".#####....",
    ".#...#....",
    ".#.#.#....",
    ".#...#..#.",
    ".#...#####",
    ".#...#..#.",
    ".#...#....",
    ".#####....",
    "..........",
]

WIDTH = HEIGHT = 10
BIT_DEPTH = 1


def chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def main() -> None:
    assert len(GLYPH) == HEIGHT, "glyph must be 10 rows"
    assert all(len(row) == WIDTH for row in GLYPH), "every row must be 10 columns"

    raw = bytearray()
    for row in GLYPH:
        raw.append(0)  # no filter
        packed = 0
        for column in range(16):  # 10 pixels, padded to two whole bytes
            ink = column < WIDTH and row[column] == "#"
            packed = (packed << 1) | (0 if ink else 1)
        raw += packed.to_bytes(2, "big")

    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, BIT_DEPTH, 0, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )
    target = Path(__file__).resolve().parent.parent / "images" / "antlia_10px.png"
    target.write_bytes(png)
    print(f"wrote {target} ({len(png)} bytes)")


if __name__ == "__main__":
    main()
