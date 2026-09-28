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
 *   - http 与 https（由 WEBDAV_URL 的 scheme 决定）
 *   - Basic 与 Digest 认证（WEBDAV_AUTH_MODE 选择），默认交给 libcurl 自动协商
 *   - 目录自动创建（MKCOL）、列表（PROPFIND）、上传（PUT）、下载（GET）、删除（DELETE）
 */
namespace webdav
{

// ============================================================================
//                          配 置 区（改完重新编译即可）
// ============================================================================

/// 服务器地址，http / https 都支持，末尾的 "/" 可有可无。
/// 例：
///   "https://dav.jianguoyun.com/dav/"   坚果云
///   "https://192.168.1.10:5244/dav/"    自建 alist + https
///   "http://192.168.1.10:5244/dav/"     自建 alist + http
///   "http://127.0.0.1:8080/dav/"        局域网
constexpr const char *WEBDAV_URL = "https://dav.jianguoyun.com/dav/";

/// 用户名，留空表示不发送认证信息
constexpr const char *WEBDAV_USER = "your_account";

/// 密码（坚果云等请填"应用密码"而不是登录密码）
constexpr const char *WEBDAV_PASSWORD = "your_password";

/// 服务器上的备份目录（相对 WEBDAV_URL），留空表示直接放在根目录。
/// 程序会自动逐级创建，支持中文与多级目录，例如 "游戏存档/CDDA"。
constexpr const char *WEBDAV_REMOTE_DIR = "CDDA_Breeze";

/// 认证方式：
///   0 = 自动：libcurl 自动协商，优先 Digest，其次 Basic（推荐，兼容性最好）
///   1 = 只用 Digest
///   2 = 只用 Basic
constexpr int WEBDAV_AUTH_MODE = 0;

/// https 是否校验服务器证书。自签名证书、局域网 IP 证书请保持 false。
constexpr bool WEBDAV_VERIFY_SSL = false;

/// 连接超时（秒）；传输超时（秒），0 表示不限制（大存档上传可能需要很久）
constexpr long WEBDAV_CONNECT_TIMEOUT = 15;
constexpr long WEBDAV_TRANSFER_TIMEOUT = 0;

/// 云端最多保留的备份数量，超出后自动删除最旧的。0 = 不限
constexpr int WEBDAV_MAX_KEEP = 0;

// ============================================================================
//                                 接 口
// ============================================================================

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
