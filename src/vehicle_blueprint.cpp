#include "vehicle_blueprint.h"

#include <algorithm>
#include <exception>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "calendar.h"
#include "cata_utility.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "json_loader.h"
#include "localized_comparator.h"
#include "map.h"
#include "path_info.h"
#include "point.h"
#include "units.h"
#include "vehicle.h"

namespace
{
// 蓝图的格式版本号，方便以后改结构时做兼容判断
constexpr int blueprint_format_version = 1;

std::string base_dir()
{
    return PATH_INFO::base_path() + "vehicle_blueprints";
}

// 反序列化失败时把车恢复到快照里的样子（含位置）
void rollback_to( vehicle &veh, const std::string &snapshot )
{
    std::optional<JsonValue> jv = json_loader::from_string_opt( snapshot );
    if( !jv ) {
        return;
    }
    try {
        veh.deserialize( jv->get_object() );
    } catch( const JsonError & ) {
        // 快照本身就是从这辆车导出来的，理论上不会失败
        return;
    }
    veh.refresh();
    map &here = get_map();
    here.rebuild_vehicle_level_caches();
    here.set_transparency_cache_dirty( veh.sm_pos.z );
}
} // namespace

namespace vehicle_blueprint
{

std::string dir()
{
    return base_dir();
}

std::string path_of( const std::string &name )
{
    return base_dir() + "/" + name + ".json";
}

std::vector<std::string> list()
{
    std::vector<std::string> res;
    if( !dir_exist( base_dir() ) ) {
        return res;
    }
    for( const std::string &full : get_files_from_path( ".json", base_dir(), false, true ) ) {
        const std::size_t slash = full.find_last_of( "/\\" );
        std::string fn = slash == std::string::npos ? full : full.substr( slash + 1 );
        if( fn.size() > 5 && fn.compare( fn.size() - 5, 5, ".json" ) == 0 ) {
            fn.erase( fn.size() - 5 );
            res.push_back( fn );
        }
    }
    std::sort( res.begin(), res.end(), localized_compare );
    return res;
}

bool export_to( const vehicle &veh, const std::string &name )
{
    if( name.empty() ) {
        return false;
    }
    if( !assure_dir_exist( base_dir() ) ) {
        return false;
    }
    return write_to_file( path_of( name ), [&]( std::ostream &fout ) {
        JsonOut jsout( fout, true ); // pretty print
        jsout.start_object();
        jsout.member( "format_version", blueprint_format_version );
        jsout.member( "vehicle_name", veh.name );
        jsout.member( "vehicle" );
        veh.serialize( jsout );
        jsout.end_object();
    }, "vehicle blueprint" );
}

bool import_from( vehicle &veh, const std::string &name )
{
    if( name.empty() ) {
        return false;
    }
    const std::string path = path_of( name );
    if( !file_exist( path ) ) {
        return false;
    }

    std::string data;
    try {
        data = read_entire_file( path );
    } catch( const std::exception & ) {
        return false;
    }
    if( data.empty() ) {
        return false;
    }

    std::optional<JsonValue> jv = json_loader::from_string_opt( data );
    if( !jv ) {
        return false;
    }
    const JsonObject jo = jv->get_object();
    if( !jo.has_object( "vehicle" ) ) {
        return false;
    }
    const JsonObject vdata = jo.get_object( "vehicle" );
    if( !vdata.has_array( "parts" ) ) {
        return false;
    }

    // 位置不跟着蓝图走：先记下当前位置，替换完内容后再塞回去
    const point keep_pos = veh.pos;
    const tripoint keep_sm_pos = veh.sm_pos;
    const int keep_om_id = veh.om_id;

    // 先给自己拍一份快照（含当前位置），解析失败时可以整辆回滚
    const std::string snapshot = serialize_wrapper( [&]( JsonOut & jsout ) {
        veh.serialize( jsout );
    } );

    // 蓝图里可能引用了当前存档里不存在的零件 / 物品，解析失败时回滚成快照
    try {
        veh.deserialize( vdata );
    } catch( const JsonError & ) {
        rollback_to( veh, snapshot );
        return false;
    }

    // 位置和朝向不跟着蓝图走：车留在原地，只替换内容
    veh.pos = keep_pos;
    veh.sm_pos = keep_sm_pos;
    veh.om_id = keep_om_id;

    // 运行时状态归零：别把一个"正在 80mph 行驶 / 被牵引 / 自动驾驶 / 坠落"的状态带过来
    veh.velocity = 0;
    veh.avg_velocity = 0;
    veh.cruise_velocity = 0;
    veh.vertical_velocity = 0;
    veh.skidding = false;
    veh.is_falling = false;
    veh.engine_on = false;
    veh.tracking_on = false;
    veh.autopilot_on = false;
    veh.is_autodriving = false;
    veh.is_following = false;
    veh.is_patrolling = false;
    veh.is_AUTOPILOT_fly = false;
    veh.of_turn_carry = 0.0f;
    veh.last_turn = 0_degrees;
    veh.turn_dir = veh.face.dir();
    veh.last_update = calendar::turn;
    veh.invalidate_towing( true );
    veh.tow_data.other_towing_point = tripoint_min;

    veh.refresh();

    // 部件数量/外形可能变了，重建地图侧的载具缓存，避免残留指向已消失部件的索引
    map &here = get_map();
    here.rebuild_vehicle_level_caches();
    here.set_transparency_cache_dirty( veh.sm_pos.z );

    return true;
}

} // namespace vehicle_blueprint
