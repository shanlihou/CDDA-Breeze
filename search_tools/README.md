# CDDA 物品 / 配方 / 家具 查询工具

离线解析 CDDA Breeze 仓库下的 JSON 数据 + 中文 PO, 提供两种界面:

| 界面 | 入口 | 适用 |
|---|---|---|
| **CLI** | `cdda_item_search.py` | 终端交互、单次查询、脚本调用 |
| **Web UI** | `run_web.py` | 浏览器搜索、详情可视化 |

## 安装

- Python 3.10+
- Web UI 额外需要: `pip install flask` (项目里 Flask 3.0.3 已可用)
- 无其他依赖

## 启动

### CLI

```powershell
# 交互模式 (支持中文)
python cdda_item_search.py

# 单次查询
python cdda_item_search.py --search "stone axe"
python cdda_item_search.py --search "木斧"
python cdda_item_search.py --search "火炉"

# 按主技能查 (制作后获得该技能经验)
python cdda_item_search.py --skill survival
python cdda_item_search.py --skill 生存
python cdda_item_search.py --skill 制造 --detail 0

# 统计 / 重建
python cdda_item_search.py --stats
python cdda_item_search.py --rebuild
```

### Web UI

```powershell
# 默认监听 http://127.0.0.1:5000
python run_web.py

# 自定义端口
python run_web.py --port 8080

# 局域网访问
python run_web.py --host 0.0.0.0

# 启动前重建索引
python run_web.py --rebuild
```

启动后浏览器访问 `http://127.0.0.1:5000/`,提供:
- 首页:常用搜索 + 按技能筛选 + 索引统计
- `/search?q=...` —— 物品 / 家具搜索结果
- `/skill?name=...` —— 按主技能筛选配方
- `/item/<id>` —— 物品详情 + 所有配方
- `/furniture/<id>` —— 家具/地形/电器详情
- `/recipe/<id>/<idx>` —— 某条配方的详情
- `/api/search?q=...` —— JSON API

## 用法详解

### CLI 命令

```
python cdda_item_search.py [选项]
  --search <查询词>       物品名搜索 (空格分隔 AND)
  --skill <技能>          按主技能筛选 (英文 id 或中文)
  --detail <N>            显示第 N 条结果的详情
  --rebuild               强制重建索引
  --stats                 显示索引统计
```

### 交互命令

| 命令 | 作用 |
|---|---|
| `<查询词>` | 物品 / 家具搜索 (空格分隔 AND, 支持中文) |
| `skill:<名>` | 按主技能查 (例: `skill:生存` / `skill:fabrication`) |
| `ids <id>` | 按 id 精确查找 (物品 / 家具) |
| `<数字>` | 上一条搜索结果中,选第 N 条查看详情 |
| `stats` | 显示统计 |
| `help` | 显示帮助 |
| `q` / `quit` | 退出 |

### 中文搜索

工具用三层数据源支持中文:

| 层级 | 来源 | 覆盖 |
|---|---|---|
| 内置词典 | `ITEM_ZH_NAMES` (~290 条) + `FURN_ZH_NAMES` (~30 条) + `SKILL_ZH_NAMES` (~25 条) | 斧/弓/弩/刀/锤/枪/火/水/医疗/工具/服装/弹药/书籍/火炉/烤箱/锻造/技能 |
| PO 正向映射 | `lang/po/zh_CN.po` (~37k 条翻译) | 显示物品 / 家具的中文名 (若 PO 翻译过) |
| PO 反向索引 | 中文 CJK 1/2/3-gram token (~235k 项) | 中文短语模糊匹配英文名 |

### Web UI

```powershell
# 默认监听 http://127.0.0.1:5000
python run_web.py

# 自定义端口
python run_web.py --port 8080

# 局域网访问
python run_web.py --host 0.0.0.0

# 启动前重建索引
python run_web.py --rebuild
```

启动后浏览器访问 `http://127.0.0.1:5000/`, 提供三个 tab 的工作流:

## 三 Tab 工作流

顶栏标签: **🔍 搜物品** · **⬆ 升级规划** · **📂 技能浏览**

### Tab 1: 搜物品 — `/items?q=...`

按物品名/中文名/别名搜索 (中英文混合 + 多关键词 AND):
- 同时搜物品 + 家具/地形
- 物品卡显示中文名 (若有)
- 支持 `?q=comp+bow` 等多关键词
- 常用 chip 一键搜索
- 旧路由 `/search?q=...` 仍可用,自动重定向

### Tab 2: 升级规划 — `/levelup?skill=<名>&from=0&to=1`

选技能 + 当前/目标等级,看**每个配方能涨多少 XP,升到下一级要多久**:
- 顶部信息卡片: 当前级、目标级、累计阈值 (D&D 5e)、还需 XP
- CDDA 经验公式说明 (XP = 制作时间(秒) / 100, cap = difficulty × 1.25)
- **逐级规划**: 每级独立展示最快 3 个配方 + 折叠材料/工具
- **全部配方速查表**: 每行显示 `单次 XP · 制造次数 · 总 XP · 总耗时 · 进度条 (% / 升)`
- 快捷 chip: 生存 0→1 / 制造 0→1 / 烹饪 0→1 / 急救 0→1 / 陷阱 0→2 / 弓术 0→3

### Tab 3: 技能/熟练度浏览 — `/browse?type=skill&q=<名>` 或 `?type=proficiency&q=<名>`

按主技能 **或** 熟练度 列出所有匹配配方:
- `type=skill`: 列出 `skill_used == <name>` 的所有配方 (制作后获得该技能经验)
- `type=proficiency`: 列出 `proficiencies contains <name>` 的所有配方 (需要该熟练度)
- **按难度分组**,同难度内按时间升序
- 每张卡片显示: 难度 / 耗时 / 自动/书 / 单次 XP / 需要的熟练度列表 / 材料 (折叠)
- **中文别名支持**: `锉工`→`prof_metalworking`、`锻造`→`prof_blacksmithing`、`弓术`→`prof_bowyery` 等
- 旧路由 `/skill?name=...` 重定向到 `/browse?type=skill&q=...`

## 详情 / API

- **`/item/<id>`** — 物品详情 + 所有配方
- **`/furniture/<id>`** — 家具/地形/电器详情
- **`/recipe/<id>/<idx>`** — 某条配方的详情
- **`/api/search?q=...`** — JSON API

## 路由汇总 (24 个端点)

| Tab | 路由 | 说明 |
|---|---|---|
| 1 | `/items?q=...` | 物品 + 家具搜索 |
| 2 | `/levelup?skill=...&from=0&to=1` | 升级规划 + XP 进度 |
| 3 | `/browse?type=skill&q=...` | 按主技能浏览 |
| 3 | `/browse?type=proficiency&q=...` | 按熟练度浏览 |
| - | `/item/<id>` | 物品详情 |
| - | `/furniture/<id>` | 家具详情 |
| - | `/recipe/<id>/<idx>` | 配方详情 |
| - | `/api/search?q=...` | JSON API |
| - | `/search?q=...` | 旧路由 → 重定向到 `/items` |
| - | `/skill?name=...` | 旧路由 → 重定向到 `/browse?type=skill&q=...` |

物品:
```
=== wood axe  [木斧]  (id = 'ax') ===
  类型 / 分类 / 重量 / 体积 / 材料 / 价格 / 末日价 / 描述 /
  关键 flags / 工具质量 / 弹药类型 / 来源文件
```

配方 (在物品详情中嵌套显示):
```
┌─ 配方 #0 ──
  类型 / 活动等级 / 难度 / 耗时
  ── 主要技能 (获得经验) ──
    skill_used: fabrication          ← 决定做完后哪个技能涨经验
  ── 所需技能等级 (门槛, 不直接获得经验) ──
    • archery Lv.1
  ── 熟练度要求 ──
    • prof_bowyery (必需)
  ── 解锁条件 ──
    ✓ 技能达阈值时自动学会
    书本学习: recipe_bows 第 1 章
  ── 制作材料 (components) ──
    • 1× stick  或 1× 2x4
  ── 工具集 (using) ──
    • 使用工具集 blacksmithing_standard (×12)
  ── 工具质量要求 (qualities) ──
    • CUT Lv.2
```

家具 / 地形:
```
=== wood stove  [木火炉]  (id = 'f_woodstove') ===
  类型:           FURN  (subtype=furniture)
  移动消耗 / 覆盖 / 交互动作 / 关键 flags
  描述: A simple metal stove for hosting wood-fueled fires...
  拆解可获得: metal_tank×1, pipe×1
  破坏可获得: scrap×[3, 6], pipe×?
  来源: data\json\furniture_and_terrain\furniture-fireplaces.json
```

## 按主技能查 (`--skill <name>` / `skill:<名>`)

返回所有 **主技能为该技能** 的配方 (主技能决定制作完成后哪个技能涨经验):

```
=== 制作可获得 'survival' 经验 的配方 (80 条) ===
  匹配的主技能 id: survival
  按难度升序

  [  0] Lv.  1        1 h ✓自动  'throwing stick'  (throwing_stick)
  [  1] Lv.  1        1 m ✓自动  'tinder'  (tinder)
  ...
```

技能查询支持英文 id (`survival`、`fabrication`) 与中文 (`生存`、`烹饪`、`急救`、`裁缝`、`机械`、`化学`、`电子`、`陷阱`、`弓术`、`近战`、`投掷` 等)。

## 文件结构

```
search_tools/
├── cdda_item_search.py     # CLI 入口 (薄包装)
├── run_web.py              # Web UI 启动器
├── cdda_search/            # 主包
│   ├── __init__.py         # 公共 API
│   ├── __main__.py         # python -m cdda_search 入口
│   ├── constants.py        # 路径、内置词典、CJK 辅助
│   ├── models.py           # Item/Recipe/Furniture dataclass + JSON 解析
│   ├── po.py               # 解析 lang/po/zh_CN.po
│   ├── database.py         # Database 类 + 索引构建 + 缓存
│   ├── search.py           # 搜索结果展示
│   ├── render.py           # 详细文本渲染 (CLI 用)
│   ├── cli.py              # argparse + 交互 REPL
│   ├── web.py              # Flask Blueprint
│   ├── templates/          # Jinja2 HTML
│   │   ├── base.html
│   │   ├── index.html
│   │   ├── search.html
│   │   ├── skill.html
│   │   └── detail.html
│   └── static/
│       └── style.css
├── README.md
├── .gitignore
└── .cache/                 # 自动生成的索引缓存
    ├── cdda_index.json     (~9 MB)  物品 + 家具 + 配方
    └── cdda_zh_index.json  (~9 MB)  PO 翻译 + 反向索引
```

## 性能

- 首次重建: ~2 秒 (扫 JSON + 解析 PO)
- 缓存命中启动: <0.2 秒
- Web 首次请求: ~0.7 秒 (含缓存加载)
- 后续请求: <100 ms
- 进程级数据库缓存 (`_db_cache`),Web 多次访问共享

## 已知限制

- **PO 不翻译物品 name 字段**,大部分中文物品名靠 PO 反查或内置词典
- **中文关键词过短** (如 "弓"、"箭") 容易匹配过宽, 配合限定词效果更好
- **部分中文表达在 PO 中不存在** (如 "急救包"、"燧发枪"),这些搜不到
- Web 开发服务器是 Flask 自带的,生产环境建议用 Gunicorn / uWSGI

## 测试样例

```powershell
# 英文
python cdda_item_search.py --search "stone axe"
python cdda_item_search.py --search "short bow"

# 中文
python cdda_item_search.py --search "木斧"
python cdda_item_search.py --search "火炉"      # 命中家具 f_woodstove
python cdda_item_search.py --search "生理盐水"
python cdda_item_search.py --search "床"        # 14 个家具 + 7 个物品

# 按主技能查
python cdda_item_search.py --skill survival --detail 5
python cdda_item_search.py --skill 制造         # 1769 条
python cdda_item_search.py --skill 烹饪         # 773 条
python cdda_item_search.py --skill 急救         # 17 条

# Web UI
python run_web.py
# 然后浏览器打开 http://127.0.0.1:5000/
```