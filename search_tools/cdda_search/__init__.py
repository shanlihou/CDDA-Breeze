"""CDDA 物品 / 配方 / 家具 / 地形 搜索工具包.

模块:
    constants  - 路径常量、内置中英对照词典、CJK 辅助函数
    models     - Item / Recipe / Furniture dataclass + JSON 解析
    po         - 解析 lang/po/zh_CN.po,构建翻译词典与反向索引
    database   - Database 类 + 索引构建 + 磁盘缓存
    search     - 物品 / 主技能搜索
    render     - 命令行与 Web 共享的文本渲染
    cli        - 命令行入口 (argparse + 交互 REPL)
    web        - Flask Web UI
"""

from .constants import (
    ITEM_ZH_NAMES,
    FURN_ZH_NAMES,
    SKILL_ZH_NAMES,
    PROJECT_ROOT,
    ITEMS_DIR,
    RECIPES_DIR,
    FURNITURE_DIR,
    PO_FILE,
    CACHE_DIR,
    CACHE_FILE,
    CACHE_FILE_ZH,
    has_chinese,
    tokenize_query,
)
from .models import Item, Recipe, Furniture, item_from_json, recipe_from_json, furniture_from_json
from .po import parse_po_file
from .database import Database, build_database
from .search import render_search_results, render_skill_results
from .render import (
    render_item,
    render_item_with_recipes,
    render_furniture,
    render_recipe,
)

__version__ = "1.0.0"
__all__ = [
    # constants
    "ITEM_ZH_NAMES", "FURN_ZH_NAMES", "SKILL_ZH_NAMES",
    "PROJECT_ROOT", "ITEMS_DIR", "RECIPES_DIR", "FURNITURE_DIR", "PO_FILE",
    "CACHE_DIR", "CACHE_FILE", "CACHE_FILE_ZH",
    "has_chinese", "tokenize_query",
    # models
    "Item", "Recipe", "Furniture",
    "item_from_json", "recipe_from_json", "furniture_from_json",
    # po
    "parse_po_file",
    # database
    "Database", "build_database",
    # search/render
    "render_search_results", "render_skill_results",
    "render_item", "render_item_with_recipes", "render_furniture", "render_recipe",
]