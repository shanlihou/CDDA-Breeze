#!/usr/bin/env python3
"""CDDA 物品 / 配方查询工具 — CLI 入口 (包装 cdda_search 包).

Usage:
    python cdda_item_search.py                              # 交互模式
    python cdda_item_search.py --search "stone axe"
    python cdda_item_search.py --search "木斧"
    python cdda_item_search.py --skill survival
    python cdda_item_search.py --skill 制造 --detail 0
    python cdda_item_search.py --stats
    python cdda_item_search.py --rebuild
"""

import sys

from cdda_search.cli import main

if __name__ == "__main__":
    sys.exit(main())