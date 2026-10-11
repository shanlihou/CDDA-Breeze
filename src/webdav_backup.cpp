#include "webdav_backup.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#include <curl/curl.h>
#include <zlib.h>

#include "cursesdef.h"
#include "color.h"
#include "filesystem.h"
#include "game.h"
#include "options.h"
#include "output.h"
#include "path_info.h"
#include "string_formatter.h"
#include "tgz_archiver.h"
#include "translations.h"
#include "ui.h"
#include "worldfactory.h"

// ---------------------------------------------------------------------------
// 小工具
// ---------------------------------------------------------------------------
namespace
{

std::string trim_str( const std::string &s )
{
    const char *ws = " \t\r\n";
    const size_t b = s.find_first_not_of( ws );
    if( b == std::string::npos ) {
        return std::string();
    }
    const size_t e = s.find_last_not_of( ws );
    return s.substr( b, e - b + 1 );
}

std::string to_lower( std::string s )
{
    std::transform( s.begin(), s.end(), s.begin(), []( unsigned char c ) {
        return static_cast<char>( std::tolower( c ) );
    } );
    return s;
}

bool starts_with( const std::string &s, const std::string &p )
{
    return s.size() >= p.size() && s.compare( 0, p.size(), p ) == 0;
}

int hex_val( char c )
{
    if( c >= '0' && c <= '9' ) {
        return c - '0';
    }
    if( c >= 'a' && c <= 'f' ) {
        return c - 'a' + 10;
    }
    if( c >= 'A' && c <= 'F' ) {
        return c - 'A' + 10;
    }
    return -1;
}

/// URL 编码（保留 / 之外的路径分隔符由调用方控制，这里按整段编码）
std::string url_encode( const std::string &s )
{
    static const char *hex = "0123456789ABCDEF";
    std::string out;
    out.reserve( s.size() * 2 );
    for( unsigned char c : s ) {
        const bool unreserved = ( c >= 'A' && c <= 'Z' ) || ( c >= 'a' && c <= 'z' ) ||
                                ( c >= '0' && c <= '9' ) || c == '-' || c == '_' || c == '.' || c == '~';
        if( unreserved ) {
            out += static_cast<char>( c );
        } else {
            out += '%';
            out += hex[( c >> 4 ) & 0xF];
            out += hex[c & 0xF];
        }
    }
    return out;
}

std::string url_decode( const std::string &s )
{
    std::string out;
    out.reserve( s.size() );
    for( size_t i = 0; i < s.size(); i++ ) {
        if( s[i] == '%' && i + 2 < s.size() ) {
            const int hi = hex_val( s[i + 1] );
            const int lo = hex_val( s[i + 2] );
            if( hi >= 0 && lo >= 0 ) {
                out += static_cast<char>( ( hi << 4 ) | lo );
                i += 2;
                continue;
            }
        }
        out += s[i];
    }
    return out;
}

/// 设置里的服务器地址，去掉误填的协议前缀与首尾空白
std::string opt_host()
{
    std::string h = trim_str( get_option<std::string>( webdav::opt::HOST ) );
    const std::string lower = to_lower( h );
    if( starts_with( lower, "https://" ) ) {
        h = h.substr( 8 );
    } else if( starts_with( lower, "http://" ) ) {
        h = h.substr( 7 );
    }
    while( !h.empty() && h.back() == '/' ) {
        h.pop_back();
    }
    return trim_str( h );
}

/// 服务器根地址：scheme://host（末尾不带 "/"）；未配置时返回空串
std::string base_url_from_options()
{
    const std::string host = opt_host();
    if( host.empty() ) {
        return std::string();
    }
    const bool https = get_option<bool>( webdav::opt::USE_HTTPS );
    return std::string( https ? "https://" : "http://" ) + host;
}

/// 取 URL 的 scheme://host 部分
std::string scheme_host( const std::string &url )
{
    const size_t p = url.find( "://" );
    if( p == std::string::npos ) {
        return std::string();
    }
    const size_t q = url.find( '/', p + 3 );
    return q == std::string::npos ? url : url.substr( 0, q );
}

std::vector<std::string> split_dir( const std::string &path )
{
    std::vector<std::string> out;
    for( size_t i = 0; i < path.size(); ) {
        size_t p = path.find_first_of( "/\\", i );
        if( p == std::string::npos ) {
            p = path.size();
        }
        const std::string seg = trim_str( path.substr( i, p - i ) );
        if( !seg.empty() ) {
            out.push_back( seg );
        }
        i = p + 1;
    }
    return out;
}

/// 设置里的备份目录切成的各段，已剔除空段与 "." / ".."
std::vector<std::string> path_segments_from_options()
{
    std::vector<std::string> segs = split_dir( get_option<std::string>( webdav::opt::PATH ) );
    segs.erase( std::remove_if( segs.begin(), segs.end(), []( const std::string &s ) {
        return s == "." || s == "..";
    } ), segs.end() );
    return segs;
}

std::string timestamp_str()
{
    const std::time_t t = std::time( nullptr );
    std::tm tm{};
#if defined( _WIN32 )
    localtime_s( &tm, &t );
#else
    localtime_r( &t, &tm );
#endif
    char buf[32];
    std::strftime( buf, sizeof( buf ), "%Y%m%d-%H%M%S", &tm );
    return std::string( buf );
}

std::string human_size( long long s )
{
    static const char *unit[] = { "B", "KB", "MB", "GB" };
    double d = static_cast<double>( s );
    int i = 0;
    while( d >= 1024.0 && i < 3 ) {
        d /= 1024.0;
        i++;
    }
    char buf[64];
    std::snprintf( buf, sizeof( buf ), "%.1f %s", d, unit[i] );
    return std::string( buf );
}

/// 临时目录：用户目录下，避开 save 目录以免被当成世界
std::string tmp_dir()
{
    const std::string d = PATH_INFO::user_dir() + "webdav_tmp/";
    fs::create_directories( fs::u8path( d ) );
    return d;
}

std::string tmp_path( const std::string &name )
{
    return tmp_dir() + name;
}

// ---------------------------------------------------------------------------
// curl 封装
// ---------------------------------------------------------------------------

std::once_flag curl_once;

void ensure_curl()
{
    std::call_once( curl_once, []() {
        curl_global_init( CURL_GLOBAL_DEFAULT );
    } );
}

size_t discard_write( char *, size_t size, size_t nmemb, void * )
{
    return size * nmemb;
}

size_t string_write( char *ptr, size_t size, size_t nmemb, void *user )
{
    std::string *s = static_cast<std::string *>( user );
    s->append( ptr, size * nmemb );
    return size * nmemb;
}

/// 简单 RAII 包装，避免泄漏 handle 与 header 链表
struct easy_handle {
    CURL *h = nullptr;
    curl_slist *headers = nullptr;

    easy_handle() = default;
    easy_handle( const easy_handle & ) = delete;
    easy_handle &operator=( const easy_handle & ) = delete;

    ~easy_handle() {
        if( headers ) {
            curl_slist_free_all( headers );
        }
        if( h ) {
            curl_easy_cleanup( h );
        }
    }
};

bool init_handle( easy_handle &eh, const std::string &url, std::string &err )
{
    eh.h = curl_easy_init();
    if( !eh.h ) {
        err = _( "无法初始化网络组件" );
        return false;
    }
    curl_easy_setopt( eh.h, CURLOPT_URL, url.c_str() );
    curl_easy_setopt( eh.h, CURLOPT_FOLLOWLOCATION, 1L );
    curl_easy_setopt( eh.h, CURLOPT_CONNECTTIMEOUT, webdav::WEBDAV_CONNECT_TIMEOUT );
    if( webdav::WEBDAV_TRANSFER_TIMEOUT > 0 ) {
        curl_easy_setopt( eh.h, CURLOPT_TIMEOUT, webdav::WEBDAV_TRANSFER_TIMEOUT );
    }
    if( !webdav::WEBDAV_VERIFY_SSL ) {
        // 自签名证书 / 局域网 IP 证书
        curl_easy_setopt( eh.h, CURLOPT_SSL_VERIFYPEER, 0L );
        curl_easy_setopt( eh.h, CURLOPT_SSL_VERIFYHOST, 0L );
    }
    const std::string user = trim_str( get_option<std::string>( webdav::opt::USER ) );
    if( !user.empty() ) {
        const std::string password = get_option<std::string>( webdav::opt::PASSWORD );
        curl_easy_setopt( eh.h, CURLOPT_USERNAME, user.c_str() );
        curl_easy_setopt( eh.h, CURLOPT_PASSWORD, password.c_str() );
        // 设置里只有「是否 Digest」一个开关：开 = 只用 Digest，关 = 只用 Basic
        const long auth = get_option<bool>( webdav::opt::AUTH_DIGEST )
                          ? CURLAUTH_DIGEST : CURLAUTH_BASIC;
        curl_easy_setopt( eh.h, CURLOPT_HTTPAUTH, auth );
    }
    // 部分 WebDAV 服务（nginx 之类）不支持 Expect: 100-continue，会导致 1 分钟卡顿
    eh.headers = curl_slist_append( eh.headers, "Expect:" );
    curl_easy_setopt( eh.h, CURLOPT_HTTPHEADER, eh.headers );
    // 默认响应体丢弃，需要时调用方覆盖
    curl_easy_setopt( eh.h, CURLOPT_WRITEFUNCTION, discard_write );
    // Android 上不设置 CA 路径，使用系统默认；这里不做额外设置
    return true;
}

long response_code( CURL *h )
{
    long code = 0;
    curl_easy_getinfo( h, CURLINFO_RESPONSE_CODE, &code );
    return code;
}

std::string describe( CURLcode res, long code )
{
    if( res != CURLE_OK ) {
        return string_format( _( "网络错误：%s" ), curl_easy_strerror( res ) );
    }
    switch( code ) {
        case 401:
            return string_format( _( "认证失败（HTTP 401），请检查用户名与密码，以及认证方式（Basic/Digest）" ) );
        case 403:
            return string_format( _( "没有访问权限（HTTP 403）" ) );
        case 404:
            return string_format( _( "路径不存在（HTTP 404），请检查服务器地址与备份目录" ) );
        case 405:
            return string_format( _( "服务器不允许该操作（HTTP 405），可能不支持 WebDAV" ) );
        case 409:
            return string_format( _( "服务器上的目录不存在（HTTP 409）" ) );
        case 0:
            return string_format( _( "没有收到服务器响应" ) );
        default:
            return string_format( _( "服务器返回 HTTP %d" ), static_cast<int>( code ) );
    }
}

/// 逐级 MKCOL，失败也无所谓（目录通常已存在），由后续 PUT 兜底报错
void ensure_remote_dir()
{
    std::string cur = base_url_from_options();
    if( cur.empty() ) {
        return;
    }
    for( const std::string &seg : path_segments_from_options() ) {
        cur += "/" + url_encode( seg );
        std::string err;
        easy_handle eh;
        if( !init_handle( eh, cur + "/", err ) ) {
            return;
        }
        curl_easy_setopt( eh.h, CURLOPT_CUSTOMREQUEST, "MKCOL" );
        curl_easy_perform( eh.h );
    }
}

// ---------------------------------------------------------------------------
// PROPFIND 解析
// ---------------------------------------------------------------------------

/// 极简 XML 扫描：只关心 response / href / getcontentlength / getlastmodified / resourcetype
std::vector<webdav::remote_file> parse_propfind( const std::string &xml, const std::string &base )
{
    std::vector<webdav::remote_file> out;
    const std::string host = scheme_host( base );
    webdav::remote_file cur;
    bool in_response = false;
    bool in_restype = false;
    std::string text;

    size_t pos = 0;
    while( pos < xml.size() ) {
        const size_t open = xml.find( '<', pos );
        if( open == std::string::npos ) {
            break;
        }
        const size_t close = xml.find( '>', open );
        if( close == std::string::npos ) {
            break;
        }
        text += xml.substr( pos, open - pos );
        std::string raw = xml.substr( open + 1, close - open - 1 );
        pos = close + 1;

        if( raw.empty() || raw[0] == '?' || raw[0] == '!' ) {
            continue; // <?xml?> / 注释
        }
        const bool closing = raw[0] == '/';
        if( closing ) {
            raw.erase( 0, 1 );
        }
        const size_t sp = raw.find_first_of( " \t\r\n" );
        if( sp != std::string::npos ) {
            raw = raw.substr( 0, sp );
        }
        bool selfclose = false;
        if( !raw.empty() && raw.back() == '/' ) {
            selfclose = true;
            raw.pop_back();
        }
        const size_t colon = raw.find( ':' );
        if( colon != std::string::npos ) {
            raw = raw.substr( colon + 1 ); // 去命名空间前缀
        }
        const std::string name = to_lower( trim_str( raw ) );

        if( !closing && !selfclose ) {
            text.clear();
            if( name == "response" ) {
                in_response = true;
                cur = webdav::remote_file();
            } else if( name == "resourcetype" ) {
                in_restype = true;
            } else if( in_restype && name == "collection" ) {
                cur.is_dir = true;
            }
            continue;
        }

        if( in_response ) {
            if( name == "href" ) {
                std::string href = trim_str( text );
                const std::string decoded = url_decode( href );
                // 取最后一段作为文件名
                std::string fname = decoded;
                while( !fname.empty() && fname.back() == '/' ) {
                    fname.pop_back();
                }
                const size_t slash = fname.find_last_of( "/\\" );
                if( slash != std::string::npos ) {
                    fname = fname.substr( slash + 1 );
                }
                cur.name = fname;
                // 组装可直接请求的 URL
                if( starts_with( href, "http://" ) || starts_with( href, "https://" ) ) {
                    cur.href = href;
                } else if( !href.empty() && href[0] == '/' ) {
                    cur.href = host + href;
                } else {
                    cur.href = base + url_encode( fname );
                }
            } else if( name == "getcontentlength" ) {
                const std::string v = trim_str( text );
                cur.size = v.empty() ? -1 : std::atoll( v.c_str() );
            } else if( name == "getlastmodified" ) {
                cur.mtime = trim_str( text );
            }
        }
        if( name == "resourcetype" ) {
            in_restype = false;
        }
        if( closing && name == "response" ) {
            if( in_response && !cur.is_dir && !cur.name.empty() ) {
                out.push_back( cur );
            }
            in_response = false;
        }
        text.clear();
    }

    std::sort( out.begin(), out.end(), []( const webdav::remote_file & a, const webdav::remote_file & b ) {
        return a.name > b.name; // 新的在前
    } );
    return out;
}

// ---------------------------------------------------------------------------
// tar.gz 解包
// ---------------------------------------------------------------------------

bool read_block( gzFile gz, char *buf )
{
    size_t got = 0;
    while( got < 512 ) {
        const int n = gzread( gz, buf + got, static_cast<unsigned>( 512 - got ) );
        if( n <= 0 ) {
            break;
        }
        got += static_cast<size_t>( n );
    }
    return got == 512;
}

long long octal_at( const char *p, size_t n )
{
    std::string s;
    for( size_t i = 0; i < n; i++ ) {
        if( p[i] == '\0' || p[i] == ' ' ) {
            break;
        }
        if( p[i] < '0' || p[i] > '7' ) {
            break;
        }
        s += p[i];
    }
    return s.empty() ? 0 : std::strtoll( s.c_str(), nullptr, 8 );
}

std::string cstr_at( const char *p, size_t n )
{
    size_t len = 0;
    while( len < n && p[len] != '\0' ) {
        len++;
    }
    return std::string( p, len );
}

bool block_is_zero( const char *p )
{
    for( int i = 0; i < 512; i++ ) {
        if( p[i] != '\0' ) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// 忙碌等待窗口
// ---------------------------------------------------------------------------

std::atomic<long long> g_now{ 0 };
std::atomic<long long> g_total{ 0 };

void default_progress( long long now, long long total )
{
    g_now = now;
    g_total = total;
}

using work_fn = std::function<bool( std::string & err )>;

/// 在后台线程跑 work，主线程画一个等待窗口（带传输进度）
bool run_with_spinner( const std::string &title, const work_fn &work, std::string &err )
{
    ensure_curl();
    bool ok = false;
    std::string worker_err;
    std::atomic<bool> finished{ false };

    g_now = 0;
    g_total = 0;

    std::thread th( [&]() {
        std::string e;
        try {
            ok = work( e );
        } catch( const std::exception &ex ) {
            ok = false;
            e = ex.what();
        } catch( ... ) {
            ok = false;
            e = "unknown error";
        }
        worker_err = e;
        finished = true;
    } );

    const int width = std::max( 34, std::min( 64, TERMX - 4 ) );
    const int height = 7;
    const catacurses::window w = catacurses::newwin( height, width,
                                 point( std::max( 0, ( TERMX - width ) / 2 ), std::max( 0, ( TERMY - height ) / 2 ) ) );
    static const char *frames[] = { "|", "/", "-", "\\" };
    int tick = 0;
    while( !finished ) {
        werase( w );
        draw_border( w, c_white );
        trim_and_print( w, point( 1, 1 ), width - 2, c_white, title );
        const long long now = g_now.load();
        const long long total = g_total.load();
        if( total > 0 ) {
            const int cells = width - 4;
            const int done = static_cast<int>( std::min<long long>( cells,
                                              now * cells / std::max<long long>( 1, total ) ) );
            std::string bar;
            for( int i = 0; i < cells; i++ ) {
                bar += i < done ? "#" : ".";
            }
            trim_and_print( w, point( 1, 2 ), width - 2, c_cyan, bar );
            trim_and_print( w, point( 1, 3 ), width - 2, c_yellow,
                            string_format( "%s  %s / %s", frames[tick % 4], human_size( now ), human_size( total ) ) );
        } else {
            trim_and_print( w, point( 1, 3 ), width - 2, c_yellow,
                            string_format( _( "处理中 %s" ), frames[tick % 4] ) );
        }
        trim_and_print( w, point( 1, 5 ), width - 2, c_light_gray, _( "请耐心等待" ) );
        catacurses::wrefresh( w );
        catacurses::refresh();
        std::this_thread::sleep_for( std::chrono::milliseconds( 150 ) );
        tick++;
    }
    th.join();
    err = worker_err;
    return ok;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// 协议层
// ---------------------------------------------------------------------------
namespace webdav
{

bool is_configured()
{
    return !opt_host().empty();
}

std::string remote_base_url()
{
    std::string u = base_url_from_options();
    if( u.empty() ) {
        return std::string();
    }
    for( const std::string &seg : path_segments_from_options() ) {
        u += "/";
        u += url_encode( seg );
    }
    u += "/";
    return u;
}

bool test_connection( std::string &err )
{
    ensure_curl();
    ensure_remote_dir(); // 顺带建目录，建不成也不影响后面的探测结果
    const std::string url = remote_base_url();
    easy_handle eh;
    if( !init_handle( eh, url, err ) ) {
        return false;
    }
    static const char *body =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<D:propfind xmlns:D=\"DAV:\"><D:prop><D:resourcetype/></D:prop></D:propfind>";
    eh.headers = curl_slist_append( eh.headers, "Depth: 0" );
    eh.headers = curl_slist_append( eh.headers, "Content-Type: application/xml; charset=utf-8" );
    curl_easy_setopt( eh.h, CURLOPT_HTTPHEADER, eh.headers );
    curl_easy_setopt( eh.h, CURLOPT_CUSTOMREQUEST, "PROPFIND" );
    curl_easy_setopt( eh.h, CURLOPT_POSTFIELDS, body );
    curl_easy_setopt( eh.h, CURLOPT_POSTFIELDSIZE, static_cast<long>( std::strlen( body ) ) );

    const CURLcode res = curl_easy_perform( eh.h );
    const long code = response_code( eh.h );
    if( res != CURLE_OK ) {
        err = describe( res, code );
        return false;
    }
    if( code == 200 || code == 207 || code == 204 || code == 301 || code == 302 ) {
        return true;
    }
    err = describe( res, code );
    return false;
}

bool list_files( std::vector<remote_file> &out, std::string &err )
{
    ensure_curl();
    const std::string url = remote_base_url();
    easy_handle eh;
    if( !init_handle( eh, url, err ) ) {
        return false;
    }
    static const char *body =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<D:propfind xmlns:D=\"DAV:\"><D:prop>"
        "<D:displayname/><D:getcontentlength/><D:getlastmodified/><D:resourcetype/>"
        "</D:prop></D:propfind>";
    eh.headers = curl_slist_append( eh.headers, "Depth: 1" );
    eh.headers = curl_slist_append( eh.headers, "Content-Type: application/xml; charset=utf-8" );
    curl_easy_setopt( eh.h, CURLOPT_HTTPHEADER, eh.headers );
    curl_easy_setopt( eh.h, CURLOPT_CUSTOMREQUEST, "PROPFIND" );
    curl_easy_setopt( eh.h, CURLOPT_POSTFIELDS, body );
    curl_easy_setopt( eh.h, CURLOPT_POSTFIELDSIZE, static_cast<long>( std::strlen( body ) ) );

    std::string resp;
    curl_easy_setopt( eh.h, CURLOPT_WRITEFUNCTION, string_write );
    curl_easy_setopt( eh.h, CURLOPT_WRITEDATA, &resp );

    const CURLcode res = curl_easy_perform( eh.h );
    const long code = response_code( eh.h );
    if( res != CURLE_OK ) {
        err = describe( res, code );
        return false;
    }
    if( code != 207 && code != 200 && code != 301 && code != 302 ) {
        err = describe( res, code );
        return false;
    }
    out = parse_propfind( resp, url );
    return true;
}

struct upload_ctx {
    std::istream *in = nullptr;
    long long total = 0;
    long long sent = 0;
    progress_cb cb;
};

size_t upload_read( char *buf, size_t size, size_t nmemb, void *user )
{
    upload_ctx *c = static_cast<upload_ctx *>( user );
    const std::streamsize want = static_cast<std::streamsize>( size * nmemb );
    c->in->read( buf, want );
    const std::streamsize got = c->in->gcount();
    c->sent += got;
    if( c->cb ) {
        c->cb( c->sent, c->total );
    }
    return static_cast<size_t>( got );
}

bool upload_file( const std::string &local_path, const std::string &remote_name,
                  std::string &err, progress_cb cb )
{
    ensure_curl();
    ensure_remote_dir();

    cata::ifstream fin( fs::u8path( local_path ), std::ios::binary );
    if( !fin.is_open() ) {
        err = string_format( _( "无法读取本地文件：%s" ), local_path );
        return false;
    }
    fin.seekg( 0, std::ios::end );
    const std::streamoff fsize = fin.tellg();
    fin.seekg( 0, std::ios::beg );
    if( fsize < 0 ) {
        err = _( "无法获取本地文件大小" );
        return false;
    }

    const std::string url = remote_base_url() + url_encode( remote_name );
    easy_handle eh;
    if( !init_handle( eh, url, err ) ) {
        return false;
    }

    upload_ctx ctx;
    ctx.in = &fin;
    ctx.total = static_cast<long long>( fsize );
    ctx.cb = cb;

    curl_easy_setopt( eh.h, CURLOPT_UPLOAD, 1L );
    curl_easy_setopt( eh.h, CURLOPT_READFUNCTION, upload_read );
    curl_easy_setopt( eh.h, CURLOPT_READDATA, &ctx );
    curl_easy_setopt( eh.h, CURLOPT_INFILESIZE_LARGE, static_cast<curl_off_t>( fsize ) );

    const CURLcode res = curl_easy_perform( eh.h );
    const long code = response_code( eh.h );
    if( res != CURLE_OK ) {
        err = describe( res, code );
        return false;
    }
    if( code != 200 && code != 201 && code != 204 && code != 0 ) {
        err = describe( res, code );
        return false;
    }
    return true;
}

struct download_ctx {
    std::ostream *out = nullptr;
    long long total = 0;
    long long got = 0;
    progress_cb cb;
};

size_t download_write( char *ptr, size_t size, size_t nmemb, void *user )
{
    download_ctx *c = static_cast<download_ctx *>( user );
    const std::streamsize n = static_cast<std::streamsize>( size * nmemb );
    c->out->write( ptr, n );
    c->got += n;
    if( c->cb ) {
        c->cb( c->got, c->total );
    }
    return c->out->good() ? static_cast<size_t>( n ) : 0;
}

bool download_file( const remote_file &file, const std::string &local_path,
                    std::string &err, progress_cb cb )
{
    ensure_curl();
    const fs::path p = fs::u8path( local_path );
    if( p.has_parent_path() ) {
        std::error_code ec;
        fs::create_directories( p.parent_path(), ec );
    }
    cata::ofstream fout( p, std::ios::binary | std::ios::trunc );
    if( !fout.is_open() ) {
        err = string_format( _( "无法写入本地文件：%s" ), local_path );
        return false;
    }

    easy_handle eh;
    if( !init_handle( eh, file.href, err ) ) {
        return false;
    }
    download_ctx ctx;
    ctx.out = &fout;
    ctx.total = file.size > 0 ? file.size : 0;
    ctx.cb = cb;

    curl_easy_setopt( eh.h, CURLOPT_WRITEFUNCTION, download_write );
    curl_easy_setopt( eh.h, CURLOPT_WRITEDATA, &ctx );

    const CURLcode res = curl_easy_perform( eh.h );
    const long code = response_code( eh.h );
    fout.close();
    if( res != CURLE_OK ) {
        err = describe( res, code );
        return false;
    }
    if( code != 200 && code != 206 && code != 0 ) {
        err = describe( res, code );
        return false;
    }
    return true;
}

bool delete_file( const std::string &remote_name, std::string &err )
{
    ensure_curl();
    const std::string url = remote_base_url() + url_encode( remote_name );
    easy_handle eh;
    if( !init_handle( eh, url, err ) ) {
        return false;
    }
    curl_easy_setopt( eh.h, CURLOPT_CUSTOMREQUEST, "DELETE" );
    const CURLcode res = curl_easy_perform( eh.h );
    const long code = response_code( eh.h );
    if( res != CURLE_OK ) {
        err = describe( res, code );
        return false;
    }
    if( code != 200 && code != 202 && code != 204 && code != 404 && code != 0 ) {
        err = describe( res, code );
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 打包 / 解包
// ---------------------------------------------------------------------------

std::string pack_world( const std::string &world_name, std::string &err )
{
    const fs::path save_root = fs::u8path( PATH_INFO::savedir() );
    const fs::path world_dir = save_root / fs::u8path( world_name );
    std::error_code ec;
    if( !fs::is_directory( world_dir, ec ) ) {
        err = string_format( _( "找不到世界目录：%s" ), world_dir.u8string() );
        return std::string();
    }

    // 本地临时文件名只用 ASCII，避免中文路径在部分平台上打不开压缩包
    const std::string arc_path = tmp_path( "backup_" + timestamp_str() + ".tar.gz" );
    std::remove( arc_path.c_str() );

    tgz_archiver tgz( arc_path );
    for( fs::recursive_directory_iterator it( world_dir, ec ), end; it != end; it.increment( ec ) ) {
        if( ec ) {
            break;
        }
        const fs::path dep = it->path().lexically_relative( save_root ); // 保留世界目录名作为顶层
        if( !tgz.add_file( it->path(), dep ) ) {
            err = string_format( _( "打包失败：%s" ), it->path().u8string() );
            return std::string();
        }
    }
    tgz.finalize();

    if( !file_exist( fs::u8path( arc_path ) ) ) {
        err = _( "生成压缩包失败" );
        return std::string();
    }
    return arc_path;
}

bool unpack_archive( const std::string &archive_path, std::string &err,
                     std::vector<std::string> *restored, bool overwrite )
{
    gzFile gz = gzopen( archive_path.c_str(), "rb" );
    if( !gz ) {
        err = string_format( _( "无法打开压缩包：%s" ), archive_path );
        return false;
    }
    const fs::path dest = fs::u8path( PATH_INFO::savedir() );
    std::error_code ec;
    fs::create_directories( dest, ec );

    std::vector<char> raw( 512 );
    bool ok = true;
    std::set<std::string> roots;  // 压缩包里的顶层目录，即世界名

    while( read_block( gz, raw.data() ) ) {
        if( block_is_zero( raw.data() ) ) {
            break; // tar 结束标记
        }
        std::string name = cstr_at( raw.data(), 100 );
        const std::string prefix = cstr_at( raw.data() + 345, 155 );
        if( !prefix.empty() ) {
            name = prefix + "/" + name;
        }
        const long long size = octal_at( raw.data() + 124, 12 );
        const char typeflag = raw.data()[156];

        if( name.empty() ) {
            continue;
        }
        const std::vector<std::string> segs = split_dir( name );
        // 防目录穿越
        bool bad = false;
        for( const std::string &seg : segs ) {
            if( seg == ".." ) {
                bad = true;
            }
        }
        if( bad || segs.empty() ) {
            continue;
        }

        const std::string root = segs.front();
        roots.insert( root );

        if( typeflag == '5' ) {
            continue; // 目录条目，写文件时自动建父目录
        }
        if( typeflag != '0' && typeflag != '\0' && typeflag != '7' ) {
            continue; // 符号链接等一律跳过
        }

        const fs::path target = dest / fs::u8path( name );
        std::error_code fec;
        // overwrite=false 时保留本地已有文件；无论写不写都要把数据块读完，否则流会错位
        const bool write_it = overwrite || !fs::exists( target, fec );

        cata::ofstream fout;
        if( write_it ) {
            if( target.has_parent_path() ) {
                fs::create_directories( target.parent_path(), ec );
            }
            fout.open( target, std::ios::binary | std::ios::trunc );
            if( !fout.is_open() ) {
                err = string_format( _( "无法写入：%s" ), target.u8string().c_str() );
                ok = false;
                break;
            }
        }
        long long left = size;
        while( left > 0 ) {
            const size_t want = static_cast<size_t>( std::min<long long>( left, 512 ) );
            if( !read_block( gz, raw.data() ) ) {
                ok = false;
                break;
            }
            if( write_it ) {
                fout.write( raw.data(), static_cast<std::streamsize>( want ) );
            }
            left -= static_cast<long long>( want );
        }
        if( write_it ) {
            fout.close();
        }
        if( !ok ) {
            break;
        }
    }
    gzclose( gz );

    if( !ok ) {
        return false;
    }
    if( restored ) {
        restored->assign( roots.begin(), roots.end() );
    }
    return true;
}

// ---------------------------------------------------------------------------
// 高层流程
// ---------------------------------------------------------------------------

/// 按 WEBDAV_MAX_KEEP 清理最旧的备份
void trim_old_backups( std::string &err )
{
    if( WEBDAV_MAX_KEEP <= 0 ) {
        return;
    }
    std::vector<remote_file> files;
    if( !list_files( files, err ) ) {
        return;
    }
    if( static_cast<int>( files.size() ) <= WEBDAV_MAX_KEEP ) {
        return;
    }
    // parse_propfind 已按名称倒序，末尾即最旧
    for( size_t i = WEBDAV_MAX_KEEP; i < files.size(); i++ ) {
        std::string e;
        delete_file( files[i].name, e );
    }
}

bool backup_world( const std::string &world_name, std::string &err, progress_cb cb )
{
    std::string arc = pack_world( world_name, err );
    if( arc.empty() ) {
        return false;
    }
    const std::string remote_name = world_name + "_" + timestamp_str() + ".tar.gz";
    const bool ok = upload_file( arc, remote_name, err, cb );
    std::remove( arc.c_str() );
    if( !ok ) {
        return false;
    }
    std::string ignored;
    trim_old_backups( ignored );
    return true;
}

bool restore_world( const remote_file &file, std::string &err,
                    std::vector<std::string> *restored, progress_cb cb )
{
    const std::string arc = tmp_path( "restore_" + timestamp_str() + ".tar.gz" );
    std::remove( arc.c_str() );
    if( !download_file( file, arc, err, cb ) ) {
        std::remove( arc.c_str() );
        return false;
    }
    if( !unpack_archive( arc, err, restored, true ) ) {
        std::remove( arc.c_str() );
        return false;
    }
    std::remove( arc.c_str() );
    return true;
}

// ---------------------------------------------------------------------------
// 菜单
// ---------------------------------------------------------------------------

static std::string current_world_name()
{
    if( world_generator && world_generator->active_world ) {
        return world_generator->active_world->world_name;
    }
    return std::string();
}

/// 没配置服务器时给个明确指引
static bool require_config()
{
    if( webdav::is_configured() ) {
        return true;
    }
    popup( _( "还没有配置 WebDAV 服务器。\n请在 设置 → WebDAV云存档 里填写服务器地址、备份目录、账号和密码。" ) );
    return false;
}

static std::string pick_world_name()
{
    const std::vector<std::string> names = world_generator->all_worldnames();
    if( names.empty() ) {
        return std::string();
    }
    uilist m;
    m.text = _( "选择要备份的世界" );
    for( size_t i = 0; i < names.size(); i++ ) {
        m.addentry( static_cast<int>( i ), true, 0, names[i] );
    }
    m.query();
    if( m.ret < 0 || static_cast<size_t>( m.ret ) >= names.size() ) {
        return std::string();
    }
    return names[m.ret];
}

static void do_test()
{
    if( !require_config() ) {
        return;
    }
    std::string err;
    const bool ok = run_with_spinner( _( "正在连接服务器" ), []( std::string & e ) {
        return webdav::test_connection( e );
    }, err );
    if( ok ) {
        popup( _( "连接成功，服务器可用。" ) );
    } else {
        popup( _( "连接失败\n%s" ), err );
    }
}

static void do_backup( const bool in_game )
{
    if( !require_config() ) {
        return;
    }
    std::string world = current_world_name();
    if( world.empty() ) {
        world = pick_world_name();
    }
    if( world.empty() ) {
        popup( _( "没有找到可备份的世界。" ) );
        return;
    }
    if( !query_yn( _( "把世界 \"%s\" 备份到云端？\n%s" ), world, webdav::remote_base_url() ) ) {
        return;
    }
    // 存档必须在主线程完成
    if( in_game && world == current_world_name() ) {
        g->save();
    }
    const std::string world_name = world;
    std::string err;
    const bool ok = run_with_spinner( _( "正在打包并上传存档" ), [&world_name]( std::string & e ) {
        return webdav::backup_world( world_name, e, default_progress );
    }, err );
    if( ok ) {
        popup( _( "备份完成。" ) );
    } else {
        popup( _( "备份失败\n%s" ), err );
    }
}

static void do_restore( const bool in_game )
{
    if( !require_config() ) {
        return;
    }
    std::vector<remote_file> files;
    std::string err;
    if( !run_with_spinner( _( "正在获取云端备份列表" ), [&files]( std::string & e ) {
        return webdav::list_files( files, e );
    }, err ) ) {
        popup( _( "获取列表失败\n%s" ), err );
        return;
    }
    if( files.empty() ) {
        popup( _( "云端还没有任何备份。" ) );
        return;
    }

    uilist m;
    m.text = _( "选择要恢复的备份" );
    for( size_t i = 0; i < files.size(); i++ ) {
        const remote_file &f = files[i];
        std::string label = f.name;
        if( f.size > 0 ) {
            label += "  (" + human_size( f.size ) + ")";
        }
        if( !f.mtime.empty() ) {
            label += "  " + f.mtime;
        }
        m.addentry( static_cast<int>( i ), true, 0, label );
    }
    m.query();
    if( m.ret < 0 || static_cast<size_t>( m.ret ) >= files.size() ) {
        return;
    }
    const remote_file chosen = files[m.ret];

    if( !query_yn( _( "恢复 \"%s\"？\n本地同名的存档文件将被覆盖。" ), chosen.name ) ) {
        return;
    }

    std::vector<std::string> restored;
    std::string rerr;
    const bool ok = run_with_spinner( _( "正在下载并恢复存档" ), [&]( std::string & e ) {
        return webdav::restore_world( chosen, e, &restored, default_progress );
    }, rerr );

    if( !ok ) {
        popup( _( "恢复失败\n%s" ), rerr );
        return;
    }

    if( !in_game ) {
        // 主菜单下可以安全地重新扫描世界列表
        world_generator->init();
    }
    std::string what;
    for( const std::string &w : restored ) {
        if( !what.empty() ) {
            what += ", ";
        }
        what += w;
    }
    if( what.empty() ) {
        what = chosen.name;
    }
    if( in_game ) {
        popup( _( "恢复完成：%s\n请退出到主菜单后重新进入游戏。" ), what );
    } else {
        popup( _( "恢复完成：%s" ), what );
    }
}

/// 管理界面的按键回调：空格勾选 / A 全选
struct manage_cb : public uilist_callback {
    std::vector<char> *marks = nullptr;
    std::vector<std::string> *labels = nullptr;

    /// 把勾选状态写回每一行的显示文本
    void sync( uilist *menu ) {
        for( size_t i = 0; i < menu->entries.size() && i < labels->size(); i++ ) {
            menu->entries[i].txt = string_format( "[%s] %s",
                                   ( *marks )[i] ? "X" : " ",
                                   ( *labels )[i] );
        }
    }

    bool key( const input_context &, const input_event &event, int entnum,
              uilist *menu ) override {
        const int ch = event.get_first_input();
        // 空格：勾选 / 取消当前项
        if( ch == ' ' ) {
            if( entnum >= 0 && entnum < static_cast<int>( marks->size() ) ) {
                ( *marks )[entnum] = ( *marks )[entnum] ? 0 : 1;
                sync( menu );
            }
            return true;
        }
        // A：全选 / 全不选
        if( ch == 'a' || ch == 'A' ) {
            bool has_unmarked = false;
            for( char m : *marks ) {
                if( !m ) {
                    has_unmarked = true;
                    break;
                }
            }
            std::fill( marks->begin(), marks->end(), has_unmarked ? 1 : 0 );
            sync( menu );
            return true;
        }
        return false;
    }

    void refresh( uilist *menu ) override {
        sync( menu );
    }
};

/// 管理云端备份：支持空格多选、批量删除，删完留在菜单里可以接着删
static void do_manage()
{
    if( !require_config() ) {
        return;
    }

    while( true ) {
        std::vector<remote_file> files;
        std::string err;
        if( !run_with_spinner( _( "正在获取云端备份列表" ), [&files]( std::string & e ) {
            return webdav::list_files( files, e );
        }, err ) ) {
            popup( _( "获取列表失败\n%s" ), err );
            return;
        }
        if( files.empty() ) {
            popup( _( "云端已经没有备份了。" ) );
            return;
        }

        std::vector<std::string> labels;
        labels.reserve( files.size() );
        for( const remote_file &f : files ) {
            std::string label = f.name;
            if( f.size > 0 ) {
                label += "  (" + human_size( f.size ) + ")";
            }
            if( !f.mtime.empty() ) {
                label += "  " + f.mtime;
            }
            labels.push_back( label );
        }
        std::vector<char> marks( files.size(), 0 );

        manage_cb cb;
        cb.marks = &marks;
        cb.labels = &labels;

        uilist m;
        m.text = _( "管理云端备份\n空格：勾选 / 取消当前项    A：全选 / 全不选\n"
                    "回车：删除所有勾选项（没勾选就删当前项）    ESC：返回上级菜单" );
        // key 传 0 = 不分配字母热键，避免和上面的 A / 空格冲突
        for( size_t i = 0; i < files.size(); i++ ) {
            m.addentry( static_cast<int>( i ), true, 0, labels[i] );
        }
        m.callback = &cb;
        m.query();
        if( m.ret < 0 || static_cast<size_t>( m.ret ) >= files.size() ) {
            return;    // ESC 取消
        }

        // 收集待删目标：有勾选就删勾选项，否则退化为删当前这一项
        std::vector<std::string> targets;
        for( size_t i = 0; i < marks.size(); i++ ) {
            if( marks[i] ) {
                targets.push_back( files[i].name );
            }
        }
        if( targets.empty() ) {
            targets.push_back( files[m.ret].name );
        }

        const size_t total = targets.size();
        if( !query_yn( _( "确定删除 %d 个云端备份？此操作不可恢复。" ), static_cast<int>( total ) ) ) {
            continue;    // 回到列表，不退出
        }

        std::vector<std::string> failed;
        const bool ok = run_with_spinner( _( "正在删除云端备份" ),
        [&targets, &failed]( std::string & e ) {
            for( const std::string &name : targets ) {
                std::string one_err;
                if( !webdav::delete_file( name, one_err ) ) {
                    failed.push_back( name + "：" + one_err );
                }
            }
            if( !failed.empty() ) {
                e = string_format( _( "%d 个删除失败" ), static_cast<int>( failed.size() ) );
                return false;
            }
            return true;
        }, err );

        if( ok ) {
            popup( _( "已删除 %d 个备份。" ), static_cast<int>( total ) );
        } else {
            std::string detail;
            for( const std::string &f : failed ) {
                detail += "\n" + f;
            }
            popup( string_format( _( "删除完成 %d 个，失败 %d 个：%s" ),
                                  static_cast<int>( total - failed.size() ),
                                  static_cast<int>( failed.size() ), detail ) );
        }
        // 继续下一轮：重新拉取列表，直到云端清空或按 ESC
    }
}

void show_menu( const bool in_game )
{
    ensure_curl();

    enum choice : int {
        C_TEST = 0,
        C_BACKUP,
        C_RESTORE,
        C_MANAGE,
        C_SETTINGS
    };

    const bool ready = is_configured();

    uilist menu;
    menu.text = ready
                ? string_format( _( "WebDAV 云存档\n当前服务器：%s" ), remote_base_url() )
                : _( "WebDAV 云存档\n还没有配置服务器，请先选择下面的“服务器设置”。" );
    menu.addentry( C_TEST, ready, '1', _( "测试服务器连接" ) );
    menu.addentry( C_BACKUP, ready, '2', _( "备份存档到云端" ) );
    menu.addentry( C_RESTORE, ready, '3', _( "从云端恢复存档" ) );
    menu.addentry( C_MANAGE, ready, '4', _( "管理云端备份（删除）" ) );
    menu.addentry( C_SETTINGS, true, '5', _( "服务器设置（地址 / 账号 / 密码）" ) );
    menu.query();

    switch( menu.ret ) {
        case C_TEST:
            do_test();
            break;
        case C_BACKUP:
            do_backup( in_game );
            break;
        case C_RESTORE:
            do_restore( in_game );
            break;
        case C_MANAGE:
            do_manage();
            break;
        case C_SETTINGS:
            // 直接跳到设置界面，改完点返回即自动保存
            get_options().show( in_game );
            break;
        default:
            break;
    }
}

} // namespace webdav
