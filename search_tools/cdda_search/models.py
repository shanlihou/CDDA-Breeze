"""数据模型与 JSON 解析."""

from __future__ import annotations

from dataclasses import dataclass, field


# ── 关注的字段,避免把整个 ~50MB JSON 全存进缓存 ──

ITEM_FIELDS = (
    "id", "type", "name", "description", "weight", "volume",
    "material", "price", "price_postapoc", "flags", "symbol",
    "color", "longest_side", "category", "subcategory",
    "qualities", "ammo", "initial_charges", "max_charges",
    "use_action", "bashing", "cutting",
)
RECIPE_FIELDS = (
    "result", "type", "skill_used", "skills_required", "difficulty",
    "time", "components", "using", "qualities", "autolearn",
    "book_learn", "proficiencies", "decomp_learn", "reversible",
    "activity_level", "byproducts",
)


@dataclass(slots=True)
class Item:
    id: str
    type: str
    name: str
    description: str = ""
    weight: str = ""
    volume: str = ""
    material: list[str] = field(default_factory=list)
    price: int | str | None = None
    price_postapoc: int | str | None = None
    flags: list[str] = field(default_factory=list)
    category: str = ""
    subcategory: str = ""
    qualities: list = field(default_factory=list)
    ammo: list[str] = field(default_factory=list)
    extra: dict = field(default_factory=dict)
    source_file: str = ""


@dataclass(slots=True)
class Furniture:
    """furniture / terrain / appliance."""
    id: str
    type: str = "FURN"
    subtype: str = "furniture"
    name: str = ""
    description: str = ""
    flags: list[str] = field(default_factory=list)
    move_cost: int | None = None
    coverage: int | None = None
    examine_action: str = ""
    deconstruct_items: list = field(default_factory=list)
    bash_items: list = field(default_factory=list)
    source_file: str = ""


@dataclass(slots=True)
class Recipe:
    result: str
    type: str = "recipe"
    skill_used: str = ""
    skills_required: list[tuple[str, int]] = field(default_factory=list)
    difficulty: int | None = None
    time: str = ""
    components: list[list[tuple[str, int, str | None]]] = field(default_factory=list)
    using: list[tuple[str, int]] = field(default_factory=list)
    qualities: list[tuple[str, int]] = field(default_factory=list)
    autolearn: bool | list = False
    book_learn: list[tuple[str, int]] = field(default_factory=list)
    proficiencies: list[dict] = field(default_factory=list)
    decomp_learn: int | None = None
    reversible: bool = False
    activity_level: str = ""
    byproducts: list = field(default_factory=list)
    source_file: str = ""


# ── JSON 辅助 ──

def _extract_name(name_field) -> str:
    """name 可能是 {str: ...} 或 str_pl / str_sp."""
    if isinstance(name_field, str):
        return name_field
    if isinstance(name_field, dict):
        return name_field.get("str") or name_field.get("str_sp") or name_field.get("ctxt") or ""
    return ""


def _coerce_list(val) -> list:
    if val is None:
        return []
    if isinstance(val, list):
        return val
    return [val]


def _filter_obj(obj: dict, keep: tuple[str, ...]) -> dict:
    return {k: obj[k] for k in keep if k in obj}


# ── 归一化函数 (处理 CDDA 多变 JSON 写法) ──

def _normalize_skills_required(val) -> list[tuple[str, int]]:
    if val is None:
        return []
    if not isinstance(val, list):
        return [(str(val), 1)]
    if val and all(isinstance(x, list) and len(x) >= 2 for x in val):
        return [(str(p[0]), int(p[1])) for p in val]
    out: list[tuple[str, int]] = []
    pending: str | None = None
    for elem in val:
        if isinstance(elem, str):
            if pending is not None:
                out.append((pending, 1))
            pending = elem
        elif isinstance(elem, (int, float)):
            if pending is not None:
                out.append((pending, int(elem)))
                pending = None
        elif isinstance(elem, list) and len(elem) >= 2:
            out.append((str(elem[0]), int(elem[1])))
    if pending is not None:
        out.append((pending, 1))
    return out


def _normalize_qualities(val) -> list[tuple[str, int]]:
    if val is None:
        return []
    if not isinstance(val, list):
        return []

    def from_one(entry):
        if isinstance(entry, dict):
            return (str(entry.get("id", "?")), int(entry.get("level", 1)))
        if isinstance(entry, list):
            if len(entry) >= 2 and not isinstance(entry[0], (list, dict)):
                return (str(entry[0]), int(entry[1]))
            inner: list[tuple[str, int]] = []
            for sub in entry:
                if isinstance(sub, dict):
                    inner.append((str(sub.get("id", "?")),
                                  int(sub.get("level", 1))))
                elif isinstance(sub, list) and len(sub) >= 2:
                    inner.append((str(sub[0]), int(sub[1])))
                elif isinstance(sub, str):
                    inner.append((sub, 1))
            return inner
        if isinstance(entry, str):
            return (entry, 1)
        return None

    out: list[tuple[str, int]] = []
    for q in val:
        r = from_one(q)
        if r is None:
            continue
        if isinstance(r, list):
            out.extend(r)
        else:
            out.append(r)
    return out


def _normalize_components(val) -> list[list[tuple[str, int, str | None]]]:
    if val is None or not isinstance(val, list):
        return []
    out: list[list[tuple[str, int, str | None]]] = []
    for group in val:
        if not isinstance(group, list):
            continue
        group_items: list[tuple[str, int, str | None]] = []
        for c in group:
            if isinstance(c, list) and len(c) >= 2:
                flag = c[2] if len(c) >= 3 and isinstance(c[2], str) else None
                group_items.append((str(c[0]), int(c[1]), flag))
        out.append(group_items)
    return out


def _normalize_using(val) -> list[tuple[str, int]]:
    if val is None or not isinstance(val, list):
        return []
    return [(str(e[0]), int(e[1]))
            for e in val if isinstance(e, list) and len(e) >= 2]


def _normalize_proficiencies(val) -> list[dict]:
    if val is None or not isinstance(val, list):
        return []
    out: list[dict] = []
    for p in val:
        if isinstance(p, dict):
            out.append({
                "proficiency": p.get("proficiency", "?"),
                "time_multiplier": p.get("time_multiplier", 1.0),
                "fail_multiplier": p.get("fail_multiplier", 1.0),
                "required": p.get("required", True),
            })
        elif isinstance(p, str):
            out.append({"proficiency": p, "time_multiplier": 1.0,
                        "fail_multiplier": 1.0, "required": True})
    return out


def _normalize_book_learn(val) -> list[tuple[str, int]]:
    if val is None or not isinstance(val, list):
        return []
    return [(str(e[0]), int(e[1]))
            for e in val if isinstance(e, list) and len(e) >= 2]


def _normalize_autolearn(val):
    if val is True:
        return True
    if val is False or val is None:
        return False
    if isinstance(val, list):
        out = []
        for e in val:
            if isinstance(e, list) and len(e) >= 2:
                out.append((str(e[0]), int(e[1])))
            elif isinstance(e, str):
                out.append((e, 1))
        return out
    return bool(val)


# ── 工厂方法 ──

def item_from_json(obj: dict, source_file: str) -> Item | None:
    if "id" not in obj or "type" not in obj:
        return None
    name = _extract_name(obj.get("name", ""))
    if not name:
        return None
    slim = _filter_obj(obj, ITEM_FIELDS)
    return Item(
        id=slim["id"],
        type=slim["type"],
        name=name,
        description=slim.get("description", ""),
        weight=slim.get("weight", ""),
        volume=slim.get("volume", ""),
        material=_coerce_list(slim.get("material", [])),
        price=slim.get("price"),
        price_postapoc=slim.get("price_postapoc"),
        flags=_coerce_list(slim.get("flags", [])),
        category=slim.get("category", ""),
        subcategory=slim.get("subcategory", ""),
        qualities=_coerce_list(slim.get("qualities", [])),
        ammo=_coerce_list(slim.get("ammo", [])),
        extra={k: v for k, v in slim.items() if k not in {
            "id", "type", "name", "description", "weight", "volume",
            "material", "price", "price_postapoc", "flags", "category",
            "subcategory", "qualities", "ammo",
        }},
        source_file=source_file,
    )


def furniture_from_json(obj: dict, source_file: str) -> Furniture | None:
    if "id" not in obj:
        return None
    obj_type = obj.get("type", "furniture")
    name = _extract_name(obj.get("name", ""))
    if not name and obj_type not in ("furniture", "terrain"):
        return None
    flags = _coerce_list(obj.get("flags", []))
    decon = obj.get("deconstruct", {}) or {}
    decon_items = _coerce_list(decon.get("items", []))
    bash = obj.get("bash", {}) or {}
    bash_items = _coerce_list(bash.get("items", []))
    examine = obj.get("examine_action", {}) or {}
    examine_str = examine.get("type", "") if isinstance(examine, dict) else (
        examine if isinstance(examine, str) else ""
    )
    type_label = "FURN" if obj_type == "furniture" else (
        "TERRAIN" if obj_type == "terrain" else "APPL")
    return Furniture(
        id=obj["id"],
        type=type_label,
        subtype=obj_type,
        name=name or obj["id"],
        description=obj.get("description", ""),
        flags=flags,
        move_cost=obj.get("move_cost"),
        coverage=obj.get("coverage"),
        examine_action=examine_str,
        deconstruct_items=decon_items,
        bash_items=bash_items,
        source_file=source_file,
    )


def recipe_from_json(obj: dict, source_file: str) -> Recipe | None:
    if obj.get("type") not in ("recipe", "uncraft"):
        return None
    if "result" not in obj:
        return None
    slim = _filter_obj(obj, RECIPE_FIELDS)
    return Recipe(
        result=slim["result"],
        type=slim.get("type", "recipe"),
        skill_used=slim.get("skill_used", ""),
        skills_required=_normalize_skills_required(slim.get("skills_required")),
        difficulty=slim.get("difficulty"),
        time=slim.get("time", ""),
        components=_normalize_components(slim.get("components")),
        using=_normalize_using(slim.get("using")),
        qualities=_normalize_qualities(slim.get("qualities")),
        autolearn=_normalize_autolearn(slim.get("autolearn")),
        book_learn=_normalize_book_learn(slim.get("book_learn")),
        proficiencies=_normalize_proficiencies(slim.get("proficiencies")),
        decomp_learn=slim.get("decomp_learn"),
        reversible=bool(slim.get("reversible", False)),
        activity_level=slim.get("activity_level", ""),
        byproducts=_coerce_list(slim.get("byproducts", [])),
        source_file=source_file,
    )


def _recipe_to_cache_dict(r: Recipe) -> dict:
    """Recipe -> JSON-serializable dict (tuples -> lists)."""
    return {
        "result": r.result, "type": r.type, "skill_used": r.skill_used,
        "skills_required": [list(t) for t in r.skills_required],
        "difficulty": r.difficulty, "time": r.time,
        "components": [[[n, q, f] for (n, q, f) in g] for g in r.components],
        "using": [list(t) for t in r.using],
        "qualities": [list(t) for t in r.qualities],
        "autolearn": r.autolearn,
        "book_learn": [list(t) for t in r.book_learn],
        "proficiencies": r.proficiencies,
        "decomp_learn": r.decomp_learn, "reversible": r.reversible,
        "activity_level": r.activity_level, "byproducts": r.byproducts,
        "source_file": r.source_file,
    }