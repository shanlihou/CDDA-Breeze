"""常量与内置词典."""

from __future__ import annotations

from pathlib import Path

# ── 路径 ──
PROJECT_ROOT = Path(__file__).resolve().parent.parent.parent
ITEMS_DIR = PROJECT_ROOT / "data" / "json" / "items"
RECIPES_DIR = PROJECT_ROOT / "data" / "json" / "recipes"
FURNITURE_DIR = PROJECT_ROOT / "data" / "json" / "furniture_and_terrain"
PO_FILE = PROJECT_ROOT / "lang" / "po" / "zh_CN.po"

CACHE_DIR = Path(__file__).resolve().parent.parent / ".cache"
CACHE_FILE = CACHE_DIR / "cdda_index.json"
CACHE_FILE_ZH = CACHE_DIR / "cdda_zh_index.json"


# ── CJK 辅助 ──

def has_chinese(s: str) -> bool:
    """判断字符串是否含 CJK 汉字."""
    if not s:
        return False
    for ch in s:
        code = ord(ch)
        if (0x4E00 <= code <= 0x9FFF) or (0x3400 <= code <= 0x4DBF) \
                or (0xF900 <= code <= 0xFAFF):
            return True
    return False


def tokenize_query(query: str) -> list[str]:
    """把查询切成 token. 英文按空格分词,中文按连续 CJK 段切分."""
    tokens: list[str] = []
    if has_chinese(query):
        cur = ""
        for ch in query:
            if has_chinese(ch):
                cur += ch
            else:
                if cur:
                    tokens.append(cur)
                    cur = ""
                if not ch.isspace():
                    tokens.append(ch)
        if cur:
            tokens.append(cur)
    else:
        tokens = query.split()
    return [t for t in tokens if t.strip()]


# ── 内置中英对照词典 ──

ITEM_ZH_NAMES: dict[str, str] = {
    # ── 斧 / 砍伐 ──
    "ax":                "木斧",
    "copper_ax":         "铜斧",
    "bronze_axe":        "青铜斧",
    "primitive_axe":     "石斧",
    "hand_axe":          "石斧头",
    "makeshift_axe":     "简易斧头",
    "fire_ax":           "消防斧",
    "hatchet":           "短柄斧 / 斧头",
    "iceaxe":            "冰镐",
    "throwing_axe":      "投掷斧",
    "war_axe":           "战斧",
    "battleaxe":         "战斧",
    "battle_axe":        "战斗斧",
    "battle_axe_fake":   "假战斗斧",
    "shillelagh":        "橡木棍",
    "woodsaw":           "木锯",
    "bow_saw":           "弓锯",
    "circular_saw":      "圆锯",
    "chainsaw_off":      "电锯 (关)",
    "chainsaw_on":       "电锯 (开)",
    "axe_primitive":     "原始斧",
    # ── 弓 / 弩 ──
    "shortbow":          "短弓",
    "longbow":           "长弓",
    "woodgreatbow":      "巨木弓",
    "compositebow":      "合成弓",
    "recurvebow":        "反曲弓",
    "reflexbow":         "反射弓",
    "hand_crossbow":     "手弩",
    "crossbow":          "木弩",
    "compositecrossbow": "复合弩",
    "pneumatic_crossbow": "气动弩",
    "huge_crossbow":     "巨型弩",
    "sling":             "撒手式投石索",
    "slingshot":         "弹弓",
    "wristrocket":       "腕式弹弓",
    # ── 剑 ──
    "broadsword":        "阔剑",
    "longsword":         "长剑",
    "katana":            "武士刀",
    "wakizashi":         "胁差",
    "rapier":            "迅击剑",
    "cutlass":           "弯刀",
    "saber":             "军刀",
    "scimitar":          "阿拉伯弯刀",
    "estoc":             "穿甲剑",
    "zweihander":        "双手剑",
    "battle_chain":      "连枷",
    "flail":             "连枷",
    "mace":              "钉锤",
    "morningstar":       "晨星锤",
    "warhammer":         "战锤",
    # ── 刀 / 匕首 ──
    "knife_combat":      "战斗刀",
    "knife_survival":    "求生刀",
    "knife_butcher":     "屠夫刀",
    "knife_steak":       "牛排刀",
    "knife_trench":      "堑壕刀",
    "kukri":             "廓尔喀弯刀",
    "machete":           "砍刀",
    "makeshift_knife":   "简易刀",
    "primitive_knife":   "原始刀",
    "bone_knife":        "骨刀",
    "switchblade":       "弹簧刀",
    "copper_knife":      "铜刀",
    "bronze_knife":      "青铜刀",
    "stone_knife":       "石刀",
    "sharp_rock":        "尖石",
    "scissors":          "剪刀",
    "scalpel":           "手术刀",
    # ── 棍 / 钝器 ──
    "hammer_sledge":     "大锤",
    "hammer":            "锤子",
    "mallet":            "木槌",
    "crowbar":           "撬棍",
    "halligan":          "哈利根撬棍",
    "wrecking_bar":      "拆墙杆",
    "shovel":            "铲子",
    "pickaxe":           "十字镐",
    "pick":              "镐",
    "plank":             "木板",
    "nailgun":           "射钉枪",
    # ── 枪 ──
    "pistol":            "手枪",
    "9mm":               "9mm 手枪",
    "magnum":            "马格南",
    "rifle":             "步枪",
    "shotgun":           "霰弹枪",
    "smg":               "冲锋枪",
    "pipe_rifle":        "管子步枪",
    "pipe_shotgun":      "管子霰弹枪",
    "surv_rifle":        "土制步枪",
    "surv_shotgun":      "土制霰弹枪",
    # ── 火源 ──
    "lighter":           "塑料打火机",
    "small_lighter":     "迷你打火机",
    "ref_lighter":       "可充气打火机",
    "ref_lighter_on":    "可充气打火机 (开)",
    "ref_lighter_butane": "丁烷打火机",
    "matches":           "火柴盒",
    "ref_matches":       "盒装火柴",
    "survival_match":    "求生火柴",
    "fire_drill":        "弓钻",
    "fire_drill_large":  "泵钻",
    "magnifying_glass":  "放大镜",
    "fire_piston":       "火活塞",
    "flint_steel":       "打火石",
    "crude_firestarter": "自制电点火器",
    "bio_lighter":       "点火器 (生化)",
    "fake_firestarter":  "点火器 (生化)",
    "crude_picklock":    "简易撬锁器",
    # ── 水 ──
    "water":             "水 (脏)",
    "water_clean":       "净水",
    "salt_water":        "盐水",
    "water_sewage":      "污水",
    "saline":            "生理盐水",
    "eyedrops":          "盐水滴眼液",
    # ── 医疗 ──
    "aspirin":           "阿司匹林",
    "codeine":           "可待因",
    "morphine":          "吗啡",
    "tramadol":          "曲马多",
    "antibiotics":       "抗生素",
    "bandages":          "绷带",
    "disinfectant":      "消毒液",
    "first_aid":         "急救包",
    "iodine":            "碘伏",
    "quikclot":          "速效止血粉",
    "adrenaline_injector": "肾上腺素注射器",
    "epinephrine_injector": "肾上腺素注射器",
    "thorazine":         "抗精神病药",
    "prozac":            "百忧解",
    "xanax":             "赞安诺",
    "vitamins":          "维生素",
    "calcium_tablet":    "钙片",
    "multivitamin":      "复合维生素",
    # ── 食物 ──
    "meat_cooked":       "熟肉",
    "meat":              "生肉",
    "fish_cooked":       "熟鱼",
    "vegetables_cooked": "熟蔬菜",
    "apple":             "苹果",
    "orange":            "橙子",
    "banana":            "香蕉",
    "sandwich":          "三明治",
    "pizza":             "披萨",
    "jerky":             "肉干",
    "soup_wood":         "汤",
    "tea":               "茶",
    "coffee":            "咖啡",
    "beer":              "啤酒",
    "wine":              "葡萄酒",
    "whiskey":           "威士忌",
    # ── 工具 ──
    "screwdriver":       "螺丝刀",
    "wrench":            "扳手",
    "pliers":            "钳子",
    "duct_tape":         "强力胶带",
    "wire":              "电线",
    "cable":             "电缆",
    "rope_30":           "绳子 (30ft)",
    "rope_6":            "绳子 (6ft)",
    "rope_makeshift_30": "简易绳",
    "chain":             "链条",
    "lockpick":          "撬锁器",
    "jackscrew":         "千斤顶",
    "sewing_kit":        "缝纫工具",
    "needle_bone":       "骨针",
    "toolset":           "成套工具",
    "soldering_iron":    "电烙铁",
    "welder":            "焊机",
    "vacuum_pump":       "真空泵",
    # ── 服装 ──
    "backpack":          "背包",
    "rucksack":          "登山包",
    "duffel_bag":        "行李袋",
    "purse":             "手提包",
    "fanny_pack":        "腰包",
    "holster":           "枪套",
    "sheath":            "刀鞘",
    "bandolier":         "弹带",
    "tactical_vest":     "战术背心",
    "kevlar":            "凯夫拉",
    "cloak":             "斗篷",
    "trenchcoat":        "风衣",
    "jacket_leather":    "皮夹克",
    "jeans":             "牛仔裤",
    "sneakers":          "运动鞋",
    "boots":             "靴子",
    "fire_boots":        "消防靴",
    "wristwatch":        "手表",
    "gloves_leather":    "皮手套",
    "gloves_fingerless": "露指手套",
    "goggles_welding":   "焊接护目镜",
    "mask_gas":          "防毒面具",
    "hat_hard":          "安全帽",
    "helmet_motor":      "摩托头盔",
    "helmet_riot":       "防暴头盔",
    "vest_leather":      "皮背心",
    # ── 弹药 ──
    "22lr":              ".22 LR 弹药",
    "9mm":               "9mm 弹药",
    "45_acp":            ".45 ACP 弹药",
    "shot_bird":         "霰弹 (鸟弹)",
    "shot_00":           "00 鹿弹",
    "shot_slug":         "独头弹",
    "arrow_wood":        "木箭",
    "arrow_metal":       "金属箭头箭",
    "bolt_wood":         "木弩矢",
    "bolt_metal":        "金属弩矢",
    "40mm_concussive":   "40mm 震撼弹",
    # ── 车辆 / 容器 ──
    "jerrycan":          "备用油桶",
    "gasoline":          "汽油",
    "diesel":            "柴油",
    "battery_car":       "汽车电瓶",
    "storage_battery":   "蓄电池",
    "solar_panel":       "太阳能板",
    "solar_panel_v2":    "太阳能板 v2",
    "reinforced_solar_panel": "加固太阳能板",
    "reinforced_solar_panel_v2": "加固太阳能板 v2",
    "folding_solar_panel": "折叠太阳能板",
    "folding_solar_panel_v2": "折叠太阳能板 v2",
    "minifridge":        "迷你冰箱",
    "minifreezer":       "迷你冰柜",
    "fridge":            "冰箱",
    "freezer":           "冰柜",
    "oven":              "烤箱",
    "kitchen_unit":      "厨房一体",
    "solarpack":         "太阳能背包",
    "water_purifier":    "净水器",
    # ── 书籍 ──
    "book_archery":      "射箭手册",
    "recipe_bows":       "弓类配方",
    "manual_archery":    "射箭指南",
    "manual_fabrication": "制造手册",
    "manual_mechanics":  "机械手册",
    "manual_electronics": "电子手册",
    "manual_tailoring":  "裁缝手册",
    "manual_cooking":    "烹饪手册",
    "manual_first_aid":  "急救手册",
    "manual_traps":      "陷阱手册",
    "textbook_fabrication": "制造学教材",
    "textbook_armschina": "中国武器",
    "textbook_survival": "求生教材",
    "textbook_chemistry": "化学教材",
    "textbook_biodiversity": "生物教材",
    "textbook_first_aid": "急救教材",
    "textbook_lockpick": "撬锁教材",
    "textbook_mechanics": "机械教材",
    "textbook_electronics": "电子教材",
    "textbook_computers": "计算机教材",
    "textbook_robotics": "机器人教材",
    "textbook_tailoring": "裁缝教材",
    "textbook_cooking":  "烹饪教材",
    "textbook_fireman":  "消防员教材",
    "textbook_gaswarfare": "化武教材",
    "textbook_army":     "军用教材",
    "textbook_carpentry": "木工教材",
    "atomic_survival":   "原子求生",
    "mag_survival":      "求生杂志",
    "survival_book":     "求生手册",
    "pocket_survival":   "口袋求生",
    "book_first_aid":    "急救手册",
    "book_gun":          "枪械书",
    "mag_archery":       "射箭杂志",
    "mag_fabrication":   "制造杂志",
    "mag_mechanics":     "机械杂志",
    "mag_electronics":   "电子杂志",
    "mag_tailoring":     "裁缝杂志",
    "mag_cooking":       "烹饪杂志",
    "mag_traps":         "陷阱杂志",
    "mag_first_aid":     "急救杂志",
    "fun_survival":      "求生小说",
    # ── 工具 / 弹药类别 ──
    "toolset_extended":   "扩充工具组",
    "pickaxe_primitive": "原始镐",
    "jackhammer":        "风镐",
    "boltcutters":       "断线钳",
    "saw":               "锯",
    "hacksaw":           "钢锯",
    "chisel":            "凿子",
    "trowel":            "泥铲",
    "sandbag":           "沙袋",
    "mortar_pestle":     "研钵",
    "funnel":            "漏斗",
    "pot":               "锅",
    "pan":               "平底锅",
    "skillet":           "煎锅",
    "wok":               "炒锅",
    "coffeepot":         "咖啡壶",
    "kettle":            "水壶",
    "thermos":           "保温杯",
    "jar_glass":         "玻璃罐",
    "jar_3l_glass":      "3L 玻璃罐",
    "bowl_plastic":      "塑料碗",
    "cup_plastic":       "塑料杯",
    "plate_pizza":       "披萨盘",
    "bottle_glass":      "玻璃瓶",
    "bottle_plastic":    "塑料瓶",
    "can_drink":         "易拉罐",
    "can_food":          "罐头",
    "bag_plastic":       "塑料袋",
    "box_small":         "小盒子",
    "box_medium":        "中盒子",
    "box_large":         "大盒子",
    "bag_garbage":       "垃圾袋",
}


FURN_ZH_NAMES: dict[str, str] = {
    # ── 火源 ──
    "f_fireplace":                  "壁炉",
    "f_woodstove":                  "木火炉",
    "f_hobo_stove":                 "流浪汉火炉",
    "f_brazier":                    "火盆",
    "f_firering":                   "火圈",
    "f_55gal_firebarrel":           "55加仑火桶",
    "f_30gal_firebarrel":           "30加仑火桶",
    "f_clay_oven":                  "土窑",
    # ── 取暖 ──
    "ap_heater_large":              "大型加热器",
    "ap_heater_small":              "小型加热器",
    "f_water_heater":               "热水器",
    # ── 炉 / 窑 / 锻造 ──
    "ap_kiln":                      "电窑炉",
    "f_kiln_empty":                 "空炭窑",
    "f_kiln_full":                  "满炭窑",
    "f_kiln_metal_empty":           "空金属炭窑",
    "f_kiln_metal_full":            "满金属炭窑",
    "f_clay_kiln":                  "陶土窑",
    "f_arcfurnace_empty":           "空电弧炉",
    "f_arcfurnace_full":            "满电弧炉",
    "f_arc_furnace":                "电弧炉",
    "f_home_furnace":               "家用炉",
    "ap_forge":                     "电锻造炉",
    "f_forge":                      "锻造炉",
    "f_forge_rock":                 "石锻造炉",
    "f_forge_artisan_0":            "手工锻造炉",
    "f_forge_artisan_1":            "手工锻造炉",
    # ── 烤箱 ──
    "ap_oven":                      "烤箱",
    "f_oven":                       "烤箱",
    "f_gas_oven":                   "燃气烤箱",
    "f_gas_oven_microwave_combo":   "燃气微波组合烤箱",
}


SKILL_ZH_NAMES: dict[str, str] = {
    "fabrication":  "制造",
    "tailor":       "裁缝",
    "cooking":      "烹饪",
    "electronics":  "电子",
    "gun":          "枪械",
    "survival":     "生存",
    "chemistry":    "化学",
    "mechanics":    "机械",
    "firstaid":     "急救",
    "computer":     "计算机",
    "traps":        "陷阱",
    "archery":      "弓术",
    "launcher":     "发射器",
    "melee":        "近战",
    "throw":        "投掷",
    "rifle":        "步枪",
    "pistol":       "手枪",
    "shotgun":      "霰弹",
    "bashing":      "钝击",
    "cutting":      "斩切",
    "stabbing":     "刺击",
    "swimming":     "游泳",
    "dodge":        "闪避",
    "smg":          "冲锋枪",
    "speech":       "口才",
    "unarmed":      "徒手",
}


# 所有 CDDA 实际使用的 skill id (构建索引时用于映射/统计)
ALL_SKILL_IDS = list(SKILL_ZH_NAMES.keys())