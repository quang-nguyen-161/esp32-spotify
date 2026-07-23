"""
Preview a raw RGB565 artwork.bin file (as produced by /api/now-playing?art=raw)
as a normal image.

Usage:
    python preview_artwork.py artwork.bin
    python preview_artwork.py artwork.bin --size 120
    python preview_artwork.py artwork.bin --size 120 --out preview.png --scale 4

Requires: pip install pillow
"""

import argparse
import struct
import sys
from pathlib import Path

from PIL import Image


def rgb565_to_rgb888(value: int) -> tuple[int, int, int]:
    r5 = (value >> 11) & 0x1F
    g6 = (value >> 5) & 0x3F
    b5 = value & 0x1F
    r8 = round(r5 * 255 / 31)
    g8 = round(g6 * 255 / 63)
    b8 = round(b5 * 255 / 31)
    return r8, g8, b8


def decode_rgb565_bin(path: Path, size: int) -> Image.Image:
    data = path.read_bytes()
    expected = size * size * 2
    if len(data) != expected:
        print(
            f"Warning: file is {len(data)} bytes, expected {expected} "
            f"for a {size}x{size} RGB565 image. Check --size matches ART_SIZE on the server.",
            file=sys.stderr,
        )

    pixel_count = min(size * size, len(data) // 2)
    img = Image.new("RGB", (size, size))
    pixels = img.load()

    for i in range(pixel_count):
        # server sends big-endian RGB565, 2 bytes per pixel
        value = struct.unpack_from(">H", data, i * 2)[0]
        r, g, b = rgb565_to_rgb888(value)
        x = i % size
        y = i // size
        pixels[x, y] = (r, g, b)

    return img


def main():
    parser = argparse.ArgumentParser(description="Preview a raw RGB565 artwork.bin file")
    parser.add_argument("file", type=Path, help="path to artwork.bin")
    parser.add_argument("--size", type=int, default=120, help="image width/height in pixels (default: 120)")
    parser.add_argument("--out", type=Path, default=None, help="save decoded PNG to this path")
    parser.add_argument("--scale", type=int, default=4, help="scale factor when displaying/saving (default: 4)")
    parser.add_argument("--no-show", action="store_true", help="don't open a preview window, just save")
    args = parser.parse_args()

    if not args.file.exists():
        print(f"File not found: {args.file}", file=sys.stderr)
        sys.exit(1)

    img = decode_rgb565_bin(args.file, args.size)

    if args.scale != 1:
        img = img.resize(
            (args.size * args.scale, args.size * args.scale),
            Image.NEAREST,  # keep pixels crisp, like the real display
        )

    out_path = args.out or args.file.with_suffix(".png")
    img.save(out_path)
    print(f"Saved preview: {out_path}")

    if not args.no_show:
        img.show()


if __name__ == "__main__":
    main()
