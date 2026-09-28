"""Database 类 + 索引构建 + 磁盘缓存管理."""

from __future__ import annotations

import json
import re
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable

from .constants import (
    CACHE_FILE, CACHE_FILE_ZH, FURNITURE_DIR, ITEM_ZH_NAMES, FURN_ZH_NAMES,
    ITEMS_DIR, PO_FILE, RECIPES_DIR, has_chinese,
)
from .models import (
    Furniture, Item, Recipe, _recipe_to_cache_dict,
    furniture_from_json, item_from_json, recipe_from_json,
)
from .po import parse_po_file


def iter_json_files(root: Path) -> Iterable[Path]:
    if not root.exists():
        return
    yield from sorted(root.rglob("*.json"))


def load_json_safe(path: Path) -> list:
    """CDDA JSON 兼容: 剥离整行 // 注释,保留 "//": "..." 键."""
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        print(f"[warn] 读取失败 {path}: {exc}", file=sys.stderr)
        return []
    text = re.sub(r"^\s*//[^\n]*\n?", "", text, flags=re.MULTILINE)
    try:
        data = json.loads(text)
    except json.JSONDecodeError as exc:
        print(f"[warn] JSON 解析失败 {path}:{exc.lineno}:{exc.colno} {exc.msg}",
              file=sys.stderr)
        return []
    if isinstance(data, dict):
        return [data]
    if isinstance(data, list):
        return data
    return []


@dataclass(slots=True)
class Database:
    items: dict[str, Item] = field(default_factory=dict)
    furniture: dict[str, Furniture] = field(default_factory=dict)
    recipes: dict[str, list[Recipe]] = field(default_factory=dict)
    po_translations: dict[str, str] = field(default_factory=dict)
    zh_to_en: dict[str, list[str]] = field(default_factory=dict)
    item_zh_names: dict[str, str] = field(default_factory=dict)
    furn_zh_names: dict[str, str] = field(default_factory=dict)

    # ── 搜索 ──

    def search(self, keywords: list[str]) -> tuple[list[Item], list[Furniture]]:
        """统一搜索: 物品 + 家具/地形, 支持中英文 AND 匹配."""
        en_kws = [k.lower() for k in keywords if not has_chinese(k) and k]
        zh_kws = [k for k in keywords if has_chinese(k)]

        zh_candidate_ids: set[str] | None = None
        for kw in zh_kws:
            ids_this: set[str] = set()
            for iid, zhname in self.item_zh_names.items():
                if kw in zhname:
                    ids_this.add(iid)
            for fid, zhname in self.furn_zh_names.items():
                if kw in zhname:
                    ids_this.add(fid)
            for en_msgid in self.zh_to_en.get(kw, []):
                el = en_msgid.lower()
                for it in self.items.values():
                    if el in (it.name + " " + it.id).lower():
                        ids_this.add(it.id)
                for fn in self.furniture.values():
                    if el in (fn.name + " " + fn.id).lower():
                        ids_this.add(fn.id)
            zh_candidate_ids = set(ids_this) if zh_candidate_ids is None else (
                zh_candidate_ids & ids_this)

        items_out: list[Item] = []
        for it in self.items.values():
            text = (it.name + " " + it.id).lower()
            if en_kws and not all(k in text for k in en_kws):
                continue
            if zh_candidate_ids is not None and it.id not in zh_candidate_ids:
                continue
            if en_kws or zh_candidate_ids is not None:
                items_out.append(it)
        items_out.sort(key=lambda x: (len(x.name), x.name))

        furn_out: list[Furniture] = []
        for fn in self.furniture.values():
            text = (fn.name + " " + fn.id).lower()
            if en_kws and not all(k in text for k in en_kws):
                continue
            if zh_candidate_ids is not None and fn.id not in zh_candidate_ids:
                continue
            if en_kws or zh_candidate_ids is not None:
                furn_out.append(fn)
        furn_out.sort(key=lambda x: (len(x.name), x.name))

        return items_out, furn_out

    def search_items(self, keywords: list[str]) -> list[Item]:
        items, _ = self.search(keywords)
        return items

    def search_by_skill(self, skill_query: str) -> list[tuple[Item, Recipe]]:
        """按主技能 skill_used 搜索, 返回 (item, recipe) 列表, 按 difficulty 升序."""
        from .constants import SKILL_ZH_NAMES
        q = skill_query.lower().strip()
        target_ids: set[str] = set()
        for en_id, zh in SKILL_ZH_NAMES.items():
            if q == en_id or q in zh or en_id in q:
                target_ids.add(en_id)
        all_skill_ids = set(SKILL_ZH_NAMES.keys())
        if q in all_skill_ids:
            target_ids.add(q)
        if not target_ids:
            return []

        results: list[tuple[Item, Recipe]] = []
        seen: set[tuple[str, str]] = set()
        for result_id, recipes in self.recipes.items():
            for r in recipes:
                if r.skill_used.lower() not in target_ids:
                    continue
                key = (result_id, f"{r.difficulty}|{r.time}|{r.source_file}")
                if key in seen:
                    continue
                seen.add(key)
                it = self.items.get(result_id)
                if not it:
                    it = Item(id=result_id, type="?", name=result_id)
                results.append((it, r))
        results.sort(key=lambda x: (
            x[1].difficulty if x[1].difficulty is not None else 99,
            x[1].time or "",
        ))
        return results

    def search_by_skill_full(self, skill_query: str) -> tuple[list[str], list[tuple[Item, Recipe]]]:
        """按主技能搜 (返回完整结果集, 不限制数量)."""
        from .constants import SKILL_ZH_NAMES
        q = skill_query.lower().strip()
        target_ids: set[str] = set()
        for en_id, zh in SKILL_ZH_NAMES.items():
            if q == en_id or q in zh or en_id in q:
                target_ids.add(en_id)
        if q in set(SKILL_ZH_NAMES):
            target_ids.add(q)
        if not target_ids:
            return [], []
        results: list[tuple[Item, Recipe]] = []
        seen: set[tuple[str, str]] = set()
        for result_id, recipes in self.recipes.items():
            for r in recipes:
                if r.skill_used.lower() not in target_ids:
                    continue
                key = (result_id, f"{r.difficulty}|{r.time}|{r.source_file}")
                if key in seen:
                    continue
                seen.add(key)
                it = self.items.get(result_id)
                if not it:
                    it = Item(id=result_id, type="?", name=result_id)
                results.append((it, r))
        # 按 difficulty 升序, 同难度按 time 升序
        results.sort(key=lambda x: (
            x[1].difficulty if x[1].difficulty is not None else 99,
            x[1].time or "",
        ))
        return sorted(target_ids), results

    def search_by_proficiency(self, prof_query: str) -> tuple[list[str], list[tuple[Item, Recipe]]]:
        """按熟练度 (prof_*) 搜配方. 返回 (匹配的中文/英文熟练度列表, 配方列表)."""
        from .constants import SKILL_ZH_NAMES
        q = prof_query.lower().strip().replace(" ", "_")
        if not q.startswith("prof_"):
            q = "prof_" + q
        # 找匹配 (支持 prof_blacksmithing / 锻造 / 锉工 / blacksmithing 等)
        # 先把所有 prof_* 拉出来
        all_profs = self._all_proficiencies()
        matched = [p for p in all_profs
                   if q in p.lower() or
                      q.replace("prof_", "").replace("_", "") in
                      p.lower().replace("prof_", "").replace("_", "")]
        # 中文/俗称 → prof_* 映射
        zh_map = {
            "锻造": "prof_blacksmithing",
            "锻师": "prof_blacksmithing",
            "锉工": "prof_metalworking",       # user's example
            "金属加工": "prof_metalworking",
            "刀匠": "prof_bladesmith",
            "铸剑": "prof_bladesmith",
            "铸甲": "prof_armorsmithing",
            "弓匠": "prof_bowyery",
            "皮革": "prof_leatherworking",
            "裁缝": "prof_leatherworking_basic",
            "焊接": "prof_welding",
            "木工": "prof_carpentry_basic",
            "料理": "prof_food_prep",
            "罐头": "prof_food_canning",
            "裁切": "prof_carving",
            "修补": "prof_closures",
            "火工": "prof_handloading",
            "塑料": "prof_plasticworking",
        }
        if q in zh_map:
            mapped = zh_map[q]
            if mapped not in matched:
                matched.insert(0, mapped)
        for cn, prof in zh_map.items():
            if cn in prof_query or prof_query in cn:
                if prof not in matched:
                    matched.insert(0, prof)

        results: list[tuple[Item, Recipe]] = []
        seen: set[tuple[str, str]] = set()
        for result_id, recipes in self.recipes.items():
            for r in recipes:
                if not r.proficiencies:
                    continue
                prof_ids = {p["proficiency"] for p in r.proficiencies}
                if not (prof_ids & set(matched)):
                    continue
                key = (result_id, f"{r.difficulty}|{r.time}|{r.source_file}")
                if key in seen:
                    continue
                seen.add(key)
                it = self.items.get(result_id)
                if not it:
                    it = Item(id=result_id, type="?", name=result_id)
                results.append((it, r))
        results.sort(key=lambda x: (
            x[1].difficulty if x[1].difficulty is not None else 99,
            x[1].time or "",
        ))
        return matched, results

    def _all_proficiencies(self) -> set[str]:
        """返回所有配方里出现的 prof_* id 集合."""
        profs: set[str] = set()
        for recipes in self.recipes.values():
            for r in recipes:
                for p in r.proficiencies:
                    profs.add(p["proficiency"])
        return profs

    # ── XP / 升级计算 (CDDA 公式) ──

    @staticmethod
    def parse_time_to_seconds(time_str: str) -> int:
        """CDDA 时间字符串 → 秒 (如 '5 m' → 300, '1 h 30 m' → 5400)."""
        if not time_str:
            return 0
        s = time_str.strip()
        # 纯数字 (旧 JSON 用秒数)
        try:
            return int(float(s))
        except ValueError:
            pass
        total = 0
        cur_num = ""
        for ch in s:
            if ch.isdigit() or ch == ".":
                cur_num += ch
            elif ch == " " and cur_num:
                cur_num += " "  # placeholder
            elif cur_num:
                num = int(float(cur_num.strip())) if "." in cur_num else int(cur_num.strip())
                if ch == "s":
                    total += num
                elif ch == "m":
                    total += num * 60
                elif ch == "h":
                    total += num * 3600
                elif ch == "d":
                    total += num * 86400
                cur_num = ""
        return total

    @staticmethod
    def time_to_moves(time_str: str) -> int:
        """CDDA 时间字符串 → moves (1 秒 = 100 moves)."""
        return Database.parse_time_to_seconds(time_str) * 100

    @staticmethod
    def xp_for_recipe(recipe_time: str) -> int:
        """每个配方基础 XP (= time_moves / 100, 等于游戏内 seconds)."""
        return Database.time_to_moves(recipe_time) // 100

    @staticmethod
    def skill_cap_for_difficulty(difficulty: int | None) -> int:
        """CDDA 公式: cap = difficulty * 1.25 (向下取整)."""
        if difficulty is None:
            return 0
        return int(difficulty * 1.25)

    # CDDA D&D 5e 升级阈值 (level 0→1 要 300 XP, etc.)
    XP_CUTOFFS = (300, 900, 2700, 6500, 14000, 23000, 34000, 48000, 64000, 85000,
                  100000, 120000, 140000, 165000, 195000, 225000, 265000, 305000, 355000, 405000)

    @staticmethod
    def xp_needed(current_level: int, target_level: int) -> int:
        """从 current_level 升到 target_level 还需要的 XP."""
        if target_level <= current_level or target_level > 20:
            return 0
        # 累计阈值: 升到 L 需要 xp_cutoffs[L-1] XP
        # current_level = 0 表示还没升级, 累计 XP = 0
        cur_xp = 0 if current_level <= 0 else Database.XP_CUTOFFS[current_level - 1]
        return Database.XP_CUTOFFS[target_level - 1] - cur_xp

    def fastest_to_level(self, skill_query: str, current_level: int = 0,
                          target_level: int = 1) -> dict:
        """分析最快从 current_level 升到 target_level 的方案.

        Returns: {
            'matched_ids': [...],
            'xp_needed': int,                    # 目标升级还需 XP
            'xp_cutoff_target': int,             # 升到 target_level 所需累计 XP
            'crafts': [(item, recipe, xp_each, n_crafts, total_xp, total_time_min)],
            'plan': [(item, recipe, n_crafts)],  # 最少制造次数的最快方案
        }"""
        from .constants import SKILL_ZH_NAMES
        q = skill_query.lower().strip()
        target_ids: set[str] = set()
        for en_id, zh in SKILL_ZH_NAMES.items():
            if q == en_id or q in zh or en_id in q:
                target_ids.add(en_id)
        if q in set(SKILL_ZH_NAMES):
            target_ids.add(q)
        if not target_ids:
            return {"matched_ids": [], "crafts": [], "plan": []}

        # XP 阈值: 升到 L 级需要的累计 XP
        xp_cutoff_target = self.XP_CUTOFFS[target_level - 1]
        # 当前等级已积累的 XP (假设从 0 开始算)
        xp_already = self.XP_CUTOFFS[current_level - 1] if current_level >= 1 else 0
        xp_needed = xp_cutoff_target - xp_already

        # 收集所有目标技能的配方, 计算每个配方的 XP
        all_candidates = []
        for result_id, recipes in self.recipes.items():
            for r in recipes:
                if r.skill_used.lower() not in target_ids:
                    continue
                xp_each = self.xp_for_recipe(r.time)
                if xp_each <= 0:  # 0 XP / 无 time 字段的配方跳过
                    continue
                # cap = difficulty * 1.25; 如果 current_level >= cap, 此配方练不上 xp
                cap = self.skill_cap_for_difficulty(r.difficulty)
                if current_level >= cap and cap > 0:
                    continue
                all_candidates.append((result_id, r, xp_each))

        # 去重 (同 result_id + time + difficulty)
        seen = set()
        unique = []
        for rid, r, xp in all_candidates:
            k = (rid, r.time, r.difficulty)
            if k in seen:
                continue
            seen.add(k)
            it = self.items.get(rid) or Item(id=rid, type="?", name=rid)
            unique.append((it, r, xp))

        # 计算每个配方制造 n 次需要的次数和总时间
        # 公式: n_crafts = ceil(xp_needed / xp_each), total_xp = n_crafts * xp_each
        enriched = []
        for it, r, xp_each in unique:
            n = (xp_needed + xp_each - 1) // xp_each   # 向上取整
            total_xp = n * xp_each
            total_sec = self.parse_time_to_seconds(r.time) * n
            enriched.append((it, r, xp_each, n, total_xp, total_sec))
        # 排序: 难度低优先 (更易学), 然后总时间, 然后 XP 单次效率
        enriched.sort(key=lambda x: (
            x[1].difficulty if x[1].difficulty is not None else 99,
            x[5],                  # 总时间
            -x[2],                 # XP 效率 (越多越优先)
        ))

        # 找最少次数方案
        plan = []
        for it, r, xp_each, n, total_xp, total_sec in enriched:
            plan.append((it, r, n))

        return {
            "matched_ids": sorted(target_ids),
            "current_level": current_level,
            "target_level": target_level,
            "xp_needed": xp_needed,
            "xp_cutoff_target": xp_cutoff_target,
            "crafts": enriched,
            "plan": plan,
        }

    # ── 工具方法 ──

    def get_recipes(self, item_id: str) -> list[Recipe]:
        return self.recipes.get(item_id, [])

    def get_zh_name(self, obj_id: str) -> str | None:
        """查物品或家具的中文名: 内置词典 > PO 正向."""
        if obj_id in self.item_zh_names:
            return self.item_zh_names[obj_id]
        if obj_id in self.furn_zh_names:
            return self.furn_zh_names[obj_id]
        it = self.items.get(obj_id)
        if it and it.name in self.po_translations:
            zh = self.po_translations[it.name]
            if zh and zh != it.name:
                return zh
        fn = self.furniture.get(obj_id)
        if fn and fn.name in self.po_translations:
            zh = self.po_translations[fn.name]
            if zh and zh != fn.name:
                return zh
        return None


def _item_to_cache_dict(it: Item) -> dict:
    return {
        "id": it.id, "type": it.type, "name": it.name,
        "description": it.description, "weight": it.weight,
        "volume": it.volume, "material": it.material,
        "price": it.price, "price_postapoc": it.price_postapoc,
        "flags": it.flags, "category": it.category,
        "subcategory": it.subcategory, "qualities": it.qualities,
        "ammo": it.ammo, "extra": it.extra,
        "source_file": it.source_file,
    }


def _furniture_to_cache_dict(f: Furniture) -> dict:
    return {
        "id": f.id, "type": f.type, "subtype": f.subtype,
        "name": f.name, "description": f.description,
        "flags": f.flags, "move_cost": f.move_cost,
        "coverage": f.coverage, "examine_action": f.examine_action,
        "deconstruct_items": f.deconstruct_items,
        "bash_items": f.bash_items, "source_file": f.source_file,
    }


def build_database(rebuild: bool = False) -> Database:
    CACHE_FILE.parent.mkdir(parents=True, exist_ok=True)

    if not rebuild and CACHE_FILE.exists():
        print(f"[info] 加载缓存 {CACHE_FILE} ...", file=sys.stderr)
        try:
            with CACHE_FILE.open(encoding="utf-8") as f:
                raw = json.load(f)
            db = Database()
            for iid, src in raw["items"].items():
                src["extra"] = src.get("extra", {})
                src["material"] = src.get("material", [])
                src["flags"] = src.get("flags", [])
                src["qualities"] = src.get("qualities", [])
                src["ammo"] = src.get("ammo", [])
                db.items[iid] = Item(**src)
            for fid, src in raw.get("furniture", {}).items():
                db.furniture[fid] = Furniture(**src)
            recipe_defaults = {
                "result": "", "type": "recipe", "skill_used": "",
                "skills_required": [], "difficulty": None, "time": "",
                "components": [], "using": [], "qualities": [],
                "autolearn": False, "book_learn": [], "proficiencies": [],
                "decomp_learn": None, "reversible": False,
                "activity_level": "", "byproducts": [], "source_file": "",
            }
            for rid, rlist in raw["recipes"].items():
                rebuilt = []
                for r in rlist:
                    kw = {k: r.get(k, v) for k, v in recipe_defaults.items()}
                    for tuple_field in ("skills_required", "using",
                                       "qualities", "book_learn"):
                        if isinstance(kw.get(tuple_field), list):
                            kw[tuple_field] = [
                                tuple(x) if isinstance(x, list) else x
                                for x in kw[tuple_field]
                            ]
                    rebuilt.append(Recipe(**kw))
                db.recipes[rid] = rebuilt
            if CACHE_FILE_ZH.exists():
                with CACHE_FILE_ZH.open(encoding="utf-8") as f:
                    zh_raw = json.load(f)
                db.po_translations = zh_raw.get("po_translations", {})
                db.zh_to_en = zh_raw.get("zh_to_en", {})
            db.item_zh_names = dict(ITEM_ZH_NAMES)
            db.furn_zh_names = dict(FURN_ZH_NAMES)
            print(f"[info] 缓存命中: {len(db.items)} 物品 / "
                  f"{len(db.furniture)} 家具 / "
                  f"{sum(len(v) for v in db.recipes.values())} 配方",
                  file=sys.stderr)
            return db
        except (OSError, json.JSONDecodeError, KeyError, TypeError) as exc:
            print(f"[warn] 缓存损坏, 重新构建: {exc}", file=sys.stderr)

    print("[info] 重建索引中 ...", file=sys.stderr)
    t0 = time.time()
    db = Database()

    # 物品
    item_count = 0
    for path in iter_json_files(ITEMS_DIR):
        for obj in load_json_safe(path):
            it = item_from_json(obj, str(path.relative_to(ITEMS_DIR.parent.parent)))
            if it:
                db.items[it.id] = it
                item_count += 1

    # 配方
    recipe_count = 0
    for path in iter_json_files(RECIPES_DIR):
        for obj in load_json_safe(path):
            r = recipe_from_json(obj, str(path.relative_to(RECIPES_DIR.parent.parent)))
            if r:
                db.recipes.setdefault(r.result, []).append(r)
                recipe_count += 1

    # 家具/地形/电器
    furn_count = 0
    if FURNITURE_DIR.exists():
        for path in iter_json_files(FURNITURE_DIR):
            for obj in load_json_safe(path):
                f = furniture_from_json(obj, str(path.relative_to(FURNITURE_DIR.parent.parent)))
                if f:
                    db.furniture[f.id] = f
                    furn_count += 1

    # PO
    if PO_FILE.exists():
        t_po = time.time()
        db.po_translations, db.zh_to_en = parse_po_file(PO_FILE)
        print(f"[info] PO 文件解析: {len(db.po_translations)} 条翻译 / "
              f"{len(db.zh_to_en)} 反向索引项 "
              f"({time.time() - t_po:.1f}s)", file=sys.stderr)
    else:
        print(f"[warn] 未找到 PO 文件 {PO_FILE}, 中文搜索不可用", file=sys.stderr)

    db.item_zh_names = dict(ITEM_ZH_NAMES)
    db.furn_zh_names = dict(FURN_ZH_NAMES)

    print(f"[info] 索引完成: {item_count} 物品 / {recipe_count} 配方 "
          f"/ {furn_count} 家具地形 ({time.time() - t0:.1f}s)", file=sys.stderr)

    cache_obj = {
        "items": {iid: _item_to_cache_dict(it) for iid, it in db.items.items()},
        "furniture": {fid: _furniture_to_cache_dict(f)
                       for fid, f in db.furniture.items()},
        "recipes": {rid: [_recipe_to_cache_dict(r) for r in rlist]
                     for rid, rlist in db.recipes.items()},
    }
    try:
        with CACHE_FILE.open("w", encoding="utf-8") as f:
            json.dump(cache_obj, f, ensure_ascii=False)
        print(f"[info] 索引已缓存到 {CACHE_FILE}", file=sys.stderr)
    except OSError as exc:
        print(f"[warn] 缓存写入失败: {exc}", file=sys.stderr)

    try:
        zh_cache = {
            "po_translations": db.po_translations,
            "zh_to_en": dict(db.zh_to_en),
        }
        with CACHE_FILE_ZH.open("w", encoding="utf-8") as f:
            json.dump(zh_cache, f, ensure_ascii=False)
        print(f"[info] PO 缓存已写入 {CACHE_FILE_ZH}", file=sys.stderr)
    except OSError as exc:
        print(f"[warn] PO 缓存写入失败: {exc}", file=sys.stderr)

    return db