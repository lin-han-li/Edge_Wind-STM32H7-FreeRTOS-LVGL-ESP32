#!/usr/bin/env python3
"""
生成 SD 卡资源目录（0:/gui 与 0:/fonts），一键扫描工程 UI 中文文案、更新字符表、生成字库 bin。

- 图片：从 GUI-Guider 的 images/*.c 转换为 LVGL .bin（复用 tools/gui_guider_pack.py）
- 字体：自动扫描 GUI-Guider_Runtime 和 EdgeWind_UI 中的 UI 文案，提取中文字符，
        更新 MDK-ARM/HARDWORK/EdgeWind_UI/fonts/chars_cn.txt，
        然后用 lv_font_conv 生成 SourceHanSerifSC_Regular_{12,14,16,20,30}.bin
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


FONT_SIZES = [12, 14, 16, 20, 30]  # 生成 12/14/16/20/30（日志小字/小字/常用/状态栏）
DEFAULT_FONT_BPP = 4
DEFAULT_FONT_OTF = "MDK-ARM/HARDWORK/EdgeWind_UI/fonts/SourceHanSerif.otf"
DEFAULT_CHARS_FILE = "MDK-ARM/HARDWORK/EdgeWind_UI/fonts/chars_cn.txt"
DEFAULT_USER_CHARS_FILE = "MDK-ARM/HARDWORK/EdgeWind_UI/fonts/chars_cn_user.txt"
DEFAULT_PINYIN_TXT = "tools/pinyin/pinyin.txt"
DEFAULT_PINYIN_DICT = "tools/pinyin/pinyin_dict.bin"
FULL_CN_MIN = 7200  # 目标“7000+”中文字符数量
FULL_CN_RANGE = (0x4E00, 0x9FFF)
# 额外强制加入字库的“中文标点/括号/全角符号”（避免显示为方框）
# - 包含常见中文标点、书名号/引号、各种括号、分隔符等
# - 同时包含 Unicode: CJK Symbols and Punctuation (U+3000..U+303F)
def _build_extra_always_symbols() -> set[str]:
    s: set[str] = set()

    # 常见中文标点/符号（可按需继续追加）
    s |= set(
        "，。！？、；："
        "“”‘’"
        "《》〈〉"
        "「」『』"
        "（）；【】〔〕〖〗"
        "—…·～"
        "￥"
    )

    # 你点名的符号：全角括号/括号族/竖线（半角竖线已包含在 ASCII 0x20-0x7F 范围）
    s |= set("（）｛｝【】｜")

    # 一些常用全角标点（避免 UI/日志用到全角时缺字）
    s |= set("，．：；？！＂＇（）［］｛｝＜＞《》【】｜～—…·")

    # Unicode: CJK Symbols and Punctuation
    for cp in range(0x3000, 0x303F + 1):
        s.add(chr(cp))

    return s


EXTRA_ALWAYS_SYMBOLS: set[str] = _build_extra_always_symbols()

def _get_lv_font_conv_cmd() -> list[str]:
    """返回可执行的 lv_font_conv 命令前缀（list[str]）。

    重点：Windows 下如果走 `npx.cmd`，超长 `--symbols` 会触发 cmd.exe 的 8191 字符限制，
    导致 `The command line is too long.`。因此在 Windows 上改为直接用 `node.exe` 调用
    `lv_font_conv.js`（通过 npm 安装到用户缓存目录，不污染仓库）。
    """
    # Windows: 使用 node 直接跑 js，绕过 npx.cmd 的 cmd.exe 限制
    if sys.platform.startswith("win"):
        node = shutil.which("node")
        npm = shutil.which("npm")
        if not node or not npm:
            raise RuntimeError(
                "[ERROR] 未检测到 Node.js 环境（node/npm）。\n"
                "        请安装 Node.js (https://nodejs.org/) 后重试。"
            )

        base = os.environ.get("LOCALAPPDATA") or tempfile.gettempdir()
        cache_dir = Path(base) / "lv_font_conv_cache"
        cache_dir.mkdir(parents=True, exist_ok=True)

        # 最小 package.json（避免 npm 报错）
        pkg_json = cache_dir / "package.json"
        if not pkg_json.exists():
            pkg_json.write_text('{"name":"lv_font_conv_cache","private":true}\n', encoding="utf-8")

        # npm bin 映射：{"lv_font_conv":"lv_font_conv.js"}（包根目录）
        lv_font_conv_js = cache_dir / "node_modules" / "lv_font_conv" / "lv_font_conv.js"
        if not lv_font_conv_js.exists():
            print(f"[INFO] 正在安装/更新 lv_font_conv 到缓存目录: {cache_dir.as_posix()}")
            cmd = [npm, "install", "--silent", "--no-fund", "--no-audit", "lv_font_conv@latest"]
            subprocess.check_call(cmd, cwd=str(cache_dir))
            if not lv_font_conv_js.exists():
                raise RuntimeError(
                    "[ERROR] 安装 lv_font_conv 成功但未找到 lv_font_conv.js。\n"
                    f"        期望路径: {lv_font_conv_js.as_posix()}"
                )

        return [node, str(lv_font_conv_js)]

    # 非 Windows：优先 npx，其次全局 lv_font_conv
    npx = shutil.which("npx")
    if npx:
        return [npx, "--yes", "lv_font_conv@latest"]

    lv_font_conv_bin = shutil.which("lv_font_conv")
    if lv_font_conv_bin:
        return [lv_font_conv_bin]

    raise RuntimeError(
        "[ERROR] 未检测到 npx 或 lv_font_conv 命令。\n"
        "        请安装 Node.js (https://nodejs.org/) 后重试，\n"
        "        或运行: npm install -g lv_font_conv"
    )


def project_root() -> Path:
    return Path(__file__).resolve().parent.parent


def find_images_dir(root: Path) -> Path:
    candidates = [
        root / "MDK-ARM/HARDWORK/GUI-Guider_Runtime/src/generated/images",
        root / "MDK-ARM/HARDWORK/GUI-Guider_Source/src/generated/images",
    ]
    for p in candidates:
        if p.exists() and any(p.glob("*.c")):
            return p
    raise FileNotFoundError("找不到 GUI-Guider images/*.c 目录（Runtime/Source 都没有）")


def run_pack_images(root: Path, images_dir: Path, out_gui_dir: Path) -> None:
    packer = root / "tools/gui_guider_pack.py"
    cmd = [
        sys.executable,
        str(packer),
        "--input",
        str(images_dir),
        "--output",
        str(out_gui_dir),
    ]
    print("[STEP] 打包图标:", " ".join(cmd))
    subprocess.check_call(cmd, cwd=str(root))


def _decode_c_string(s: str) -> str:
    """Best-effort decode C string literal content (handles \\n, \\xNN, \\uNNNN, \\UNNNNNNNN)."""
    s = s.replace(r"\\", "\\").replace(r"\"", "\"")
    s = s.replace(r"\n", "\n").replace(r"\r", "\r").replace(r"\t", "\t")

    def repl(m: re.Match[str]) -> str:
        return chr(int(m.group(1), 16))

    s = re.sub(r"\\x([0-9A-Fa-f]{2})", repl, s)
    s = re.sub(r"\\u([0-9A-Fa-f]{4})", repl, s)
    s = re.sub(r"\\U([0-9A-Fa-f]{8})", repl, s)
    return s


def collect_ui_chinese_chars(root: Path) -> set[str]:
    """扫描 UI + 日志文案，提取所有中文字符（Unicode 0x4E00-0x9FFF）

    说明：除 UI 文案外，还会扫描常见日志来源（ESP8266/SD_Card/Core），保证控制台/日志区域不缺字。
    """
    scan_dirs = [
        # UI（GUI Guider 生成代码 + 业务 UI 资源）
        root / "MDK-ARM/HARDWORK/GUI-Guider_Runtime/src/generated",
        root / "MDK-ARM/HARDWORK/EdgeWind_UI",
        # 日志（ESP/存储/系统）
        root / "MDK-ARM/HARDWORK/ESP8266",
        root / "MDK-ARM/HARDWORK/SD_Card",
        root / "Core/Src",
        root / "Core/Inc",
    ]

    chinese_chars: set[str] = set(EXTRA_ALWAYS_SYMBOLS)
    # 直接提取 C 字符串字面量（覆盖 create_tile 等包装函数里的文案）
    patterns = [
        r'"((?:\\.|[^"\\])*)"',
    ]

    for scan_dir in scan_dirs:
        if not scan_dir.exists():
            print(f"[WARN] 扫描目录不存在: {scan_dir}")
            continue

        print(f"[SCAN] 扫描中文文案: {scan_dir}")
        for p in list(scan_dir.rglob("*.c")) + list(scan_dir.rglob("*.h")):
            try:
                content = p.read_text(encoding="utf-8", errors="ignore")
                for pattern in patterns:
                    for m in re.finditer(pattern, content):
                        text = _decode_c_string(m.group(1))
                        for ch in text:
                            cp = ord(ch)
                            # 中文 CJK 统一表意文字主要范围：0x4E00-0x9FFF
                            if (0x4E00 <= cp <= 0x9FFF) or (ch in EXTRA_ALWAYS_SYMBOLS):
                                chinese_chars.add(ch)
            except Exception as e:
                print(f"[WARN] 读取 {p} 失败: {e}")

    print(f"[INFO] 扫描到 {len(chinese_chars)} 个不同的中文字符")
    return chinese_chars


def ensure_min_cn_chars(chars: set[str], min_count: int) -> set[str]:
    """确保中文字符数量达到 min_count，不足时用 Unicode 范围补齐"""
    if len(chars) >= min_count:
        return chars
    start, end = FULL_CN_RANGE
    for cp in range(start, end + 1):
        ch = chr(cp)
        if ch not in chars:
            chars.add(ch)
        if len(chars) >= min_count:
            break
    return chars


def _read_cn_chars_file(path: Path) -> set[str]:
    """从文本文件中提取字符集合（默认：CJK(0x4E00-0x9FFF) + 常用中文标点）"""
    out: set[str] = set()
    if not path.exists():
        return out
    try:
        content = path.read_text(encoding="utf-8", errors="ignore")
        for ch in content:
            cp = ord(ch)
            if (0x4E00 <= cp <= 0x9FFF) or (ch in EXTRA_ALWAYS_SYMBOLS):
                out.add(ch)
    except Exception as e:
        print(f"[WARN] 读取 {path.as_posix()} 失败: {e}")
    return out


def _read_cn_chars_from_pinyin_txt(path: Path) -> set[str]:
    """从 pinyin.txt 中提取“候选汉字/符号”集合，用于保证候选词不会显示为方框。

    支持格式：pinyin,汉字列表
    """
    out: set[str] = set()
    if not path.exists():
        return out
    try:
        for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
            s = line.strip()
            if not s or s.startswith("#"):
                continue
            # 允许行内注释
            if "#" in s:
                s = s.split("#", 1)[0].strip()
            if not s:
                continue
            if "," not in s:
                continue
            _, rhs = s.split(",", 1)
            for ch in rhs:
                cp = ord(ch)
                if (0x4E00 <= cp <= 0x9FFF) or (ch in EXTRA_ALWAYS_SYMBOLS):
                    out.add(ch)
    except Exception as e:
        print(f"[WARN] 读取 pinyin.txt 失败: {path.as_posix()} : {e}")
    return out


def update_chars_cn_file(
    root: Path,
    new_chars: set[str],
    chars_file_path: str,
    user_chars_file_path: str,
    min_cn: int = 0,
    keep_existing: bool = False,
) -> Path:
    """更新 chars_cn.txt：保留原有字符，合并扫描结果 + 用户追加字符，去重排序后写回。

    默认不再强制补齐 7000+（你现在希望只生成“用到的字”）。
    如需全量补齐，可通过 --min-cn / --full-cn 控制。
    """
    chars_file = root / chars_file_path
    chars_file.parent.mkdir(parents=True, exist_ok=True)

    existing_chars = _read_cn_chars_file(chars_file) if keep_existing else set()
    if not keep_existing:
        print("[INFO] chars_cn.txt 使用重建模式：不保留旧字符集（用于裁剪全量字库）")
    user_chars_file = root / user_chars_file_path
    user_chars = _read_cn_chars_file(user_chars_file)
    if user_chars:
        print(f"[INFO] 合并用户追加字符: {user_chars_file_path} (共 {len(user_chars)} 字)")

    # 合并
    all_chars = existing_chars | new_chars | user_chars
    if min_cn and len(all_chars) < min_cn:
        before = len(all_chars)
        all_chars = ensure_min_cn_chars(all_chars, min_cn)
        print(f"[INFO] 中文字符补齐: {before} -> {len(all_chars)} (目标 {min_cn})")
    if not all_chars:
        print(f"[WARN] 未扫描到任何中文字符，保持 {chars_file_path} 不变")
        if chars_file.exists():
            return chars_file
        # 若文件不存在且无字符，创建空文件
        chars_file.write_text("", encoding="utf-8")
        return chars_file

    # 按 Unicode 码位排序
    sorted_chars = sorted(all_chars, key=lambda c: ord(c))
    # 写回文件（每行若干字符，便于阅读，这里简单用换行分隔每个字）
    chars_file.write_text("".join(sorted_chars), encoding="utf-8")
    print(f"[OK] 更新字符表: {chars_file.as_posix()} (共 {len(all_chars)} 字)")
    return chars_file


def generate_font_bins(
    root: Path,
    chars_file: Path,
    out_fonts_dir: Path,
    ui_scanned_chars: set[str] | None = None,
    user_chars_file: Path | None = None,
    user_chars_scope: str = "font20",
) -> None:
    """用 lv_font_conv 生成 12/14/16/20/30 五个字号的 .bin 字库"""
    font_otf = root / DEFAULT_FONT_OTF
    if not font_otf.exists():
        raise FileNotFoundError(
            f"[ERROR] 字体源文件不存在: {font_otf.as_posix()}\n"
            f"        请将思源宋体 OTF 文件放置到该路径，或修改脚本中的 DEFAULT_FONT_OTF"
        )

    if not chars_file.exists() or chars_file.stat().st_size == 0:
        raise FileNotFoundError(
            f"[ERROR] 字符表为空或不存在: {chars_file.as_posix()}\n"
            f"        请确保工程中有 UI 文案使用中文，或手动维护该文件"
        )

    # 读取字符表内容（用于 --symbols）
    all_symbols_set = _read_cn_chars_file(chars_file)
    if not all_symbols_set:
        raise ValueError(f"[ERROR] 字符表内容为空: {chars_file.as_posix()}")

    lv_font_conv_cmd = _get_lv_font_conv_cmd()

    out_fonts_dir.mkdir(parents=True, exist_ok=True)

    # 默认：用户手工追加字符（chars_cn_user.txt）只并入 20px（拼音候选用 20px）。
    # 其它字号仍保留“UI 扫描到的字”，避免 UI 缺字，同时避免把大量“手工补字”扩散到所有字号。
    user_extra_only: set[str] = set()
    if user_chars_scope not in ("all", "font20"):
        raise ValueError("[ERROR] user_chars_scope must be 'all' or 'font20'")
    if user_chars_scope == "font20":
        if ui_scanned_chars is None:
            ui_scanned_chars = collect_ui_chinese_chars(root)
        if user_chars_file is None:
            user_chars_file = root / DEFAULT_USER_CHARS_FILE
        user_chars = _read_cn_chars_file(user_chars_file)
        # 仅把“非 UI 扫描字符”的手工追加字当作 extra；
        # 但不要把强制标点算进去（标点应进入所有字号）。
        user_extra_only = (user_chars - ui_scanned_chars) - EXTRA_ALWAYS_SYMBOLS
        if user_extra_only:
            print(f"[INFO] 用户追加字符仅并入 20px：其它字号将剔除 {len(user_extra_only)} 个非 UI 扫描字符")

    for size in FONT_SIZES:
        out_bin = out_fonts_dir / f"SourceHanSerifSC_Regular_{size}.bin"
        symbols_set = all_symbols_set
        if user_extra_only and size != 20:
            symbols_set = all_symbols_set - user_extra_only
        # 强制标点进入所有字号
        symbols_set = symbols_set | EXTRA_ALWAYS_SYMBOLS
        symbols = "".join(sorted(symbols_set, key=lambda c: ord(c)))
        cmd = [
            *lv_font_conv_cmd,
            "--format",
            "bin",
            "--font",
            str(font_otf),
            "--size",
            str(size),
            "--bpp",
            str(DEFAULT_FONT_BPP),
            "--range",
            "0x20-0x7F",  # ASCII
            "--symbols",
            symbols,  # 中文字符
            "-o",
            str(out_bin),
        ]
        # 避免打印超长命令行（symbols 可能 7000+）
        print(f"[STEP] 生成字体 {size}px:", " ".join(cmd[:6]), "...", f"-o {out_bin.name}")
        try:
            subprocess.check_call(cmd, cwd=str(root))
            print(f"[OK] 字体生成成功: {out_bin.as_posix()} ({out_bin.stat().st_size} bytes)")
        except subprocess.CalledProcessError as e:
            raise RuntimeError(f"[ERROR] 字体生成失败: {out_bin.as_posix()}\n{e}")


def generate_pinyin_dict(root: Path, txt_path: Path, out_bin: Path) -> bool:
    script = root / "tools" / "gen_pinyin_dict.py"
    if not script.exists():
        print(f"[WARN] 未找到脚本: {script.as_posix()}")
        return False

    if not txt_path.exists():
        print(f"[INFO] 未找到拼音源文件，跳过: {txt_path.as_posix()}")
        return False

    cmd = [
        sys.executable,
        str(script),
        "--input",
        str(txt_path),
        "--output",
        str(out_bin),
    ]
    print("[STEP] 生成拼音字典:", " ".join(cmd))
    subprocess.check_call(cmd, cwd=str(root))
    return out_bin.exists()


def generate_pinyin_txt_from_chars(root: Path, chars_file: Path, out_txt: Path) -> bool:
    script = root / "tools" / "gen_pinyin_txt_from_chars.py"
    if not script.exists():
        print(f"[WARN] 未找到脚本: {script.as_posix()}")
        return False

    if not chars_file.exists():
        print(f"[WARN] 未找到 chars 文件: {chars_file.as_posix()}")
        return False

    cmd = [
        sys.executable,
        str(script),
        "--chars",
        str(chars_file),
        "--out",
        str(out_txt),
    ]
    print("[STEP] 从 chars_cn.txt 生成全量 pinyin.txt:", " ".join(cmd))
    subprocess.check_call(cmd, cwd=str(root))
    return out_txt.exists()


def generate_pinyin_txt_from_binfont(root: Path, font_bin: Path, out_txt: Path) -> bool:
    script = root / "tools" / "gen_pinyin_txt_from_binfont.py"
    if not script.exists():
        print(f"[WARN] 未找到脚本: {script.as_posix()}")
        return False
    if not font_bin.exists():
        print(f"[WARN] 未找到字库文件: {font_bin.as_posix()}")
        return False

    cmd = [
        sys.executable,
        str(script),
        "--font",
        str(font_bin),
        "--out",
        str(out_txt),
    ]
    print("[STEP] 从 20px binfont 生成全量 pinyin.txt:", " ".join(cmd))
    subprocess.check_call(cmd, cwd=str(root))
    return out_txt.exists()

def write_readme(out_dir: Path) -> None:
    readme = out_dir / "README_SD_COPY.txt"
    readme.write_text(
        "【SD 卡资源拷贝说明】\n\n"
        "1. 将本目录下的 gui/ 与 fonts/ 两个文件夹直接拷贝到 SD 卡根目录。\n"
        "   即：\n"
        "     - tools/sd_payload/gui/   → SD:/gui/\n"
        "     - tools/sd_payload/fonts/ → SD:/fonts/\n\n"
        "2. 如需强制板子同步资源到 QSPI Flash：\n"
        "   在 SD:/gui 下放置 update.flag（空文件即可）。\n\n"
        "3. 确认工程加载字库文件名：\n"
        "   - SourceHanSerifSC_Regular_12.bin (日志小字，可选)\n"
        "   - SourceHanSerifSC_Regular_14.bin (小字，可选)\n"
        "   - SourceHanSerifSC_Regular_16.bin (小字，可选)\n"
        "   - SourceHanSerifSC_Regular_20.bin (gui_assets.c)\n"
        "   - SourceHanSerifSC_Regular_30.bin (状态栏，可选)\n\n"
        "4. 拼音字典（可选，用于中文输入法）：\n"
        "   - pinyin/pinyin_dict.bin\n",
        encoding="utf-8",
    )


def main() -> None:
    ap = argparse.ArgumentParser(description="一键生成 SD 卡资源 payload（扫描中文+生成字库+打包图标）")
    ap.add_argument(
        "--out",
        default="tools/sd_payload",
        help="输出目录（默认：tools/sd_payload）",
    )
    ap.add_argument(
        "--force-update-flag",
        action="store_true",
        help="在输出 gui/ 下生成 update.flag（空文件），用于强制板子同步",
    )
    ap.add_argument(
        "--min-cn",
        type=int,
        default=0,
        help="最少中文字符数量（默认 0：不补齐，只生成“用到的字”）",
    )
    ap.add_argument(
        "--full-cn",
        action="store_true",
        help=f"补齐到 {FULL_CN_MIN}+ 全量（等价于 --min-cn {FULL_CN_MIN}）",
    )
    ap.add_argument(
        "--keep-existing-chars",
        action="store_true",
        help="保留旧的 chars_cn.txt 再叠加（默认：不保留，按当前扫描/词库/手工字重建，用于裁剪字库）",
    )
    ap.add_argument(
        "--pinyin-from-chars",
        action="store_true",
        help="从 chars_cn.txt 自动生成 tools/pinyin/pinyin.txt（需要 pip install pypinyin）",
    )
    ap.add_argument(
        "--pinyin-from-font20",
        action="store_true",
        help="从生成出来的 20px 字库 bin (SourceHanSerifSC_Regular_20.bin) 自动生成 tools/pinyin/pinyin.txt（需要 pip install pypinyin）",
    )
    ap.add_argument(
        "--pinyin-union-ui-chars",
        action="store_true",
        help="将项目扫描到的中文字符也强行并入 pinyin.txt（即使不在 20px 字库里也会进词库；会输出缺字报告）",
    )
    ap.add_argument(
        "--chars-file",
        default=DEFAULT_CHARS_FILE,
        help=f"字符表路径（默认：{DEFAULT_CHARS_FILE}）",
    )
    ap.add_argument(
        "--user-chars-file",
        default=DEFAULT_USER_CHARS_FILE,
        help=f"用户追加字符表路径（不会被脚本覆盖，默认：{DEFAULT_USER_CHARS_FILE}）",
    )
    ap.add_argument(
        "--user-chars-scope",
        choices=["all", "font20"],
        default="font20",
        help="用户追加字符并入范围：all=所有字号；font20=仅20px（默认，拼音候选用 20px）",
    )
    ap.add_argument(
        "--pinyin-txt",
        default=DEFAULT_PINYIN_TXT,
        help=f"拼音源文件路径（默认：{DEFAULT_PINYIN_TXT}）",
    )
    ap.add_argument(
        "--pinyin-dict",
        default=DEFAULT_PINYIN_DICT,
        help=f"拼音字典路径（可选，默认：{DEFAULT_PINYIN_DICT}）",
    )
    args = ap.parse_args()

    root = project_root()
    out_dir = (root / args.out).resolve()
    out_gui_dir = out_dir / "gui"
    out_fonts_dir = out_dir / "fonts"
    out_pinyin_dir = out_dir / "pinyin"

    out_gui_dir.mkdir(parents=True, exist_ok=True)
    out_fonts_dir.mkdir(parents=True, exist_ok=True)
    out_pinyin_dir.mkdir(parents=True, exist_ok=True)

    print("=" * 60)
    print("【一键生成 SD 卡资源】")
    print("=" * 60)

    # 1. 扫描 UI 中文字符
    print("\n[STEP 1/5] 扫描工程 UI 文案中的中文字符...")
    chinese_chars = collect_ui_chinese_chars(root)

    # 2. 更新字符表
    print("\n[STEP 2/5] 更新字符表文件...")
    pinyin_txt = Path(args.pinyin_txt)
    if not pinyin_txt.is_absolute():
        pinyin_txt = root / pinyin_txt
    pinyin_chars = _read_cn_chars_from_pinyin_txt(pinyin_txt)
    if pinyin_chars:
        print(f"[INFO] 合并拼音词库候选字: {pinyin_txt.as_posix()} (共 {len(pinyin_chars)} 字)")

    merged_chars = chinese_chars | pinyin_chars
    min_cn = FULL_CN_MIN if args.full_cn else max(0, int(args.min_cn))
    chars_file = update_chars_cn_file(
        root,
        merged_chars,
        args.chars_file,
        args.user_chars_file,
        min_cn=min_cn,
        keep_existing=bool(args.keep_existing_chars),
    )

    # 3. 生成字体 bin
    print("\n[STEP 3/5] 生成字体 bin (12/14/16/20/30px)...")
    user_chars_file = Path(args.user_chars_file)
    if not user_chars_file.is_absolute():
        user_chars_file = root / user_chars_file
    generate_font_bins(
        root,
        chars_file,
        out_fonts_dir,
        ui_scanned_chars=chinese_chars,
        user_chars_file=user_chars_file,
        user_chars_scope=args.user_chars_scope,
    )

    # 4. 生成拼音字典
    print("\n[STEP 4/5] 生成拼音字典 (可选)...")
    # pinyin_txt 已在 Step2 解析过，这里直接复用
    pinyin_bin = Path(args.pinyin_dict)
    if not pinyin_bin.is_absolute():
        pinyin_bin = root / pinyin_bin
    if args.pinyin_from_chars:
        generate_pinyin_txt_from_chars(root, chars_file, pinyin_txt)
    if args.pinyin_from_font20:
        font20 = out_fonts_dir / "SourceHanSerifSC_Regular_20.bin"
        extra_file = None
        report_file = None
        if args.pinyin_union_ui_chars:
            extra_file = out_pinyin_dir / "ui_chars_scanned.txt"
            extra_file.write_text("".join(sorted(chinese_chars)) + "\n", encoding="utf-8")
            report_file = out_pinyin_dir / "pinyin_union_report.txt"

        cmd = [
            sys.executable,
            str(root / "tools" / "gen_pinyin_txt_from_binfont.py"),
            "--font",
            str(font20),
            "--out",
            str(pinyin_txt),
        ]
        if extra_file:
            cmd += ["--extra", str(extra_file)]
        if report_file:
            cmd += ["--report", str(report_file)]
        print("[STEP] 从 20px binfont 生成全量 pinyin.txt:", " ".join(cmd))
        subprocess.check_call(cmd, cwd=str(root))
    generate_pinyin_dict(root, pinyin_txt, pinyin_bin)

    # 5. 打包图标
    print("\n[STEP 5/5] 打包图标...")
    images_dir = find_images_dir(root)
    print(f"[INFO] 图标源目录: {images_dir.as_posix()}")
    run_pack_images(root, images_dir, out_gui_dir)

    # 6. 可选：拷贝拼音字典
    if pinyin_bin.exists():
        dst = out_pinyin_dir / "pinyin_dict.bin"
        shutil.copy2(pinyin_bin, dst)
        print(f"[OK] 拼音字典已拷贝: {dst.as_posix()}")
    else:
        print(f"[INFO] 未找到拼音字典，跳过: {pinyin_bin.as_posix()}")

    # 可选：生成 update.flag
    if args.force_update_flag:
        flag_file = out_gui_dir / "update.flag"
        flag_file.write_bytes(b"")
        print(f"[OK] 已创建强制同步标志: {flag_file.as_posix()}")

    write_readme(out_dir)

    print("\n" + "=" * 60)
    print("[DONE] SD 卡资源已生成完毕！")
    print("=" * 60)
    print(f"输出目录: {out_dir.as_posix()}")
    print("\n【下一步】拷贝到 SD 卡根目录：")
    print(f"  - {out_gui_dir.relative_to(root)}   → SD:/gui/")
    print(f"  - {out_fonts_dir.relative_to(root)} → SD:/fonts/")
    print(f"  - {out_pinyin_dir.relative_to(root)} → SD:/pinyin/")
    print("=" * 60)


if __name__ == "__main__":
    main()
