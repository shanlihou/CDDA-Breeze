# Hentai Mod Reloaded（CDDA-Breeze 移植版）参考与维护指南

> 本文档定位为**事实性参考 / 维护手册**，面向 mod 的开发者与维护者。
> 内容覆盖：mod 身份与依赖、启用方式、文件架构、系统清单、与 CDDA-Breeze 的兼容层、
> 以及 `--check-mods` 一致性验证。
>
> **内容边界声明**：本 mod 含成人色情内容，且其中部分实体/角色为未成年人形象
> （如 `mon_corrupted_schoolgirl` / `corrupted_schoolboy` / `TALK_DD_CHILDREN` 等）。
> 出于内容安全规范，本文档**不提供**任何针对未成年人形象的玩法说明，也**不展开**
> 成人角色之间的露骨互动步骤。如需这部分信息，请直接阅读对应 `json/` 源文件。

---

## 1. 概述

| 项 | 内容 |
| --- | --- |
| 主 mod id | `mod_hentai_reloaded_jpv_r18_port` |
| 主 mod 目录 | `data/mods/hentai-r18-h/` |
| 显示名 | 追加 - Hentai Mod Reloaded（variant_jp r18beta 移植版） |
| 版本 | 0.3 |
| 作者 / 维护 | 溜まり場の変態&有志、lispcoc、roloa、m_kw、kitayama（原作者）；cknight828（移植维护） |
| 依赖 | `dda`（基础游戏） |
| 平衡补丁 mod | `mod_hentai_balance`（`data/mods/hentai_balance/`，依赖主 mod） |
| 关联 mod | `cubi_no_upgrade`（`data/mods/cubi_no_upgrade/`，抑制 CUBI 升级） |

主 mod 在 `modinfo.json` 的自述中建议：**进入游戏后优先打开「自带特性」进行设定**——
即角色创建阶段可在特质/职业里选取本 mod 提供的内容。

> 提示：`android` 构建产物里会出现 `data/cache/mods/...` 与 `data/mods/...` 两份，
> 那是打包缓存，实际源在 `data/mods/` 下，改源即可，勿改 `cache/`。

---

## 2. 启用方式（技术性）

1. 新建世界（或编辑世界）时，在 **Mods** 列表里依次勾选：
   - `追加 - Hentai Mod Reloaded(...)`（主 mod）
   - `变更 - Hentai Mod Reloaded 平衡调整`（平衡补丁，可选但推荐用于稳定版难度适配）
2. 角色创建时选取本 mod 提供的**职业 / 特质 / 变异**（见第 5 节）。
3. 进入游戏后按 mod 自述先完成「自带特性」设定。

> 依赖顺序由 `modinfo.json` 的 `dependencies` 保证：平衡补丁依赖主 mod，主 mod 依赖 `dda`。
> 缺依赖时世界创建会报错。

---

## 3. 与 CDDA-Breeze 的兼容层

本 fork 为让 0.H 系 mod（含本 mod）**不改 JSON 直接跑**，在引擎侧实现了一套兼容层。
运行 `hentai-r18-h` 时实际依赖以下已实现的机制（详见 `src/math_exp.*`、`src/condition.*`、
`src/effect_on_condition.*` 等）：

- **`math` 表达式引擎**：`eocs.json` 中大量使用 `math: [ ... ]` 做变量运算。
- **`if / then / else` 条件分支** 与 **`context_val` / `var_val` / `global_val`** 变量作用域。
- **事件总线**：`eoc_type: "EVENT"` + `required_event`（如 `character_finished_activity`、
  `game_avatar_new`），本 mod 的 `EOC_ACT_VIBE_STATISTIC` 即依赖此。
- **`run_eocs` / `u_query_tile` / `u_run_inv_eocs` / `location_variable_adjust` / `set_condition`** 等 EOC 指令。
- **`ITEM` / `monster_flag` / `str_sp` / `see_cost` 符号名 / `resist` 嵌套材质** 等数据字段。
- **附魔 `RANGE` / `RANGED_DAMAGE` / `AVOID_FRIENDRY_FIRE`**。

> 新增条件键时**必须**同步登记到 `dialogue_data::complex_conds` / `simple_string_conds`
> （`src/condition.h`），否则 `conditional_t` 的二次校验会报 `unrecognized condition`。
> 这些是兼容层对 0.H JSON 的兜底，不是主 mod 本身的 bug。

---

## 4. 模块架构（文件地图）

`hentai-r18-h/json/` 与 `append/`、`extend_json/` 构成全部内容（合计约 1.35 万行）：

| 子系统 | 关键文件 | 说明 |
| --- | --- | --- |
| 效果 Effects | `json/effects.json` | `lust`（情欲）、`corrupt`（堕落）、`after_orgasm`、`finished_onani` 等状态效果 |
| 核心逻辑 EOC | `json/eocs.json` | 情欲/高潮流程、统计、`spell_hentai_get_monster_id` 怪物种族识别法术集 |
| 怪物 Monsters | `json/monsters/monsters.json`（~821 行）、`monstergroups.json`、`monster_drops.json`、`monsters_override.json` | 恶魔系（succubi / incubi / cambion）、山羊头恶魔、藤兽等 |
| 怪物特攻 | `json/monster_special_attacks/*` | `LOVE_FLAME`、`SEDUCE`、`THROW_KISS`、`STRIP_U`、`wife_u`、`expose`、`parrot_naked` 等 |
| 物品 Items | `json/items/*`、`json/items/item_actions/*` | 书籍、工具（按摩器 `ACT_VIBE`）、食物、护甲、容器；动作 `anthropomorph` / `make_pet` / `yiff` / `spawn_artifact` |
| 变异 Mutations | `json/mutations/*`、`append/mutations/*` | `CUBI` 物种、`FIEND`（恶魔）类别、`estrus`（发情）、`pregnancy`（怀孕，`append/pregnancy.json` ~502 行）、维生素 |
| NPC | `json/npcs/*` | 职业 `classes.json`、特质组 `trait_groups.json`、任务 `missiondef.json`、对话树（含 anthro cubi / pet）、`NC_DEMONBEING_SCHOOLGIRL` |
| 对话/梦境 | `json/chat_cubi.json`、`json/dreams.json`、`append/talk_sex.json`、`append/wet_dream.json` | NPC 对话与梦境脚本 |
| 地图 Mapgen | `json/mapgen/corrupted_school.json`、`corrupted_school_roof.json`、`json/overmap/*` | 「腐化学校」等专属地宫与 overmap specials |
| 配方/材料 | `json/recipes.json`、`json/materials.json`、`json/harvest.json` | 本 mod 新增配方与材料 |
| 职业 Professions | `json/professions.json`、`append/professions.json` | 「一人一狗/一猫/一熊崽」等起始职业（含变异药剂） |
| 统计/陷阱/物种 | `json/statistics.json`、`json/traps.json`、`json/species.json` | 统计项、陷阱、物种定义 |

> 平衡补丁 `hentai_balance/` 内容很少（单层目录），通过覆盖/调整数值来适配稳定版难度。

---

## 5. 角色创建（事实性清单）

- **职业（Professions）**：`json/professions.json` 提供「一人一狗 / 一人一猫 / 一人一熊崽」等，
  起始携带变异药剂（`iv_mutagen_human` + 注射器）与宠物，用于快速进入本 mod 的变异线。
- **变异类别（Mutation Category）**：`FIEND`（恶魔）阈值变异 `THRESH_FIEND`，
  对应维生素 `mutagen_fiend`；另有 `CUBI` 物种线（魅魔/ incubi 相关）。
- **特质组（Trait Groups）**：`json/npcs/trait_groups.json` 提供可选取的特质集合，
  具体内容见源文件（按 mod 自述「优先打开自带特性」选取）。

---

## 6. 主要系统概览（高层、非露骨）

- **情欲 / 堕落系统**：`lust` 效果随交互累积，满强度触发 `after_orgasm` 与体液物品生成；
  `corrupt` 效果有智力对抗（`u_val('intelligence')`）的抵抗判定；`SEXUAL_SENSITIVE` 旗帜使
  情欲时长翻倍。相关统计项 `reached_orgasm` 由 `EOC_ACT_VIBE_STATISTIC` 等累加。
- **怀孕 / 发情系统**：`append/pregnancy.json`、`append/estrus.json` 实现发情周期与怀孕流程，
  走 EOC + 维生素驱动。
- **怪物生态**：以恶魔/魅魔/incubi 家族为主，配专属特殊攻击；掉落见 `monster_drops.json`，
  刷新组见 `monstergroups.json`。
- **物品与动作**：除常规书籍/工具/食物/护甲外，`item_actions` 提供变身（anthropomorph）、
  造宠（make_pet）、召唤神器（spawn_artifact）等功能性动作。
- **地宫**：`corrupted_school` 系列 mapgen 提供专属探索场景，由 overmap specials 接入世界。

> 涉及**未成年人形象**的实体（`*_schoolgirl` / `*_schoolboy` / `children` 相关）以及成人之间的
> 露骨互动步骤，**本文档不做说明**，请直接查阅 `json/monsters/`、`json/npcs/`、`append/talk_sex.json`
> 等源文件自行评估。

---

## 7. 维护与一致性验证

日常维护（尤其改了主 mod 的 JSON 或动了兼容层）后，务必跑：

```bash
# 无头校验，只报第一个错误；输出 "check-mods: DONE" 即全过
out/dist/cataclysm-tiles.exe --check-mods mod_hentai_reloaded_jpv_r18_port
# 平衡补丁一起验
out/dist/cataclysm-tiles.exe --check-mods mod_hentai_balance
```

- 校验依赖 `set_headless_mode(true)`，否则会弹阻塞式错误框。
- 传错 mod id 会报 `Unknown mod`，属用法错误而非回归。
- 若改动触及兼容层（`src/` 的 math / condition / eoc 等），需要同步在 Windows / Android / macOS
  三端回归（参见 `build_win.bat` / `build_android.bat` / `build_mac.sh`）。
- 改了 `data/` 后 **Android 端必须 +1 `DATA_REVISION`**（`SplashScreen.java`），否则设备沿用旧 data，
  表现为 JSON/键位改动不生效。

---

## 8. 排错速查

| 现象 | 可能原因 | 处理 |
| --- | --- | --- |
| 世界创建报缺依赖 | 未同时勾选主 mod / `dda` | 按第 2 节顺序勾选 |
| `--check-mods` 报 `Unknown mod` | id 拼错 | 用 `mod_hentai_reloaded_jpv_r18_port` / `mod_hentai_balance` |
| 报 `unrecognized condition` | 兼容层新增条件键未登记 `complex_conds`/`simple_string_conds` | 在 `src/condition.h` 补登记 |
| Android 改 data 不生效 | 未 +1 `DATA_REVISION` | 改 `SplashScreen.java` 常量 |
| 三端行为不一致 | 只编了一端 | 三端都回归构建 |

---

*文档基于 `data/mods/hentai-r18-h` 与 `data/mods/hentai_balance` 实际源文件整理（v0.3）。
内容安全边界见顶部声明。*
