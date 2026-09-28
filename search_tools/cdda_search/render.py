"""详细渲染: 物品 / 家具 / 配方的文本格式."""

from __future__ import annotations

from .models import Furniture, Item, Recipe


# ── 配方内字段格式化器 ──

def _format_components(components) -> str:
    if not components:
        return "  (无)"
    lines = []
    for group in components:
        if not group:
            continue
        parts = []
        for name, qty, flag in group:
            tag = " (任选)" if flag == "LIST" else ""
            parts.append(f"{qty}× {name}{tag}")
        lines.append("  或 ".join(parts))
    return "\n".join("  • " + ln for ln in lines)


def _format_using(using) -> str:
    if not using:
        return "  (无)"
    return "\n".join(f"  • 使用工具集 {kit} (×{qty})" for kit, qty in using)


def _format_qualities(qualities) -> str:
    if not qualities:
        return "  (无)"
    return "\n".join(f"  • {qid} Lv.{lv}" for qid, lv in qualities)


def _format_skills_required(skills_req) -> str:
    if not skills_req:
        return "  (无)"
    return "\n".join(f"  • {sid} Lv.{lv}" for sid, lv in skills_req)


def _format_proficiencies(profs) -> str:
    if not profs:
        return "  (无)"
    lines = []
    for p in profs:
        pid = p["proficiency"]
        req = "必需" if p["required"] else "可选"
        tm = p["time_multiplier"]
        if tm != 1.0:
            lines.append(f"  • {pid}  ({req}, 时间倍率 ×{tm})")
        else:
            lines.append(f"  • {pid}  ({req})")
    return "\n".join(lines)


def _format_book_learn(books) -> str:
    if not books:
        return "  (无)"
    return "\n".join(f"  • {book} 第 {ch} 章" for book, ch in books)


def _format_autolearn(autolearn) -> str:
    if autolearn is True:
        return "  ✓ 技能达阈值时自动学会"
    if isinstance(autolearn, list) and autolearn:
        parts = [f"{sid}={lv}" for sid, lv in autolearn]
        return "  ✓ 自动学会: " + ", ".join(parts)
    return "  ✗ 非自动(需从书本学习)"


# ── 配方 ──

def render_recipe(r: Recipe) -> str:
    lines = [
        f"=== 配方 (result = {r.result!r}) ===",
        f"类型:           {r.type}",
        f"活动等级:        {r.activity_level or '未指定'}",
        f"难度:           {r.difficulty if r.difficulty is not None else '未指定'}",
        f"耗时:           {r.time or '未指定'}",
        "",
        "── 主要技能 (获得经验) ──",
        f"  skill_used: {r.skill_used or '(无)'}",
        "",
        "── 所需技能等级 (门槛, 不直接获得经验) ──",
        _format_skills_required(r.skills_required),
        "",
        "── 熟练度要求 ──",
        _format_proficiencies(r.proficiencies),
        "",
        "── 解锁条件 ──",
        _format_autolearn(r.autolearn),
        "书本学习:",
        _format_book_learn(r.book_learn),
        "",
        "── 制作材料 (components) ──",
        _format_components(r.components),
        "",
        "── 工具集 (using) ──",
        _format_using(r.using),
        "",
        "── 工具质量要求 (qualities) ──",
        _format_qualities(r.qualities),
    ]
    if r.byproducts:
        lines += ["", "── 副产品 ──", _format_components(r.byproducts)]
    if r.reversible:
        lines += ["", "── 备注 ──", "  reversible: 可反向拆解"]
    if r.decomp_learn:
        lines += ["", f"  拆解可学技能数: {r.decomp_learn}"]
    if r.source_file:
        lines += ["", f"来源: {r.source_file}"]
    return "\n".join(lines)


# ── 物品 ──

def render_item(it: Item, db=None) -> str:
    zh = db.get_zh_name(it.id) if db else None
    title_zh = f"  [{zh}]" if zh else ""
    lines = [
        f"=== {it.name}{title_zh}  (id = {it.id!r}) ===",
        f"类型:           {it.type}",
        f"分类:           {it.category} / {it.subcategory}",
        f"重量:           {it.weight}",
        f"体积:           {it.volume}",
        f"材料:           {', '.join(it.material) if it.material else '(未指定)'}",
        f"价格:           {it.price}",
        f"末日价:         {it.price_postapoc}",
        "",
        "描述:",
        f"  {it.description}" if it.description else "  (无)",
        "",
        f"关键 flags:    {', '.join(it.flags[:15]) if it.flags else '(无)'}",
        f"工具质量:       {it.qualities if it.qualities else '(无)'}",
        f"弹药类型:       {', '.join(it.ammo) if it.ammo else '(无)'}",
    ]
    if it.source_file:
        lines += ["", f"来源: {it.source_file}"]
    return "\n".join(lines)


def render_item_with_recipes(it: Item, recipes: list[Recipe], db=None) -> str:
    out = [render_item(it, db)]
    if not recipes:
        out += ["", "── 相关配方 ──",
                "  (无 — 该物品无法由玩家制作, 通常为地图生成或怪物掉落)"]
    else:
        out += ["", f"── 找到 {len(recipes)} 条相关配方 ──"]
        for i, r in enumerate(recipes):
            out.append("")
            out.append("┌─ 配方 #%d ─────────────────────────────────────────" % i)
            out.append(render_recipe(r))
            out.append("└─────────────────────────────────────────────")
    return "\n".join(out)


# ── 家具 / 地形 / 电器 ──

def render_furniture(fn: Furniture, db=None) -> str:
    zh = db.get_zh_name(fn.id) if db else None
    title_zh = f"  [{zh}]" if zh else ""
    lines = [
        f"=== {fn.name}{title_zh}  (id = {fn.id!r}) ===",
        f"类型:           {fn.type}  (subtype={fn.subtype})",
        f"移动消耗:       {fn.move_cost if fn.move_cost is not None else '(无)'}",
        f"覆盖:           {fn.coverage if fn.coverage is not None else '(无)'}",
        f"交互动作:        {fn.examine_action or '(无)'}",
        "",
        f"关键 flags:    {', '.join(fn.flags[:15]) if fn.flags else '(无)'}",
    ]
    if fn.description:
        lines += ["", "描述:", f"  {fn.description}"]
    if fn.deconstruct_items:
        items_str = ", ".join(
            f"{x.get('item', '?')}×{x.get('count', '?')}"
            if isinstance(x, dict) else str(x)
            for x in fn.deconstruct_items
        )
        lines += ["", f"拆解可获得: {items_str}"]
    if fn.bash_items:
        items_str = ", ".join(
            f"{x.get('item', '?')}×{x.get('count', '?')}"
            if isinstance(x, dict) else str(x)
            for x in fn.bash_items
        )
        lines += ["", f"破坏可获得: {items_str}"]
    if fn.source_file:
        lines += ["", f"来源: {fn.source_file}"]
    return "\n".join(lines)