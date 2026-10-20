#pragma once
#ifndef CATA_SRC_VEHICLE_BLUEPRINT_H
#define CATA_SRC_VEHICLE_BLUEPRINT_H

#include <string>
#include <vector>

#include "point.h"

class vehicle;

/**
 * 载具蓝图：把一整辆车（部件、油箱、货物、标签、loot 区域、名称……）
 * 导出成一个 JSON 文件，之后可以再从 JSON 生成一辆同样的车。
 *
 * 底层直接复用存档用的 vehicle::serialize / vehicle::deserialize，
 * 所以保真度和存档完全一致（包括油箱余量、部件损伤、货物里的物品）。
 *
 * 文件放在游戏根目录的 vehicle_blueprints/ 下，跨存档通用。
 */
namespace vehicle_blueprint
{

/** 蓝图存放目录（导出时才会被创建） */
std::string dir();

/** 蓝图的完整文件路径（自动补 .json 后缀） */
std::string path_of( const std::string &name );

/** 列出已有蓝图名（不含 .json 后缀），按本地化字典序排序 */
std::vector<std::string> list();

/** 把 veh 完整导出为 dir()/name.json */
bool export_to( const vehicle &veh, const std::string &name );

/**
 * 用 dir()/name.json 在 p 处生成一辆**全新的**载具。
 *
 * 注意：这里**不会**去改动任何已经存在的载具——旧版本是拿蓝图直接 deserialize
 * 覆盖当前正在查看的那辆车，实测会导致崩溃（原地换零件的副作用太多：
 * 地图侧缓存、货物引用、牵引状态、部件指针全都悬空）。
 * 现在改成"新建一辆放到地上"，由玩家自己保证落点足够空旷。
 *
 * 返回新载具，失败（文件损坏 / 没有部件 / 落点被占）返回 nullptr。
 */
vehicle *spawn_at( const std::string &name, const tripoint &p );

} // namespace vehicle_blueprint

#endif // CATA_SRC_VEHICLE_BLUEPRINT_H
