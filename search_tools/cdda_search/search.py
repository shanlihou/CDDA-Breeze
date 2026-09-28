"""搜索结果展示 (CLI 和 Web 共用的格式化函数)."""

from __future__ import annotations

from typing import Sequence

from .models import Furniture, Item, Recipe


def render_search_results(items: Sequence[Item], db,
                         max_show: int = 30,
                         furns: Sequence[Furniture] | None = None) -> str:
    """统一搜索结果展示 (CLI 风格, 给 Web 模板前用)."""
    furns = furns or []
    if not items and not furns:
        return "(无匹配)"
    lines = []
    total = len(items) + len(furns)
    lines.append(f"找到 {total} 个匹配 (物品 {len(items)}, 家具/地形 {len(furns)})")
    n = 0
    for f in furns[:max_show]:
        zh = db.get_zh_name(f.id) if db else None
        zh_part = f"  [{zh}]" if zh else ""
        lines.append(f"  [{n:3d}] # {f.name!r}{zh_part}  ({f.id})  [{f.type}]")
        n += 1
    for it in items[:max(0, max_show - n)]:
        flag = "*" if it.flags else " "
        zh = db.get_zh_name(it.id) if db else None
        zh_part = f"  [{zh}]" if zh else ""
        lines.append(f"  [{n:3d}] {flag} {it.name!r}{zh_part}  ({it.id})  [{it.type}]")
        n += 1
    if total > max_show:
        lines.append(f"  ... 还有 {total - max_show} 个 (用 --detail <索引> 查看)")
    return "\n".join(lines)


def render_skill_results(skill_name: str, matched_ids: list[str],
                          results: list[tuple[Item, Recipe]],
                          max_show: int = 80) -> str:
    """按主技能查到的配方展示."""
    lines = [
        f"=== 制作可获得 '{skill_name}' 经验 的配方 ({len(results)} 条) ===",
        f"  匹配的主技能 id: {', '.join(sorted(matched_ids))}",
        f"  按难度升序, 显示前 {min(max_show, len(results))} 条",
        "",
    ]
    for i, (it, r) in enumerate(results[:max_show]):
        diff = r.difficulty if r.difficulty is not None else 99
        time_s = r.time or "?"
        auto = "✓自动" if r.autolearn else "书"
        lines.append(f"  [{i:3d}] Lv.{diff:>3d} {time_s:>10s} {auto}  "
                     f"{it.name!r}  ({it.id})")
    if len(results) > max_show:
        lines.append(f"  ... 还有 {len(results) - max_show} 条 (用 --detail <索引> 查看)")
    return "\n".join(lines)