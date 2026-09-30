#pragma once
#ifndef CATA_SRC_WEBDAV_BACKUP_H
#define CATA_SRC_WEBDAV_BACKUP_H

#include <functional>
#include <string>
#include <vector>

/**
 * 存档的 WebDAV 备份 / 恢复。
 *
 * 协议支持：
 *   - http 与 https（由「使用 HTTPS」设置决定）
 *   - Basic 与 Digest 认证（由「使用 Digest 认证」设置决定）
 *   - 目录自动创建（MKCOL）、列表（PROPFIND）、上传（PUT）、下载（GET）、删除（DELETE）
 *
 * 服务器相关配置全部是游戏设置项，玩家在「设置 → WebDAV云存档」里自己填，
 * 保存在 config/options.json，改完立刻生效，不需要重新编译。
 */
namespace webdav
{

// ============================================================================
//                 设 置 项 键 名（对应「设置 → WebDAV云存档」）
// ============================================================================

namespace opt
{
/// 服务器地址，不含协议，例如 dav.jianguoyun.com 或 192.168.1.10:5244
constexpr const char *HOST = "WEBDAV_HOST";
/// 服务器上的备份目录，例如 dav/CDDA_Breeze，留空表示根目录
constexpr const char *PATH = "WEBDAV_PATH";
/// 是否用 https（false = http）
constexpr const char *USE_HTTPS = "WEBDAV_USE_HTTPS";
/// 用户名，留空表示不发送认证信息
constexpr const char *USER = "WEBDAV_USER";
/// 密码
constexpr const char *PASSWORD = "WEBDAV_PASSWORD";
/// 是否用 Digest 认证（false = Basic）
constexpr const char *AUTH_DIGEST = "WEBDAV_AUTH_DIGEST";
} // namespace opt

// ---------------------------------------------------------------------------
//                        内 部 参 数（一般不用改）
// ---------------------------------------------------------------------------

/// 连接超时（秒）
constexpr long WEBDAV_CONNECT_TIMEOUT = 15;
/// 传输超时（秒），0 表示不限制（大存档上传可能需要很久）
constexpr long WEBDAV_TRANSFER_TIMEOUT = 0;
/// https 是否校验服务器证书。自签名证书、局域网 IP 证书保持 false
constexpr bool WEBDAV_VERIFY_SSL = false;
/// 云端最多保留的备份数量，超出后自动删除最旧的。0 = 不限
constexpr int WEBDAV_MAX_KEEP = 0;

// ============================================================================
//                                 接 口
// ============================================================================

/// 是否已在设置里填好服务器地址
bool is_configured();

/// 由设置项拼出的备份目录 URL（末尾一定带 "/"）；未配置时返回空串
std::string remote_base_url();

/// 云端上的一个文件
struct remote_file {
    std::string name;      ///< 文件名（已 URL 解码）
    std::string href;      ///< 可直接请求的完整 URL
    long long size = 0;    ///< 字节数，未知为 -1
    std::string mtime;     ///< 服务器返回的修改时间
    bool is_dir = false;
};

/// 传输进度回调：now / total 均为字节数，total 可能为 0（未知）
using progress_cb = std::function<void( long long now, long long total )>;

/// 测试连接与认证是否正常，失败时 err 写入原因
bool test_connection( std::string &err );

/// 列出备份目录中的文件（跳过目录），按名称倒序（新的在前）
bool list_files( std::vector<remote_file> &out, std::string &err );

/// 上传本地文件到备份目录，remote_name 只写文件名
bool upload_file( const std::string &local_path, const std::string &remote_name,
                  std::string &err, progress_cb cb = nullptr );

/// 下载远端文件到 local_path
bool download_file( const remote_file &file, const std::string &local_path,
                    std::string &err, progress_cb cb = nullptr );

/// 删除云端文件，remote_name 只写文件名
bool delete_file( const std::string &remote_name, std::string &err );

/// 把整个世界目录打包成 tar.gz，返回压缩包绝对路径；失败返回空串
std::string pack_world( const std::string &world_name, std::string &err );

/// 把 tar.gz 解包到存档目录。restored 可选，接收被恢复的世界名。
/// overwrite=true 覆盖同名文件，false 则保留本地已有的文件（不会删除任何东西）
bool unpack_archive( const std::string &archive_path, std::string &err,
                     std::vector<std::string> *restored = nullptr, bool overwrite = true );

/// 完整备份流程：打包 → 上传 → （可选）清理旧备份
/// @param world_name savedir 下的世界目录名
/// @param cb 进度回调
bool backup_world( const std::string &world_name, std::string &err, progress_cb cb = nullptr );

/// 完整恢复流程：下载 → 解包，restored 接收恢复出来的世界名
bool restore_world( const remote_file &file, std::string &err,
                    std::vector<std::string> *restored = nullptr, progress_cb cb = nullptr );

/// 打开"WebDAV 云存档"菜单。
/// @param in_game 是否在游戏中（游戏中会先存档，且恢复后不刷新世界列表）
void show_menu( bool in_game = false );

} // namespace webdav

#endif // CATA_SRC_WEBDAV_BACKUP_H
