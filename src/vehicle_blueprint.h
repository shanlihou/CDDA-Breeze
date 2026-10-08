#pragma once
#ifndef CATA_SRC_VEHICLE_BLUEPRINT_H
#define CATA_SRC_VEHICLE_BLUEPRINT_H

#include <string>
#include <vector>

class vehicle;

/**
 * 载具蓝图：把一整辆车（部件、油箱、货物、标签、loot 区域、名称……）
 * 导出成一个 JSON 文件，之后可以再从 JSON 原地恢复。
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
 * 用 dir()/name.json 原地恢复 veh：
 * 部件 / 货物 / 名称 / 标签 / 区域全部替换为文件里的状态，
 * 但**保留车辆当前所在位置**（不会把车搬走），
 * 并把速度、牵引、自动驾驶等运行时状态归零，避免带进来一个"正在飙车"的状态。
 */
bool import_from( vehicle &veh, const std::string &name );

} // namespace vehicle_blueprint

#endif // CATA_SRC_VEHICLE_BLUEPRINT_H
