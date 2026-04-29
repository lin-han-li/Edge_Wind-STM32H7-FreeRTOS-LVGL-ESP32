#!/usr/bin/env python3
from __future__ import annotations

import argparse
from collections import OrderedDict
from pathlib import Path


def _load_chars(chars_file: Path) -> list[str]:
    text = chars_file.read_text(encoding="utf-8", errors="ignore")
    # chars_cn.txt 通常是一行连续字符；这里按字符拆分并去重（保序）
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


def _filter_cjk_3byte(chars: list[str]) -> list[str]:
    """只保留 UTF-8 编码为 3 字节的字符（兼容当前 LVGL 拼音候选实现）。"""
    out: list[str] = []
    for ch in chars:
        try:
            if len(ch.encode("utf-8")) == 3:
                out.append(ch)
        except Exception:
            continue
    return out


def _to_pinyin_map(chars: list[str]) -> dict[str, str]:
    try:
        from pypinyin import Style, pinyin  # type: ignore
    except Exception as e:
        raise RuntimeError(
            "缺少依赖 pypinyin。请先运行: pip install pypinyin\n"
            f"import 失败: {e}"
        )

    # 拼音 -> 候选字符（保序去重）
    mp: dict[str, list[str]] = {}
    for ch in chars:
        # 只处理 CJK 范围更稳一点（避免把符号/偏旁强行转拼音）
        cp = ord(ch)
        if not (0x3400 <= cp <= 0x9FFF):
            continue

        py_list = pinyin(ch, style=Style.NORMAL, heteronym=False, errors="ignore")
        if not py_list or not py_list[0] or not py_list[0][0]:
            continue
        py = py_list[0][0].lower().strip()
        if not py or not ("a" <= py[0] <= "z"):
            continue
        mp.setdefault(py, []).append(ch)

    # 合并成字符串（去重保序）
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

    # 排序：按拼音字母序
    out_sorted = OrderedDict(sorted(out.items(), key=lambda kv: kv[0]))
    return dict(out_sorted)


def _write_pinyin_txt(pinyin_map: dict[str, str], out_path: Path) -> None:
    lines: list[str] = []
    for py, cand in pinyin_map.items():
        if not cand:
            continue
        lines.append(f"{py},{cand}")
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> None:
    root = Path(__file__).resolve().parent.parent
    default_chars = root / "MDK-ARM" / "HARDWORK" / "EdgeWind_UI" / "fonts" / "chars_cn.txt"
    default_out = root / "tools" / "pinyin" / "pinyin.txt"

    ap = argparse.ArgumentParser(description="从 chars_cn.txt 生成全量 pinyin.txt（拼音->汉字列表）")
    ap.add_argument("--chars", default=str(default_chars), help="chars_cn.txt 路径")
    ap.add_argument("--out", default=str(default_out), help="输出 pinyin.txt 路径")
    ap.add_argument("--keep-non3byte", action="store_true", help="不丢弃非 3 字节 UTF-8 字符（不推荐）")
    args = ap.parse_args()

    chars_path = Path(args.chars)
    out_path = Path(args.out)
    if not chars_path.is_absolute():
        chars_path = (root / chars_path).resolve()
    if not out_path.is_absolute():
        out_path = (root / out_path).resolve()

    if not chars_path.exists():
        raise FileNotFoundError(f"chars file not found: {chars_path.as_posix()}")

    chars = _load_chars(chars_path)
    if not args.keep_non3byte:
        chars = _filter_cjk_3byte(chars)

    pinyin_map = _to_pinyin_map(chars)
    _write_pinyin_txt(pinyin_map, out_path)

    total_chars = sum(len(v) for v in pinyin_map.values())
    print(f"[OK] pinyin.txt: {out_path.as_posix()}  entries={len(pinyin_map)} chars={total_chars}")


if __name__ == "__main__":
    main()

