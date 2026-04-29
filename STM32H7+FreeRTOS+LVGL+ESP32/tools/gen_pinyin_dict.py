#!/usr/bin/env python3
from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path

MAGIC = 0x50594442  # 'PYDB'
VERSION = 0x0001


def _normalize_line(line: str) -> str:
    line = line.strip()
    if not line:
        return ""
    if line.startswith("#") or line.startswith("//"):
        return ""
    return line.replace("，", ",")


def _parse_pinyin_file(path: Path) -> list[tuple[str, str]]:
    text = path.read_text(encoding="utf-8", errors="ignore")
    entries: dict[str, list[str]] = {}

    for idx, raw in enumerate(text.splitlines(), 1):
        line = _normalize_line(raw)
        if not line:
            continue

        parts = [p.strip() for p in line.split(",") if p.strip()]
        if len(parts) < 2:
            print(f"[WARN] skip invalid line {idx}: {raw}")
            continue

        py = parts[0].lower()
        if not py or not ("a" <= py[0] <= "z"):
            print(f"[WARN] skip invalid pinyin {idx}: {py}")
            continue

        cand = "".join(parts[1:])
        cand = re.sub(r"\s+", "", cand)
        if not cand:
            print(f"[WARN] skip empty candidate {idx}: {py}")
            continue

        filtered = _filter_ime_chars(py, cand)
        if not filtered:
            print(f"[WARN] skip empty candidate after filter {idx}: {py}")
            continue

        entries.setdefault(py, []).append(filtered)

    merged: list[tuple[str, str]] = []
    for py, cand_list in entries.items():
        all_cand = "".join(cand_list)
        # Deduplicate characters while preserving order
        seen: set[str] = set()
        dedup: list[str] = []
        for ch in all_cand:
            if ch in seen:
                continue
            seen.add(ch)
            dedup.append(ch)
        merged.append((py, "".join(dedup)))

    merged.sort(key=lambda x: (x[0][0], x[0]))
    return merged


def _filter_ime_chars(py: str, cand: str) -> str:
    kept: list[str] = []
    dropped: list[str] = []
    for ch in cand:
        if len(ch.encode("utf-8")) == 3:
            kept.append(ch)
        else:
            dropped.append(ch)
    if dropped:
        seen: set[str] = set()
        uniq: list[str] = []
        for ch in dropped:
            if ch in seen:
                continue
            seen.add(ch)
            uniq.append(ch)
        print(f"[WARN] drop non-3byte chars for {py}: {''.join(uniq)}")
    return "".join(kept)


def _build_bin(entries: list[tuple[str, str]]) -> bytes:
    header_size = 4 + 2 + 2 + 4 + 4 + 4
    entry_size = 4 + 4
    entry_count = len(entries)

    table_offset = header_size
    strings_offset = table_offset + entry_size * entry_count

    table_data = bytearray()
    strings = bytearray()

    for py, cand in entries:
        py_bytes = py.encode("ascii", errors="ignore") + b"\0"
        cand_bytes = cand.encode("utf-8") + b"\0"

        if (len(cand_bytes) - 1) % 3 != 0:
            print(f"[WARN] candidate length not multiple of 3 bytes: {py}")

        py_off = strings_offset + len(strings)
        strings.extend(py_bytes)
        cand_off = strings_offset + len(strings)
        strings.extend(cand_bytes)

        table_data.extend(struct.pack("<II", py_off, cand_off))

    header = struct.pack(
        "<IHHIII",
        MAGIC,
        VERSION,
        0,
        entry_count,
        table_offset,
        strings_offset,
    )

    return bytes(header + table_data + strings)


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Convert comma-separated pinyin.txt to pinyin_dict.bin"
    )
    parser.add_argument(
        "--input",
        default=None,
        help="Input pinyin.txt path (default: tools/pinyin/pinyin.txt or tools/pinyin.txt)",
    )
    parser.add_argument(
        "--output",
        default=None,
        help="Output pinyin_dict.bin path (default: tools/pinyin/pinyin_dict.bin)",
    )
    args = parser.parse_args()

    root = Path(__file__).resolve().parent.parent
    default_txt = root / "tools" / "pinyin" / "pinyin.txt"
    fallback_txt = root / "tools" / "pinyin.txt"
    default_bin = root / "tools" / "pinyin" / "pinyin_dict.bin"

    in_path = Path(args.input) if args.input else (default_txt if default_txt.exists() else fallback_txt)
    out_path = Path(args.output) if args.output else default_bin
    if not in_path.is_absolute():
        in_path = (root / in_path).resolve()
    if not out_path.is_absolute():
        out_path = (root / out_path).resolve()

    if not in_path.exists():
        raise FileNotFoundError(f"pinyin.txt not found: {in_path.as_posix()}")

    entries = _parse_pinyin_file(in_path)
    if not entries:
        raise RuntimeError("No valid pinyin entries parsed")

    blob = _build_bin(entries)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_bytes(blob)

    print(f"[OK] pinyin dict: {out_path.as_posix()} ({len(blob)} bytes), entries={len(entries)}")


if __name__ == "__main__":
    main()
