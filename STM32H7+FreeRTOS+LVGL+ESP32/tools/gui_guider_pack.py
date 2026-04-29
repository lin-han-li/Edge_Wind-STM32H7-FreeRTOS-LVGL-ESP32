#!/usr/bin/env python3
import argparse
import os
import re
import struct
from pathlib import Path


COLOR_FORMAT_MAP = {
    "LV_COLOR_FORMAT_RGB565A8": 0x14,
    "LV_COLOR_FORMAT_RGB565": 0x12,
    "LV_COLOR_FORMAT_RGB888": 0x10,
    "LV_COLOR_FORMAT_ARGB8888": 0x0B,
    "LV_COLOR_FORMAT_XRGB8888": 0x0A,
    "LV_COLOR_FORMAT_L8": 0x06,
    "LV_COLOR_FORMAT_A8": 0x08,
}


def parse_int(token: str) -> int:
    token = token.strip()
    if token.startswith(("0x", "0X")):
        return int(token, 16)
    return int(token, 10)


def parse_color_format(cf_token: str) -> int:
    if cf_token in COLOR_FORMAT_MAP:
        return COLOR_FORMAT_MAP[cf_token]
    return parse_int(cf_token)


def extract_field(text: str, field: str) -> str:
    pattern = rf"\.header\.{re.escape(field)}\s*=\s*([^,]+),"
    match = re.search(pattern, text)
    if not match:
        raise ValueError(f"missing header field: {field}")
    return match.group(1).strip()


def extract_array_data(text: str) -> bytes:
    match = re.search(r"uint8_t\s+\w+_map\[\]\s*=\s*\{", text)
    if not match:
        raise ValueError("missing data array")
    start = match.end()
    end = text.find("};", start)
    if end == -1:
        raise ValueError("unterminated data array")
    body = text[start:end]
    values = [int(x, 0) for x in re.findall(r"0x[0-9A-Fa-f]+|\d+", body)]
    return bytes(values)


def extract_array_name(text: str) -> str:
    match = re.search(r"uint8_t\s+(\w+)_map\[\]\s*=\s*\{", text)
    if not match:
        raise ValueError("missing array name")
    return match.group(1)


def build_header(cf: int, w: int, h: int, stride: int) -> bytes:
    magic = 0x19
    flags = 0
    reserved = 0
    word0 = (magic & 0xFF) | ((cf & 0xFF) << 8) | ((flags & 0xFFFF) << 16)
    word1 = (w & 0xFFFF) | ((h & 0xFFFF) << 16)
    word2 = (stride & 0xFFFF) | ((reserved & 0xFFFF) << 16)
    return struct.pack("<III", word0, word1, word2)


def convert_file(src: Path, dst_dir: Path) -> Path:
    text = src.read_text(encoding="utf-8", errors="ignore")

    cf_token = extract_field(text, "cf")
    stride_token = extract_field(text, "stride")
    w_token = extract_field(text, "w")
    h_token = extract_field(text, "h")

    cf = parse_color_format(cf_token)
    stride = parse_int(stride_token)
    w = parse_int(w_token)
    h = parse_int(h_token)

    data = extract_array_data(text)
    header = build_header(cf, w, h, stride)

    array_name = extract_array_name(text).lstrip("_")
    out_path = dst_dir / f"{array_name}.bin"
    out_path.write_bytes(header + data)
    return out_path


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert GUI-Guider LVGL C images into LVGL .bin images."
    )
    parser.add_argument(
        "--input",
        default=r"MDK-ARM/HARDWORK/GUI-Guider_Runtime/src/generated/images",
        help="Input directory containing GUI-Guider image .c files",
    )
    parser.add_argument(
        "--output",
        default=r"tools/qspi_assets/gui",
        help="Output directory for .bin images",
    )
    args = parser.parse_args()

    src_dir = Path(args.input)
    dst_dir = Path(args.output)
    dst_dir.mkdir(parents=True, exist_ok=True)

    converted = 0
    for src in sorted(src_dir.glob("*.c")):
        out_path = convert_file(src, dst_dir)
        converted += 1
        print(f"[OK] {src.name} -> {out_path.as_posix()}")

    if converted == 0:
        print("No .c image files found.")
    else:
        print(f"Converted {converted} files.")
        print("Fonts: use lv_font_conv to generate .bin files into tools/qspi_assets/fonts/")
        print("Example:")
        print("  lv_font_conv --format bin --font \"MDK-ARM/HARDWORK/EdgeWind_UI/fonts/SourceHanSerif.otf\" \\")
        print("    --size 20 --bpp 4 -o tools/qspi_assets/fonts/SourceHanSerifSC_Regular_20.bin")
        print("  lv_font_conv --format bin --font \"MDK-ARM/HARDWORK/EdgeWind_UI/fonts/SourceHanSerif.otf\" \\")
        print("    --size 60 --bpp 4 -o tools/qspi_assets/fonts/SourceHanSerifSC_Regular_60.bin")


if __name__ == "__main__":
    main()
