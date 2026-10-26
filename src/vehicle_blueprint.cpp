#include "vehicle_blueprint.h"

#include <algorithm>
#include <exception>
#include <optional>
#include <string>
#include <vector>

#include "cata_utility.h"
#include "debug.h"
#include "filesystem.h"
#include "flexbuffer_json.h"
#include "json_loader.h"
#include "localized_comparator.h"
#include "map.h"
#include "path_info.h"
#include "point.h"
#include "vehicle.h"

namespace
{
// 蓝图的格式版本号，方便以后改结构时做兼容判断
constexpr int blueprint_format_version = 1;

std::string base_dir()
{
    return PATH_INFO::base_path() + "vehicle_blueprints";
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

vehicle *spawn_at( const std::string &name, const tripoint &p )
{
    if( name.empty() ) {
        return nullptr;
    }
    const std::string path = path_of( name );
    if( !file_exist( path ) ) {
        return nullptr;
    }

    // 蓝图目录是玩家手工放文件的地方，格式写错不能把游戏崩掉（JsonError 会直接
    // std::terminate）。整个解析过程都兜住，失败一律返回 nullptr，由调用方弹
    // 「蓝图可能已损坏」的提示。
    try {
        const std::string data = read_entire_file( path );
        if( data.empty() ) {
            return nullptr;
        }

        std::optional<JsonValue> jv = json_loader::from_string_opt( data );
        if( !jv ) {
            DebugLog( D_ERROR, D_MAIN ) << "blueprint " << name << ": not valid json";
            return nullptr;
        }
        if( !jv->test_object() ) {
            // 最常见的手滑：把 data/json/vehicles/ 下的载具模板整个拷进来了。
            // 模板顶层是数组（一个文件可以放多辆车），跟导出的蓝图不是同一种格式。
            DebugLog( D_ERROR, D_MAIN ) << "blueprint " << name
                           << ": root is not an object (looks like a vehicle template)";
            return nullptr;
        }
        const JsonObject jo = jv->get_object();
        if( !jo.has_object( "vehicle" ) ) {
            DebugLog( D_ERROR, D_MAIN ) << "blueprint " << name << ": missing \"vehicle\"";
            return nullptr;
        }
        const JsonObject vdata = jo.get_object( "vehicle" );
        if( !vdata.has_array( "parts" ) ) {
            DebugLog( D_ERROR, D_MAIN ) << "blueprint " << name << ": missing \"parts\"";
            return nullptr;
        }

        // 整辆车的落位 / 碰撞检查 / 地图缓存注册全部交给 map::add_vehicle_from_json，
        // 这里只负责把 JSON 解析出来。地图里现存的载具一辆都不会被碰。
        return get_map().add_vehicle_from_json( vdata, p );
    } catch( const std::exception &err ) {
        DebugLog( D_ERROR, D_MAIN ) << "blueprint " << name << ": " << err.what();
        return nullptr;
    }
}

} // namespace vehicle_blueprint
