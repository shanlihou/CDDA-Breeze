"""Flask Web UI — 3-tab design.

Tabs:
  Tab 1 /items        — 物品搜索 (中英文, 物品 + 家具)
  Tab 2 /levelup      — 升级规划 (技能 + 当前/目标级 → 每配方 XP 贡献 + 进度条)
  Tab 3 /browse       — 技能/熟练度浏览 (按 prof_* 或 skill_used 列出所有配方)
"""

from __future__ import annotations

from flask import (
    Blueprint, Flask, abort, jsonify, redirect, request,
    render_template, url_for,
)

from .constants import SKILL_ZH_NAMES, tokenize_query
from .database import Database, build_database
from .models import Furniture, Item, Recipe
from .render import (
    render_furniture, render_item, render_item_with_recipes,
    render_recipe,
)
from .search import render_search_results, render_skill_results

bp = Blueprint("cdda_search", __name__,
               template_folder="templates",
               static_folder="static")

_db_cache: Database | None = None


def _get_db() -> Database:
    global _db_cache
    if _db_cache is None:
        _db_cache = build_database()
    return _db_cache


def _stats(db: Database) -> dict:
    return {
        "items": len(db.items),
        "furniture": len(db.furniture),
        "recipes": sum(len(v) for v in db.recipes.values()),
        "po_translations": len(db.po_translations),
        "skill_count": len(SKILL_ZH_NAMES),
        "item_zh_count": len(db.item_zh_names),
        "furn_zh_count": len(db.furn_zh_names),
    }


def _render_index(active_tab: str):
    """渲染首页 — 显示三个 tab 入口卡片 + 统计."""
    db = _get_db()
    skills = sorted(SKILL_ZH_NAMES.items(), key=lambda x: x[1])
    return render_template(
        "index.html",
        active_tab=active_tab,
        skills=skills,
        stats=_stats(db),
    )


# ── 首页 (重定向到 items) ──

@bp.route("/")
def index():
    return redirect(url_for("cdda_search.items"))


# ════════════════════════════════════════════════════════════════
# Tab 1: 物品搜索
# ════════════════════════════════════════════════════════════════

@bp.route("/items")
def items():
    db = _get_db()
    q = (request.args.get("q") or "").strip()
    if q:
        tokens = tokenize_query(q)
        items_, furns = db.search(tokens)
        total = len(items_) + len(furns)
        return render_template(
            "items.html",
            active_tab="items", q=q,
            items=items_, furns=furns, total=total,
            item_zh_names=db.item_zh_names,
            stats=_stats(db),
        )
    return _render_index("items")


# 旧路由 /search 重定向到 /items
@bp.route("/search")
def search_alias():
    return redirect(url_for("cdda_search.items",
                            q=request.args.get("q", "")))


# ════════════════════════════════════════════════════════════════
# Tab 2: 升级规划 (skill + from/to → 每个配方的 XP 贡献 + 进度条)
# ════════════════════════════════════════════════════════════════

@bp.route("/levelup", methods=["GET"])
def levelup():
    db = _get_db()
    skills = sorted(SKILL_ZH_NAMES.items(), key=lambda x: x[1])
    stats = _stats(db)

    # skill 兼容 name (旧链接)
    name = (request.args.get("skill") or
            request.args.get("name") or "").strip()

    if not name:
        return render_template(
            "levelup.html",
            active_tab="levelup",
            name="", matched_ids=[], crafts=[], plan=[],
            progression=[], not_found=False, no_skill=True,
            from_lv=0, to_lv=1,
            xp_cutoff=Database.XP_CUTOFFS,
            skills=skills, stats=stats,
        )

    try:
        from_lv = max(0, int(request.args.get("from", 0)))
        to_lv = max(1, min(20, int(request.args.get("to", 1))))
    except ValueError:
        from_lv, to_lv = 0, 1
    if to_lv <= from_lv:
        to_lv = from_lv + 1

    result = db.fastest_to_level(name, from_lv, to_lv)
    matched = result.get("matched_ids", [])
    if not matched:
        return render_template(
            "levelup.html",
            active_tab="levelup",
            name=name, matched_ids=[], crafts=[], plan=[],
            progression=[], not_found=True, no_skill=False,
            from_lv=from_lv, to_lv=to_lv,
            xp_cutoff=Database.XP_CUTOFFS,
            skills=skills, stats=stats,
        )

    # 逐级规划: from → from+1 → ... → to
    progression = []
    for lvl in range(from_lv, to_lv):
        sub = db.fastest_to_level(name, lvl, lvl + 1)
        progression.append({
            "from": lvl,
            "to": lvl + 1,
            "xp_cutoff": Database.XP_CUTOFFS[lvl],
            "xp_needed": sub["xp_needed"],
            "crafts": sub["crafts"][:5],
        })

    return render_template(
        "levelup.html",
        active_tab="levelup",
        name=name, matched_ids=matched,
        crafts=result["crafts"], plan=result["plan"],
        progression=progression,
        not_found=False, no_skill=False,
        from_lv=from_lv, to_lv=to_lv,
        current_level=result["current_level"],
        target_level=result["target_level"],
        xp_needed=result["xp_needed"],
        xp_cutoff_target=result["xp_cutoff_target"],
        xp_cutoff=Database.XP_CUTOFFS,
        skills=skills, stats=stats,
    )


# ════════════════════════════════════════════════════════════════
# Tab 3: 技能/熟练度浏览 (列出所有匹配配方, 按难度分组)
# ════════════════════════════════════════════════════════════════

@bp.route("/browse", methods=["GET"])
def browse():
    db = _get_db()
    btype = (request.args.get("type") or "skill").strip().lower()
    q = (request.args.get("q") or "").strip()
    skills = sorted(SKILL_ZH_NAMES.items(), key=lambda x: x[1])
    stats = _stats(db)
    prof_names = sorted(db._all_proficiencies())

    if not q:
        return render_template(
            "browse.html",
            active_tab="browse", btype=btype, q="",
            matched_ids=[], groups=[], total=0,
            no_query=True,
            skills=skills, prof_names=prof_names, stats=stats,
        )

    if btype == "proficiency":
        matched, results = db.search_by_proficiency(q)
    else:
        matched, results = db.search_by_skill_full(q)
    title_zh = matched[0] if matched else q

    # 按难度分组 + 预计算 XP
    by_diff: dict[int, list] = {}
    for it, r in results:
        d = r.difficulty if r.difficulty is not None else 0
        xp_each = Database.xp_for_recipe(r.time)
        by_diff.setdefault(d, []).append((it, r, xp_each))

    groups = [{
        "difficulty": d,
        "count": len(items_list),
        "items": items_list,
    } for d, items_list in sorted(by_diff.items())]

    return render_template(
        "browse.html",
        active_tab="browse", btype=btype, q=q,
        matched_ids=matched, groups=groups, total=len(results),
        no_query=False, title_zh=title_zh,
        skills=skills, prof_names=prof_names, stats=stats,
    )


# 旧路由 /skill 重定向到 /browse?type=skill
@bp.route("/skill")
def skill_alias():
    args = {k: v for k, v in request.args.items()}
    args["type"] = "skill"
    return redirect(url_for("cdda_search.browse", **args))


# ════════════════════════════════════════════════════════════════
# 详情路由 (物品 / 家具 / 配方)
# ════════════════════════════════════════════════════════════════

@bp.route("/item/<item_id>")
def item_detail(item_id: str):
    db = _get_db()
    it = db.items.get(item_id)
    if not it:
        abort(404)
    recipes = db.get_recipes(item_id)
    return render_template(
        "detail.html",
        active_tab="items",  # 详情高亮 Tab 1
        kind="item", obj=it, recipes=recipes,
        zh_name=db.get_zh_name(item_id),
        text=render_item_with_recipes(it, recipes, db),
        stats=_stats(db),
    )


@bp.route("/furniture/<item_id>")
def furniture_detail(item_id: str):
    db = _get_db()
    fn = db.furniture.get(item_id)
    if not fn:
        abort(404)
    return render_template(
        "detail.html",
        active_tab="items",
        kind="furniture", obj=fn, recipes=[],
        zh_name=db.get_zh_name(item_id),
        text=render_furniture(fn, db),
        stats=_stats(db),
    )


@bp.route("/recipe/<item_id>/<int:idx>")
def recipe_detail(item_id: str, idx: int):
    db = _get_db()
    recipes = db.get_recipes(item_id)
    if idx < 0 or idx >= len(recipes):
        abort(404)
    return render_template(
        "detail.html",
        active_tab="levelup",  # 配方详情高亮 Tab 2
        kind="recipe", obj=recipes[idx], recipes=[],
        zh_name=db.get_zh_name(item_id),
        text=render_recipe(recipes[idx]),
        item_id=item_id, idx=idx,
        stats=_stats(db),
    )


# ════════════════════════════════════════════════════════════════
# JSON API
# ════════════════════════════════════════════════════════════════

@bp.route("/api/search")
def api_search():
    db = _get_db()
    q = (request.args.get("q") or "").strip()
    if not q:
        return jsonify({"error": "missing q"}), 400
    tokens = tokenize_query(q)
    items, furns = db.search(tokens)
    return jsonify({
        "items": [it.id for it in items[:50]],
        "furniture": [fn.id for fn in furns[:50]],
    })


# ════════════════════════════════════════════════════════════════
# 错误页
# ════════════════════════════════════════════════════════════════

def _err_page(code: int, message: str):
    db = _get_db()
    return render_template("error.html",
                           code=code, message=message,
                           stats=_stats(db)), code


def create_app() -> Flask:
    app = Flask(__name__)
    app.register_blueprint(bp)
    app.register_error_handler(404, lambda e: _err_page(404, "页面未找到"))
    app.register_error_handler(500, lambda e: _err_page(500, "服务器内部错误"))
    return app