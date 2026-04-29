import struct
import sys
from pathlib import Path


PNG_SIG = b"\x89PNG\r\n\x1a\n"


def _png_wh(png: bytes) -> tuple[int, int] | None:
    if not png.startswith(PNG_SIG):
        return None
    # IHDR starts at byte 8, width/height at 16..24
    if len(png) < 24:
        return None
    w = int.from_bytes(png[16:20], "big")
    h = int.from_bytes(png[20:24], "big")
    if w <= 0 or h <= 0:
        return None
    return w, h


def wrap_png_as_ico(png: bytes) -> bytes:
    wh = _png_wh(png) or (64, 64)
    w, h = wh
    # ICO uses 0 to represent 256
    w_b = 0 if w >= 256 else w
    h_b = 0 if h >= 256 else h
    header = struct.pack("<HHH", 0, 1, 1)  # reserved, type=icon, count=1
    # ICONDIRENTRY: width, height, colorcount, reserved, planes, bitcount, bytes_in_res, image_offset
    entry = struct.pack("<BBBBHHII", w_b, h_b, 0, 0, 1, 32, len(png), 6 + 16)
    return header + entry + png


def main(argv: list[str]) -> int:
    if len(argv) < 3:
        print("Usage: make_icon.py <src> <dst>", file=sys.stderr)
        return 2
    src = Path(argv[1])
    dst = Path(argv[2])
    data = src.read_bytes()
    # If it's a PNG masquerading as .ico, wrap it into a real ICO container.
    if data.startswith(PNG_SIG):
        out = wrap_png_as_ico(data)
    else:
        out = data
    dst.write_bytes(out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))

