"""命令行入口."""

from __future__ import annotations

import argparse
import sys

from .constants import SKILL_ZH_NAMES
from .database import build_database
from .render import render_furniture, render_item_with_recipes
from .search import render_search_results, render_skill_results


def _setup_stdout_encoding() -> None:
    """Windows 终端默认 GBK, 强制 UTF-8 输出 (• 等符号)."""
    for sn in ("stdout", "stderr"):
        s = getattr(sys, sn, None)
        if s is None:
            continue
        try:
            s.reconfigure(encoding="utf-8")  # type: ignore[attr-defined]
        except (AttributeError, OSError):
            pass


def cmd_search(db, query: str, detail_idx: int | None) -> int:
    from .constants import tokenize_query
    tokens = tokenize_query(query)
    items, furns = db.search(tokens)
    total = len(items) + len(furns)
    if total == 0:
        print(f"未找到匹配 {query!r} 的物品.")
        return 1
    if detail_idx is None:
        print(render_search_results(items, db, furns=furns))
        return 0
    if detail_idx < 0 or detail_idx >= total:
        print(f"--detail 索引超出范围 [0, {total - 1}]")
        return 1
    if detail_idx < len(furns):
        print(render_furniture(furns[detail_idx], db))
        return 0
    it = items[detail_idx - len(furns)]
    recipes = db.get_recipes(it.id)
    print(render_item_with_recipes(it, recipes, db))
    return 0


def cmd_skill(db, skill_query: str, detail_idx: int | None) -> int:
    q = skill_query.lower().strip()
    matched_ids: set[str] = set()
    for en_id, zh in SKILL_ZH_NAMES.items():
        if q == en_id or q in zh or en_id in q:
            matched_ids.add(en_id)
    if q in set(SKILL_ZH_NAMES):
        matched_ids.add(q)
    if not matched_ids:
        print(f"未知技能 {skill_query!r}. 可用: "
              f"{', '.join(sorted(set(SKILL_ZH_NAMES)))}")
        return 1
    results = db.search_by_skill(skill_query)
    if not results:
        print(f"主技能为 {skill_query!r} 的配方: 0 条")
        return 0
    if detail_idx is None:
        print(render_skill_results(skill_query, sorted(matched_ids), results))
        return 0
    if detail_idx < 0 or detail_idx >= len(results):
        print(f"--detail 索引超出范围 [0, {len(results) - 1}]")
        return 1
    it, r = results[detail_idx]
    print(render_item_with_recipes(it, [r], db))
    return 0


def _fmt_min(seconds: int) -> str:
    if seconds < 60:
        return f"{seconds} 秒"
    if seconds < 3600:
        return f"{seconds // 60} 分 {seconds % 60} 秒"
    return f"{seconds // 3600} 时 {(seconds % 3600) // 60} 分"


def cmd_levelup(db, skill_query: str, current_level: int = 0,
                 target_level: int = 1, max_show: int = 12) -> int:
    """经验分析器: 显示当前等级可造的所有配方 (按效率排序)."""
    from .database import Database
    from .models import Item

    # 找技能 id
    matched_ids: set[str] = set()
    q = skill_query.lower().strip()
    for en_id, zh in SKILL_ZH_NAMES.items():
        if q == en_id or q in zh or en_id in q:
            matched_ids.add(en_id)
    if q in set(SKILL_ZH_NAMES):
        matched_ids.add(q)
    if not matched_ids:
        print(f"未知技能 {skill_query!r}. 可用: "
              f"{', '.join(sorted(set(SKILL_ZH_NAMES)))}")
        return 1

    zh_name = next((zh for en, zh in SKILL_ZH_NAMES.items()
                    if en in matched_ids), skill_query)

    # CDDA 规则: cap = difficulty × 1.25; get_skill_level(id) > cap 时停止给 XP
    # 即: difficulty >= 当前等级 的配方都能继续涨经验 (符合用户发现的规则)
    craftable = []
    for result_id, recipes in db.recipes.items():
        for r in recipes:
            if r.skill_used.lower() not in matched_ids:
                continue
            diff = r.difficulty if r.difficulty is not None else 99
            if diff < current_level:  # 低难度配方在你等级超过阈值后失效
                continue
            xp_each = Database.xp_for_recipe(r.time)
            if xp_each <= 0:
                continue
            time_sec = Database.parse_time_to_seconds(r.time)
            eff = xp_each / max(time_sec, 1) * 60  # XP/分钟
            it = db.items.get(result_id) or Item(id=result_id, type="?", name=result_id)
            craftable.append((it, r, xp_each, time_sec, eff))
    craftable.sort(key=lambda x: -x[4])

    print(f"=== 经验分析器: {zh_name} (id={', '.join(sorted(matched_ids))})")
    print(f"  当前等级: Lv.{current_level}")
    if target_level > current_level:
        cur_xp = 0 if current_level <= 0 else Database.XP_CUTOFFS[current_level - 1]
        tgt_xp = Database.XP_CUTOFFS[target_level - 1]
        print(f"  目标等级: Lv.{target_level}")
        print(f"  累计阈值: {tgt_xp} XP (D&D 5e)")
        print(f"  还需 XP:   {tgt_xp - cur_xp}")
    print(f"  提示: cap = difficulty × 1.25, 即 difficulty ≤ 当前等级 都能涨经验")
    print()

    print(f"=== 可制造配方 (Lv.{current_level} 能做的, 按 XP/分钟 降序, 前 {max_show} 条) ===")
    print(f"{'#':>3s}  {'物品':28s}  {'Lv':>3s}  {'耗时':>7s}  {'单次':>5s}  {'效率':>7s}  {'解锁'}")
    for i, (it, r, xp_each, time_sec, eff) in enumerate(craftable[:max_show]):
        time_str = r.time or "?"
        auto = "✓" if r.autolearn else "书"
        print(f"  [{i:2d}]  {it.name[:26]:26s}  Lv.{r.difficulty:<2d}  "
              f"{time_str:>7s}  +{xp_each:<4d}  {eff:6.1f}/分  {auto}")

    if not craftable:
        print(f"  (无 — 该等级范围内没有可用配方)")
    return 0


def cmd_stats(db) -> int:
    total_recipes = sum(len(v) for v in db.recipes.values())
    craftable = sum(1 for iid in db.items if iid in db.recipes)
    print(f"物品总数:         {len(db.items)}")
    print(f"家具/地形:        {len(db.furniture)}")
    print(f"配方条目数:       {total_recipes}")
    print(f"可制作物品:       {craftable}")
    print(f"内置物品中文名:    {len(db.item_zh_names)} 条")
    print(f"内置家具中文名:    {len(db.furn_zh_names)} 条")
    print(f"PO 翻译条目:      {len(db.po_translations)} 条 (zh_CN.po)")
    print(f"PO 反向索引:      {len(db.zh_to_en)} 个 CJK token")
    types: dict[str, int] = {}
    for it in db.items.values():
        types[it.type] = types.get(it.type, 0) + 1
    print("物品按 type 分布:")
    for t, c in sorted(types.items(), key=lambda x: -x[1])[:15]:
        print(f"  {t:20s} {c:6d}")
    return 0


def interactive_loop(db) -> int:
    from .constants import tokenize_query
    print("CDDA 物品配方查询 — 交互模式")
    print("输入物品名片段(空格分隔多个关键词, 全部需匹配, 支持中文).")
    print("命令:")
    print("  q / quit           退出")
    print("  stats              显示统计")
    print("  ids <id>           按 id 直查")
    print("  skill:<名>         按主技能查 (例: skill:survival / skill:生存 / skill:制造)")
    print("  help               显示此帮助")
    while True:
        try:
            raw = input("\nsearch> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return 0
        if not raw:
            continue
        if raw in ("q", "quit", "exit"):
            return 0
        if raw in ("qa", "quitall"):
            return 0
        if raw == "stats":
            cmd_stats(db)
            continue
        if raw in ("h", "help", "?", "帮助"):
            print("  q / quit           退出")
            print("  stats              显示统计")
            print("  ids <id>           按 id 直查")
            print("  skill:<名>         按主技能查")
            print("  levelup:<名>       升级到下一级最快方案 (levelup:生存 / levelup:survival)")
            print("  <查询词>            普通物品名搜索")
            print("  <数字>              查看上一条搜索结果的第 N 个详情")
            continue
        if raw.startswith("ids "):
            iid = raw[4:].strip()
            it = db.items.get(iid)
            if it:
                print(render_item_with_recipes(it, db.get_recipes(iid), db))
                continue
            fn = db.furniture.get(iid)
            if fn:
                print(render_furniture(fn, db))
                continue
            print(f"找不到 id = {iid!r}")
            continue
        if raw.startswith("rebuild"):
            print("请用 --rebuild 参数重启程序")
            continue
        if raw.startswith("levelup:") or raw.startswith("升级:"):
            skill_q = raw.split(":", 1)[1].strip()
            if not skill_q:
                print("用法: levelup:<英文 id 或中文> (例: levelup:survival / levelup:生存)")
                continue
            try:
                cmd_levelup(db, skill_q, current_level=0, target_level=1)
            except SystemExit:
                pass
            continue
        if raw.startswith("skill:") or raw.startswith("技能:"):
            skill_q = raw.split(":", 1)[1].strip()
            if not skill_q:
                print("用法: skill:<英文 id 或中文>")
                continue
            results = db.search_by_skill(skill_q)
            if not results:
                print(f"主技能匹配 {skill_q!r} 的配方: 0 条")
                continue
            matched_ids: set[str] = set()
            q = skill_q.lower()
            for en_id, zh in SKILL_ZH_NAMES.items():
                if q == en_id or q in zh or en_id in q:
                    matched_ids.add(en_id)
            print(render_skill_results(skill_q, sorted(matched_ids), results))
            try:
                sel = input("detail> 输入索引查看详情, 回车继续: ").strip()
            except (EOFError, KeyboardInterrupt):
                print()
                continue
            if not sel:
                continue
            try:
                idx = int(sel)
            except ValueError:
                continue
            if idx < 0 or idx >= len(results):
                print("索引越界")
                continue
            it, r = results[idx]
            print(render_item_with_recipes(it, [r], db))
            continue
        tokens = tokenize_query(raw)
        items, furns = db.search(tokens)
        total = len(items) + len(furns)
        if total == 0:
            print("(无匹配)")
            continue
        print(render_search_results(items, db, furns=furns, max_show=50))
        if total == 1:
            print()
            if furns:
                print(render_furniture(furns[0], db))
            else:
                print(render_item_with_recipes(items[0],
                                              db.get_recipes(items[0].id), db))
            continue
        try:
            sel = input("detail> 输入索引查看详情, 回车继续搜索: ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            continue
        if not sel:
            continue
        try:
            idx = int(sel)
        except ValueError:
            print(f"无效输入: {sel!r}")
            continue
        if idx < 0 or idx >= total:
            print(f"索引越界 [0, {total - 1}]")
            continue
        print()
        if idx < len(furns):
            print(render_furniture(furns[idx], db))
        else:
            it = items[idx - len(furns)]
            print(render_item_with_recipes(it, db.get_recipes(it.id), db))


def build_argparser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="CDDA 物品 / 配方 / 家具 查询 (支持中英文)",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=(
            "示例:\n"
            "  python cdda_item_search.py\n"
            "  python cdda_item_search.py --search 'stone axe'\n"
            "  python cdda_item_search.py --search '木斧'\n"
            "  python cdda_item_search.py --skill survival\n"
            "  python cdda_item_search.py --skill 制造 --detail 0\n"
            "  python cdda_item_search.py --stats\n"
            "  python cdda_item_search.py --rebuild\n"
        ),
    )
    p.add_argument("--search", help="搜索关键词 (空格分隔 AND, 支持中英文)")
    p.add_argument("--skill", help="按主技能筛选 (英文 id 或中文, 例如 survival/生存)")
    p.add_argument("--levelup", metavar="SKILL",
                   help="计算升到目标等级的最快方案 (与 --from / --to 配合)")
    p.add_argument("--from", type=int, default=0, dest="from_lv",
                   help="当前等级 (默认 0)")
    p.add_argument("--to", type=int, default=1, dest="to_lv",
                   help="目标等级 (默认 1)")
    p.add_argument("--detail", type=int, default=None,
                   help="显示第 N 条结果的详情")
    p.add_argument("--rebuild", action="store_true", help="强制重建索引缓存")
    p.add_argument("--stats", action="store_true", help="显示索引统计")
    return p


def main(argv: list[str] | None = None) -> int:
    _setup_stdout_encoding()
    args = build_argparser().parse_args(argv)
    db = build_database(rebuild=args.rebuild)

    if args.stats:
        return cmd_stats(db)
    if args.levelup is not None:
        return cmd_levelup(db, args.levelup,
                           current_level=args.from_lv,
                           target_level=args.to_lv)
    if args.skill is not None:
        return cmd_skill(db, args.skill, args.detail)
    if args.search is not None:
        return cmd_search(db, args.search, args.detail)
    if args.rebuild:
        return 0
    return interactive_loop(db)


if __name__ == "__main__":
    sys.exit(main())