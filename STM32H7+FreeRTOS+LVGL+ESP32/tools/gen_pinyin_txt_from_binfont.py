#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
from collections import OrderedDict
from pathlib import Path


CMAP_FORMAT0_FULL = 0
CMAP_SPARSE_FULL = 1
CMAP_FORMAT0_TINY = 2
CMAP_SPARSE_TINY = 3


def _read_u32(blob: bytes, off: int) -> int:
    return struct.unpack_from("<I", blob, off)[0]


def _read_label(blob: bytes, start: int, expect: bytes) -> int:
    length = _read_u32(blob, start)
    tag = blob[start + 4 : start + 8]
    if tag != expect:
        raise ValueError(f"label mismatch @0x{start:08X}: expect={expect!r} got={tag!r}")
    if length < 8:
        raise ValueError(f"invalid section length @0x{start:08X}: {length}")
    return length


def _parse_head(blob: bytes) -> tuple[int, int]:
    """return (head_len, index_to_loc_format)"""
    head_len = _read_label(blob, 0, b"head")
    # font_header_bin_t is 40 bytes (see lv_binfont_loader.c)
    hdr_off = 8
    fmt = "<I" + "H" * 8 + "h" * 5 + "H" * 0  # placeholder, read selectively
    # easier: unpack only the last part we need using fixed offsets
    # layout:
    # u32 version (0)
    # u16 tables_count (4)
    # u16 font_size (6)
    # u16 ascent (8)
    # i16 descent (10)
    # u16 typo_ascent (12)
    # i16 typo_descent (14)
    # u16 typo_line_gap (16)
    # i16 min_y (18)
    # i16 max_y (20)
    # u16 default_advance_width (22)
    # u16 kerning_scale (24)
    # u8 index_to_loc_format (26)
    index_to_loc_format = blob[hdr_off + 26]
    return head_len, int(index_to_loc_format)


def _parse_cmaps(blob: bytes, cmaps_start: int) -> tuple[int, set[int]]:
    cmaps_len = _read_label(blob, cmaps_start, b"cmap")
    off = cmaps_start + 8
    sub_cnt = _read_u32(blob, off)
    off += 4

    table_fmt = "<IIHHHBB"  # cmap_table_bin_t (16 bytes)
    table_size = struct.calcsize(table_fmt)
    tables = []
    for _ in range(sub_cnt):
        data_offset, range_start, range_length, glyph_id_start, entries_count, fmt_type, _pad = struct.unpack_from(
            table_fmt, blob, off
        )
        off += table_size
        tables.append((data_offset, range_start, range_length, entries_count, fmt_type))

    cps: set[int] = set()
    for data_offset, range_start, range_length, entries_count, fmt_type in tables:
        if fmt_type in (CMAP_FORMAT0_TINY, CMAP_FORMAT0_FULL):
            # whole range
            for rcp in range(int(range_length)):
                cps.add(int(range_start) + rcp)
            # FORMAT0_FULL has extra glyph offset list; we don't need it for codepoints
            continue

        if fmt_type in (CMAP_SPARSE_TINY, CMAP_SPARSE_FULL):
            data_off = cmaps_start + int(data_offset)
            # unicode_list: uint16[entries_count] (relative codepoints)
            rels = struct.unpack_from("<" + "H" * int(entries_count), blob, data_off)
            for rcp in rels:
                cps.add(int(range_start) + int(rcp))
            continue

        # unknown cmap format, skip
        continue

    return cmaps_len, cps


def _filter_hanzi_only(codepoints: set[int]) -> list[str]:
    out: list[str] = []
    for cp in sorted(codepoints):
        # 只取 UTF-8 为 3 字节（BMP），并限制到常见汉字区（含扩展A/兼容区）
        if cp <= 0xFFFF:
            if (0x3400 <= cp <= 0x9FFF) or (0xF900 <= cp <= 0xFAFF):
                ch = chr(cp)
                if len(ch.encode("utf-8")) == 3:
                    out.append(ch)
    return out


def _load_extra_chars(extra_path: Path) -> list[str]:
    text = extra_path.read_text(encoding="utf-8", errors="ignore")
    seen: set[str] = set()
    out: list[str] = []
    for ch in text:
        if ch in ("\r", "\n", "\t", " "):
            continue
        if ch in seen:
            continue
        seen.add(ch)
        out.append(ch)
    return out


def _filter_3byte_hanzi(chars: list[str]) -> list[str]:
    out: list[str] = []
    for ch in chars:
        cp = ord(ch)
        if cp > 0xFFFF:
            continue
        if not ((0x3400 <= cp <= 0x9FFF) or (0xF900 <= cp <= 0xFAFF)):
            continue
        if len(ch.encode("utf-8")) != 3:
            continue
        out.append(ch)
    return out


def _to_pinyin_map(chars: list[str]) -> dict[str, str]:
    try:
        from pypinyin import Style, pinyin  # type: ignore
    except Exception as e:
        raise RuntimeError(
            "缺少依赖 pypinyin。请先运行: pip install pypinyin\n"
            f"import 失败: {e}"
        )

    mp: dict[str, list[str]] = {}
    skipped = 0
    for ch in chars:
        py_list = pinyin(ch, style=Style.NORMAL, heteronym=False, errors="ignore")
        if not py_list or not py_list[0] or not py_list[0][0]:
            skipped += 1
            continue
        py = py_list[0][0].lower().strip()
        if not py or not ("a" <= py[0] <= "z"):
            skipped += 1
            continue
        mp.setdefault(py, []).append(ch)

    out: dict[str, str] = {}
    for py, lst in mp.items():
        seen: set[str] = set()
        dedup: list[str] = []
        for ch in lst:
            if ch in seen:
                continue
            seen.add(ch)
            dedup.append(ch)
        out[py] = "".join(dedup)

    out_sorted = OrderedDict(sorted(out.items(), key=lambda kv: kv[0]))
    if skipped:
        print(f"[WARN] pypinyin 无法转拼音的字符数: {skipped}")
    return dict(out_sorted)


def _write_pinyin_txt(pinyin_map: dict[str, str], out_path: Path) -> None:
    lines: list[str] = []
    for py, cand in pinyin_map.items():
        if cand:
            lines.append(f"{py},{cand}")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    ap = argparse.ArgumentParser(description="从 LVGL binfont(.bin) 解析 glyph 列表并生成 pinyin.txt")
    ap.add_argument("--font", required=True, help="输入 binfont 文件路径（例如 SourceHanSerifSC_Regular_20.bin）")
    ap.add_argument("--out", default="tools/pinyin/pinyin.txt", help="输出 pinyin.txt 路径")
    ap.add_argument(
        "--extra",
        default=None,
        help="额外要强行并入的字符文件（例如项目扫描到的中文字符）。文件内容可为任意文本，脚本会提取其中的 3 字节汉字。",
    )
    ap.add_argument(
        "--report",
        default=None,
        help="输出报告文件（列出 extra 中不在 font20 内的缺字统计）",
    )
    args = ap.parse_args()

    font_path = Path(args.font)
    out_path = Path(args.out)
    if not font_path.is_absolute():
        font_path = (root / font_path).resolve()
    if not out_path.is_absolute():
        out_path = (root / out_path).resolve()

    blob = font_path.read_bytes()
    head_len, _loc_fmt = _parse_head(blob)
    cmaps_start = head_len
    _cmaps_len, cps = _parse_cmaps(blob, cmaps_start)
    font_chars = _filter_hanzi_only(cps)
    font_set = set(font_chars)

    extra_set: set[str] = set()
    extra_path: Path | None = None
    if args.extra:
        extra_path = Path(args.extra)
        if not extra_path.is_absolute():
            extra_path = (root / extra_path).resolve()
        if extra_path.exists():
            extra_chars = _filter_3byte_hanzi(_load_extra_chars(extra_path))
            extra_set = set(extra_chars)
        else:
            print(f"[WARN] extra 文件不存在，跳过: {extra_path.as_posix()}")

    union_chars = sorted(font_set | extra_set)
    missing = sorted(extra_set - font_set)

    pmap = _to_pinyin_map(union_chars)
    _write_pinyin_txt(pmap, out_path)

    total_chars = sum(len(v) for v in pmap.values())
    print(f"[OK] from binfont: {font_path.as_posix()}")
    print(f"[OK] pinyin.txt: {out_path.as_posix()} entries={len(pmap)} chars={total_chars}")
    if args.extra:
        print(f"[INFO] font chars={len(font_set)} extra chars={len(extra_set)} union={len(union_chars)} missing_in_font={len(missing)}")

    if args.report:
        rep = Path(args.report)
        if not rep.is_absolute():
            rep = (root / rep).resolve()
        rep.parent.mkdir(parents=True, exist_ok=True)
        lines = []
        lines.append(f"font: {font_path.as_posix()}")
        if extra_path:
            lines.append(f"extra: {extra_path.as_posix()}")
        lines.append(f"font_chars: {len(font_set)}")
        lines.append(f"extra_chars: {len(extra_set)}")
        lines.append(f"union_chars: {len(union_chars)}")
        lines.append(f"missing_in_font: {len(missing)}")
        lines.append("")
        if missing:
            lines.append("missing_chars:")
            lines.append("".join(missing))
            lines.append("")
        rep.write_text("\n".join(lines), encoding="utf-8")
        print(f"[OK] report: {rep.as_posix()}")


if __name__ == "__main__":
    main()

