"""解析 lang/po/zh_CN.po,构建翻译词典与反向索引."""

from __future__ import annotations

import re
import sys
from pathlib import Path

from .constants import has_chinese


def parse_po_file(path: Path) -> tuple[dict[str, str], dict[str, list[str]]]:
    """解析 gettext .po 文件.

    Returns:
        en_to_zh: 英文 msgid -> 中文 msgstr (只保留有翻译的)
        zh_to_en: 中文子串 -> 英文 msgid 列表 (反向索引,用于中文搜索)
    """
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as exc:
        print(f"[warn] 无法读取 {path}: {exc}", file=sys.stderr)
        return {}, {}

    en_to_zh: dict[str, str] = {}

    # 解析 msgctxt/msgid/msgstr 三元组 (msgctxt 可选)
    pattern = re.compile(
        r'(?:^msgctxt\s+"((?:[^"\\]|\\.)*)"\s*\n)?'
        r'^msgid\s+"((?:[^"\\]|\\.)*)"\s*\n'
        r'^msgstr\s+"((?:[^"\\]|\\.)*)"\s*\n',
        re.MULTILINE,
    )
    for m in pattern.finditer(text):
        msgid = m.group(2)
        msgstr = m.group(3)
        if not msgid or not msgstr or msgid == msgstr:
            continue
        # 解码 .po 转义
        msgid_d = (msgid.encode().decode("unicode_escape")
                   .encode("latin-1", errors="replace")
                   .decode("utf-8", errors="replace"))
        msgstr_d = (msgstr.encode().decode("unicode_escape")
                    .encode("latin-1", errors="replace")
                    .decode("utf-8", errors="replace"))
        en_to_zh[msgid_d] = msgstr_d

    zh_to_en: dict[str, list[str]] = {}
    for msgid, msgstr in en_to_zh.items():
        if not has_chinese(msgstr):
            continue
        cjk_chars = [c for c in msgstr if has_chinese(c)]
        if not cjk_chars:
            continue
        tokens: set[str] = set()
        tokens.add(msgstr)                # 整段
        for c in cjk_chars:                # 单字
            tokens.add(c)
        for i in range(len(cjk_chars) - 1):    # 2-gram
            tokens.add(cjk_chars[i] + cjk_chars[i + 1])
        for i in range(len(cjk_chars) - 2):    # 3-gram
            tokens.add(cjk_chars[i] + cjk_chars[i + 1] + cjk_chars[i + 2])
        for t in tokens:
            zh_to_en.setdefault(t, []).append(msgid)
    return en_to_zh, zh_to_en