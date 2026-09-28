#!/usr/bin/env python3
"""CDDA 物品查询 — Web UI 启动器.

Usage:
    python run_web.py             # 默认 127.0.0.1:5000
    python run_web.py 0.0.0.0     # 监听所有网卡
    python run_web.py --port 8080
"""

from __future__ import annotations

import argparse
import sys

try:
    from flask import Flask
except ImportError:
    print("错误: 需要 Flask. 安装: pip install flask",
          file=sys.stderr)
    sys.exit(1)

from cdda_search.web import create_app


def main() -> int:
    p = argparse.ArgumentParser(description="CDDA 物品查询 Web UI")
    p.add_argument("--host", default="127.0.0.1",
                   help="监听地址 (默认 127.0.0.1)")
    p.add_argument("--port", type=int, default=5000,
                   help="监听端口 (默认 5000)")
    p.add_argument("--debug", action="store_true",
                   help="启用 Flask debug 模式")
    p.add_argument("--rebuild", action="store_true",
                   help="启动前强制重建索引")
    args = p.parse_args()

    if args.rebuild:
        from cdda_search.database import build_database
        build_database(rebuild=True)

    app = create_app()
    print(f"CDDA 物品查询 — Web UI")
    print(f"  访问 http://{args.host}:{args.port}/")
    print(f"  Ctrl+C 退出")
    app.run(host=args.host, port=args.port, debug=args.debug)
    return 0


if __name__ == "__main__":
    sys.exit(main())