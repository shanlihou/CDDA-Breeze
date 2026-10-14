#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
scan_save_items.py — CDDA / CDDA-Breeze 游戏外存档物品扫描工具

不依赖游戏本体，直接读取存档目录里落盘的地图 quad 文件（JSON），
遍历整张存档（所有 overmap、所有 Z 层）的地面物品、家具、载具货舱，
按通配符 / 子串 / 正则匹配物品类型 id（typeid），并输出坐标。

============================================================
存档格式（基于 src/savegame_json.cpp / src/mapbuffer.cpp 推导）
============================================================
存档目录结构（以某个世界存档根目录为例）：
    <world_save>/
        maps/<seg>/<ox>.<oy>.<oz>.map      <- 地图 quad 文件（纯 JSON）
        player/, overmap..., ...

每个 *.map 文件是一个 JSON 数组，元素是 submap（子地图）对象：
    {
      "coordinates": [sm_x, sm_y, sm_z],   // 子地图坐标（1 单位 = 1 子地图）
      "version": N,
      "items": [ [i, j, [ item, item, ... ]], ... ],   // 第 (i,j) 格上的物品堆
      "furniture": [ [i, j, "f_rubble"], ... ],
      "vehicles": [ { "name":"car", "parts":[ { "items":[ item, ... ] } ] } ],
      ...
    }
- 子地图是 12x12 格（SEEX=SEEY=12）。
- 每个 item 对象含 "typeid"（字符串）与嵌套 "contents"（容器内的物品，可递归）。
- 地图方格(ms)坐标 = sm * 12 + (i, j)。

============================================================
用法
============================================================
    # 基本：在存档根目录下扫描所有匹配 *rifle* 的物品
    python3 scan_save_items.py /path/to/save "rifle"

    # 大小写不敏感 + 仅扫地面物品
    python3 scan_save_items.py /path/to/save "*ammo*" -i --ground

    # 正则匹配 + 导出 CSV
    python3 scan_save_items.py /path/to/save "food_.*" --regex --csv out.csv

    # 仅统计每种匹配物品的数量（不打印逐条）
    python3 scan_save_items.py /path/to/save "*" --summary

    # 自带冒烟测试（用内存构造假数据，无需真实存档）
    python3 scan_save_items.py --selftest

参数说明见 --help。
"""

import argparse
import fnmatch
import io
import json
import os
import re
import sys
import tempfile

# 子地图边长（与 src/game_constants.h: SEEX=SEEY=12 一致）
SEEX = 12
SEEY = 12
# 1 overmap 格 = 2x2 子地图 = 24x24 地图方格
OMT_TILES = SEEX * 2


# --------------------------------------------------------------------------- #
# 匹配器
# --------------------------------------------------------------------------- #
def build_matcher(pattern, case_insensitive, use_regex):
    """返回一个 predicate: (string) -> bool。"""
    if use_regex:
        flags = re.IGNORECASE if case_insensitive else 0
        rx = re.compile(pattern, flags)
        return lambda s: rx.search(s) is not None

    has_wild = ("*" in pattern) or ("?" in pattern)
    if has_wild:
        flags = fnmatch.fnmatch if case_insensitive else fnmatch.fnmatchcase
        return lambda s: flags(s, pattern)
    else:
        # 纯子串（默认大小写不敏感，与玩家直觉一致）
        if case_insensitive:
            pat = pattern.lower()
            return lambda s: pat in s.lower()
        return lambda s: pattern in s


# --------------------------------------------------------------------------- #
# 物品遍历
# --------------------------------------------------------------------------- #
def collect_typeids(node, out):
    """递归遍历任意 JSON 节点，收集所有含字符串 typeid 的对象的 typeid。

    CDDA 物品 JSON 的容器结构是：item -> "contents"(list of 口袋)
    -> 口袋 -> "contents"(list of item)。这里不硬编码层级，直接整树遍历，
    凡是 dict 且带字符串 "typeid" 即视为一件物品的类型 id。
    """
    if isinstance(node, dict):
        tid = node.get("typeid")
        if isinstance(tid, str) and tid:
            out.append(tid)
        for v in node.values():
            collect_typeids(v, out)
    elif isinstance(node, list):
        for v in node:
            collect_typeids(v, out)


def iter_item_stack(stack):
    """stack 可能是 list[item] 或单个 item dict。"""
    if isinstance(stack, list):
        for it in stack:
            yield it
    elif isinstance(stack, dict):
        yield stack


def scan_submap(submap_obj, matcher, categories, results, stats):
    """扫描单个 submap 对象，命中则追加到 results。"""
    coords = submap_obj.get("coordinates")
    if not (isinstance(coords, list) and len(coords) == 3):
        return
    sm_x, sm_y, sm_z = (int(coords[0]), int(coords[1]), int(coords[2]))

    # --- 地面物品 ---
    if "ground" in categories:
        items_arr = submap_obj.get("items")
        if isinstance(items_arr, list):
            for entry in items_arr:
                if not (isinstance(entry, list) and len(entry) >= 3):
                    continue
                i, j = int(entry[0]), int(entry[1])
                ms_x = sm_x * SEEX + i
                ms_y = sm_y * SEEY + j
                ms_z = sm_z
                for item_obj in iter_item_stack(entry[2]):
                    tids = []
                    collect_typeids(item_obj, tids)
                    for tid in tids:
                        stats["ground"] += 1
                        if matcher(tid):
                            results.append({
                                "category": "ground",
                                "typeid": tid,
                                "x": ms_x, "y": ms_y, "z": ms_z,
                                "where": "",
                            })

    # --- 家具 ---
    if "furniture" in categories:
        furn_arr = submap_obj.get("furniture")
        if isinstance(furn_arr, list):
            for entry in furn_arr:
                if not (isinstance(entry, list) and len(entry) >= 3):
                    continue
                i, j = int(entry[0]), int(entry[1])
                fid = entry[2]
                if not isinstance(fid, str):
                    continue
                ms_x = sm_x * SEEX + i
                ms_y = sm_y * SEEY + j
                ms_z = sm_z
                stats["furniture"] += 1
                if matcher(fid):
                    results.append({
                        "category": "furniture",
                        "typeid": fid,
                        "x": ms_x, "y": ms_y, "z": ms_z,
                        "where": "",
                    })

    # --- 载具货舱 ---
    if "vehicle" in categories:
        veh_arr = submap_obj.get("vehicles")
        if isinstance(veh_arr, list):
            for veh in veh_arr:
                if not isinstance(veh, dict):
                    continue
                vname = veh.get("name", "")
                if not isinstance(vname, str):
                    vname = ""
                # 载具坐标落在子地图级别（精确货舱格需解析 part 安装点，这里给子地图原点）
                ms_x = sm_x * SEEX
                ms_y = sm_y * SEEY
                ms_z = sm_z
                parts = veh.get("parts")
                if not isinstance(parts, list):
                    continue
                for part in parts:
                    if not isinstance(part, dict):
                        continue
                    pitems = part.get("items")
                    for item_obj in iter_item_stack(pitems):
                        tids = []
                        collect_typeids(item_obj, tids)
                        for tid in tids:
                            stats["vehicle"] += 1
                            if matcher(tid):
                                results.append({
                                    "category": "vehicle",
                                    "typeid": tid,
                                    "x": ms_x, "y": ms_y, "z": ms_z,
                                    "where": vname,
                                })


# --------------------------------------------------------------------------- #
# 存档遍历
# --------------------------------------------------------------------------- #
def find_map_files(save_dir):
    """返回存档下所有 *.map 文件的路径列表（递归，顺序稳定）。"""
    maps_root = os.path.join(save_dir, "maps")
    if not os.path.isdir(maps_root):
        # 容错：有些布局 maps 不一定存在，退化为在 save_dir 下全盘找 *.map
        maps_root = save_dir
    files = []
    for root, _dirs, names in os.walk(maps_root):
        for n in names:
            if n.endswith(".map"):
                files.append(os.path.join(root, n))
    files.sort()
    return files


def scan_save(save_dir, matcher, categories, max_results=None,
              show_progress=True):
    """扫描整个存档，返回 (results, stats, scanned_files)。"""
    map_files = find_map_files(save_dir)
    if not map_files:
        raise FileNotFoundError(
            "在 %s 下未找到任何 *.map 文件（请确认传入的是『世界存档根目录』，"
            "其下应包含 maps/ 子目录）" % save_dir)

    results = []
    stats = {"ground": 0, "furniture": 0, "vehicle": 0, "files": 0, "submaps": 0}
    hit_limit = max_results is not None and max_results > 0

    for idx, path in enumerate(map_files, 1):
        try:
            with io.open(path, "r", encoding="utf-8") as fh:
                data = json.load(fh)
        except (ValueError, OSError) as e:
            if show_progress:
                sys.stderr.write("  [跳过] 解析失败 %s: %s\n" % (path, e))
            continue
        stats["files"] += 1
        if isinstance(data, list):
            for submap_obj in data:
                if isinstance(submap_obj, dict):
                    stats["submaps"] += 1
                    scan_submap(submap_obj, matcher, categories, results, stats)
        if hit_limit and len(results) >= max_results:
            if show_progress:
                sys.stderr.write("  [达到最大结果数 %d，停止扫描]\n" % max_results)
            break
        if show_progress and (idx % 200 == 0 or idx == len(map_files)):
            sys.stderr.write("  进度: %d/%d 文件, 命中 %d 条\r"
                             % (idx, len(map_files), len(results)))
            sys.stderr.flush()

    if show_progress:
        sys.stderr.write("\n")
    return results, stats, map_files


# --------------------------------------------------------------------------- #
# 输出
# --------------------------------------------------------------------------- #
def omt_coords(x, y, z):
    return (x // OMT_TILES, y // OMT_TILES, z)


def print_results(results, with_omt=True):
    if not results:
        print("未找到匹配的物品。")
        return
    # 表头与行构建保持对称：基础列固定，omt 列按需附加
    base_header = ["分类", "typeid", "X(ms)", "Y(ms)", "Z(ms)", "载具"]
    base_row = lambda r: [r["category"], r["typeid"], r["x"], r["y"], r["z"], r["where"]]
    if with_omt:
        header = base_header[:5] + ["X(omt)", "Y(omt)", "Z(omt)"] + [base_header[5]]
        def make_row(r):
            ox, oy, oz = omt_coords(r["x"], r["y"], r["z"])
            return [r["category"], r["typeid"], r["x"], r["y"], r["z"], ox, oy, oz, r["where"]]
    else:
        header = base_header
        make_row = base_row
    rows = [make_row(r) for r in results]
    # 简单对齐打印
    widths = [len(h) for h in header]
    for row in rows:
        for c, cell in enumerate(row):
            widths[c] = max(widths[c], len(str(cell)))
    fmt = "  ".join("{:<%d}" % w for w in widths)
    print(fmt.format(*header))
    print(fmt.format(*["-" * w for w in widths]))
    for row in rows:
        print(fmt.format(*[str(c) for c in row]))


def print_summary(results):
    from collections import Counter
    cnt = Counter((r["category"], r["typeid"]) for r in results)
    print("匹配汇总（分类 / typeid / 数量）：")
    for (cat, tid), n in sorted(cnt.items(), key=lambda kv: (-kv[1], kv[0])):
        print("  %-10s %-30s %d" % (cat, tid, n))


def write_csv(results, path, with_omt=True):
    import csv
    with io.open(path, "w", encoding="utf-8", newline="") as fh:
        w = csv.writer(fh)
        head = ["category", "typeid", "x_ms", "y_ms", "z_ms", "vehicle"]
        if with_omt:
            head += ["x_omt", "y_omt", "z_omt"]
        w.writerow(head)
        for r in results:
            row = [r["category"], r["typeid"], r["x"], r["y"], r["z"], r["where"]]
            if with_omt:
                ox, oy, oz = omt_coords(r["x"], r["y"], r["z"])
                row += [ox, oy, oz]
            w.writerow(row)
    print("已写出 CSV: %s (%d 行)" % (path, len(results)))


# --------------------------------------------------------------------------- #
# 自带冒烟测试（无需真实存档）
# --------------------------------------------------------------------------- #
def selftest():
    sample = [
        {
            "coordinates": [100, 200, 0],
            "version": 34,
            "items": [
                [3, 4, [
                    {"typeid": "rifle_hunting", "charges": 0,
                     "contents": [{"typeid": "ammo_223"}, {"typeid": "ammo_223"}]}
                ]],
                [0, 0, [{"typeid": "rock"}]]
            ],
            "furniture": [[5, 5, "f_rubble"]],
            "vehicles": [
                {"name": "my_car",
                 "parts": [{"items": [{"typeid": "vehicle_battery_car"}]}]}
            ]
        },
        {
            "coordinates": [-50, -10, -1],
            "items": [[1, 1, [{"typeid": "food_apple"}]]]
        }
    ]
    # 用 * 匹配全部，验证递归收集是否生效（容器内的 ammo_223 也应被收集）
    matcher = build_matcher("*", case_insensitive=True, use_regex=False)
    results = []
    stats = {"ground": 0, "furniture": 0, "vehicle": 0, "files": 0, "submaps": 0}
    for sm in sample:
        scan_submap(sm, matcher, {"ground", "furniture", "vehicle"}, results, stats)

    # 断言
    assert any(r["typeid"] == "rifle_hunting" for r in results), "应收集 rifle_hunting"
    assert any(r["typeid"] == "ammo_223" for r in results), "应递归收集容器内 ammo_223"
    assert any(r["typeid"] == "food_apple" for r in results), "应收集另一子地图的 apple"
    # 验证 *rifle* 子串匹配（大小写不敏感）只在地面命中步枪
    matcher_r = build_matcher("*rifle*", case_insensitive=True, use_regex=False)
    res_r = []
    scan_submap(sample[0], matcher_r, {"ground", "vehicle", "furniture"}, res_r, stats)
    assert any(r["typeid"] == "rifle_hunting" for r in res_r), "子串 rifle 应命中"
    assert not any(r["typeid"] == "ammo_223" for r in res_r), "ammo 不应被 rifle 命中"
    # 坐标校验：sm=(100,200,0), tile=(3,4) -> ms=(100*12+3, 200*12+4, 0)
    rifle = next(r for r in results if r["typeid"] == "rifle_hunting")
    assert rifle["x"] == 1203 and rifle["y"] == 2404 and rifle["z"] == 0, rifle
    assert rifle["category"] == "ground"

    # 载具匹配校验
    matcher2 = build_matcher("vehicle_battery", case_insensitive=False, use_regex=False)
    res2 = []
    scan_submap(sample[0], matcher2, {"vehicle"}, res2, stats)
    assert any(r["typeid"] == "vehicle_battery_car" and r["where"] == "my_car"
               for r in res2), "应匹配载具货舱"

    print("SELFTEST PASSED: rifle_hunting=%s ammo_223=%s apple=%s coord=%s vehicle=%s"
          % (any(r["typeid"] == "rifle_hunting" for r in results),
             any(r["typeid"] == "ammo_223" for r in results),
             any(r["typeid"] == "food_apple" for r in results),
             (rifle["x"], rifle["y"], rifle["z"]),
             any(r["typeid"] == "vehicle_battery_car" for r in res2)))


# --------------------------------------------------------------------------- #
# CLI
# --------------------------------------------------------------------------- #
def main(argv=None):
    parser = argparse.ArgumentParser(
        description="游戏外扫描 CDDA 存档搜索物品（地面/家具/载具货舱）。",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__)
    parser.add_argument("save_dir", nargs="?", help="世界存档根目录（含 maps/ 子目录）")
    parser.add_argument("pattern", nargs="?", default="*",
                        help="匹配模式，默认 *（全部）。支持 * ? 通配；无通配时按子串匹配")
    parser.add_argument("-i", "--ignore-case", action="store_true",
                        help="大小写不敏感")
    parser.add_argument("--regex", action="store_true", help="把 pattern 当作正则表达式")
    parser.add_argument("--csv", metavar="FILE", help="将结果导出为 CSV")
    parser.add_argument("--max-results", type=int, default=0,
                        help="最多收集多少条命中（0=不限制）")
    parser.add_argument("--summary", action="store_true",
                        help="只输出每种匹配物品的汇总计数")
    parser.add_argument("--ground", action="store_true", help="仅扫地面物品")
    parser.add_argument("--vehicle", action="store_true", help="仅扫载具货舱")
    parser.add_argument("--furniture", action="store_true", help="仅扫家具")
    parser.add_argument("--no-omt", action="store_true",
                        help="输出中不附加 overmap 坐标")
    parser.add_argument("--no-progress", action="store_true", help="关闭进度输出")
    parser.add_argument("--selftest", action="store_true",
                        help="运行内置冒烟测试（无需真实存档）")
    args = parser.parse_args(argv)

    if args.selftest:
        selftest()
        return 0

    if not args.save_dir:
        parser.print_help()
        sys.stderr.write("\n错误：请提供存档目录路径。\n")
        return 2

    categories = {"ground", "vehicle", "furniture"}
    if args.ground or args.vehicle or args.furniture:
        categories = set()
        if args.ground:
            categories.add("ground")
        if args.vehicle:
            categories.add("vehicle")
        if args.furniture:
            categories.add("furniture")

    matcher = build_matcher(args.pattern, args.ignore_case, args.regex)

    try:
        results, stats, map_files = scan_save(
            args.save_dir, matcher, categories,
            max_results=args.max_results if args.max_results > 0 else None,
            show_progress=not args.no_progress)
    except FileNotFoundError as e:
        sys.stderr.write("错误: %s\n" % e)
        return 1

    print("扫描完成：%d 个 map 文件, %d 个子地图；检查 地面=%d 家具=%d 载具=%d"
          % (stats["files"], stats["submaps"],
             stats["ground"], stats["furniture"], stats["vehicle"]))
    print("匹配命中：%d 条（模式=%r）" % (len(results), args.pattern))
    print("-" * 60)

    if args.summary:
        print_summary(results)
    else:
        print_results(results, with_omt=not args.no_omt)

    if args.csv:
        write_csv(results, args.csv, with_omt=not args.no_omt)

    return 0


if __name__ == "__main__":
    sys.exit(main())
