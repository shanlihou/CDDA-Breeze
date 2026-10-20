#!/usr/bin/env bash
#
# CDDA-Breeze Linux 一键构建脚本（ASCII / curses 字符版）
#
# 全流程：检查/安装依赖 -> CMake 配置 -> 编译 -> 生成启动脚本
#
# 基本用法：
#   ./build_linux.sh                  # 装依赖 + Release 编译 ASCII 版
#   ./build_linux.sh --install-deps   # 只安装依赖，不编译
#   ./build_linux.sh --help           # 查看全部参数
#
# 产物：
#   二进制: out/build/linux-<type>/src/cataclysm
#   启动:   out/dist/run-cataclysm.sh
#
set -uo pipefail

# ========================= 默认配置 =========================
BUILD_TYPE="Release"
JOBS=""
DO_CLEAN=0
INSTALL_DEPS_ONLY=0
CONFIGURE_ONLY=0
RUN_AFTER=0
WERROR=0                   # 默认关闭 -Werror：本 fork 有大量自定义代码，警告可能很多
TILES=OFF
CURSES=ON
SOUND=OFF
LOCALIZE=ON
LANGUAGES="zh_CN"          # 默认编译简体中文；不需要多语言用 --no-localize
TESTS=OFF
BACKTRACE=ON
LIBBACKTRACE=OFF
USE_HOME_DIR=ON
USE_PREFIX_DATA_DIR=OFF    # 关掉后从仓库根目录（data/lang）找资源，方便直接跑
CMAKE_GENERATOR=""
SHOW_HELP=0
# ============================================================

# ---------- 输出样式 ----------
if [ -t 1 ]; then
    C_RESET=$'\033[0m'; C_RED=$'\033[31m'; C_GREEN=$'\033[32m'
    C_YELLOW=$'\033[33m'; C_BLUE=$'\033[34m'; C_BOLD=$'\033[1m'
else
    C_RESET=""; C_RED=""; C_GREEN=""; C_YELLOW=""; C_BLUE=""; C_BOLD=""
fi
info()  { printf '%s[信息]%s %s\n' "$C_BLUE" "$C_RESET" "$*"; }
ok()    { printf '%s[完成]%s %s\n' "$C_GREEN" "$C_RESET" "$*"; }
warn()  { printf '%s[警告]%s %s\n' "$C_YELLOW" "$C_RESET" "$*"; }
err()   { printf '%s[错误]%s %s\n' "$C_RED" "$C_RESET" "$*" >&2; }
title() { printf '%s%s%s\n' "$C_BOLD" "$*" "$C_RESET"; }

usage() {
    cat <<EOF

用法: build_linux.sh [release | debug | relwithdebinfo | minsizerel] [选项]

构建类型（默认 release）:
  release               发布版（推荐）
  debug                 调试版（优化关闭，含调试符号）
  relwithdebinfo        带调试信息的发布版
  minsizerel            最小体积发布版

通用选项:
  --jobs N              并行编译线程数（默认按 CPU 核心数与内存自动收敛；可强制覆盖）
  --clean               清空构建目录后重新编译
  --install-deps        只检查并安装依赖，然后退出（不编译）
  --configure-only      只跑 CMake 配置就退出（用于排查依赖问题）
  --run                 构建完成后直接启动游戏
  --help                显示本帮助

构建选项:
  --no-localize         关闭多语言（不需要 gettext，也不编 .mo）
  --languages LIST      要编译的语言，空格分隔，如 "zh_CN zh_TW"（默认 zh_CN）
  --tests ON|OFF        是否编译单元测试（默认 OFF）
  --werror              开启 -Werror（默认关闭，避免 fork 自定义代码警告导致编译失败）
  --generator NAME      CMake 生成器（默认优先 Ninja，其次 Make）

产物:
  构建目录: out/build/linux-<type>
  二进制:     out/build/linux-<type>/src/cataclysm
  启动脚本:   out/dist/run-cataclysm.sh
  存档目录:   ~/.cataclysm-dda/

EOF
}

die() { err "$*"; exit 1; }

# ---------- 参数解析 ----------
while [ $# -gt 0 ]; do
    case "$1" in
        release)          BUILD_TYPE="Release";        shift ;;
        debug)            BUILD_TYPE="Debug";          shift ;;
        relwithdebinfo)   BUILD_TYPE="RelWithDebInfo"; shift ;;
        minsizerel)       BUILD_TYPE="MinSizeRel";     shift ;;
        --jobs)           [ $# -ge 2 ] || die "--jobs 缺少参数"; JOBS="$2"; shift 2 ;;
        --clean)          DO_CLEAN=1;     shift ;;
        --install-deps)   INSTALL_DEPS_ONLY=1; shift ;;
        --configure-only) CONFIGURE_ONLY=1; shift ;;
        --run)            RUN_AFTER=1;    shift ;;
        --no-localize)    LOCALIZE=OFF;   shift ;;
        --languages)      [ $# -ge 2 ] || die "--languages 缺少参数"; LANGUAGES="$2"; shift 2 ;;
        --tests)          [ $# -ge 2 ] || die "--tests 缺少参数"; TESTS="$2"; shift 2 ;;
        --werror)         WERROR=1;       shift ;;
        --generator)      [ $# -ge 2 ] || die "--generator 缺少参数"; CMAKE_GENERATOR="$2"; shift 2 ;;
        --help|-h)        SHOW_HELP=1;    shift ;;
        *)                err "未知参数: $1"; usage; exit 1 ;;
    esac
done

if [ "$SHOW_HELP" = "1" ]; then usage; exit 0; fi

# ---------- 路径 ----------
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT/out/build/linux-${BUILD_TYPE,,}"
DIST_DIR="$ROOT/out/dist"
RUNNER="$DIST_DIR/run-cataclysm.sh"

title "============================================================"
title " CDDA-Breeze Linux 构建（ASCII / curses 字符版）"
title " 仓库: $ROOT"
title " 类型: $BUILD_TYPE"
title "============================================================"
echo

# ============================================================
echo "[1/6] 检查环境"
# ============================================================
[ "$(uname -s)" = "Linux" ] || die "此脚本只能在 Linux 上运行（当前: $(uname -s)）"
info "Linux $(uname -r) / $(uname -m)"

# 是否用 sudo 装依赖（非 root 时）
SUDO=""
if [ "$(id -u)" -ne 0 ]; then SUDO="sudo"; fi

# 探测包管理器
PKG_MGR=""
if command -v apt-get >/dev/null 2>&1; then PKG_MGR="apt"
elif command -v dnf >/dev/null 2>&1;     then PKG_MGR="dnf"
elif command -v yum >/dev/null 2>&1;     then PKG_MGR="yum"
elif command -v pacman >/dev/null 2>&1;  then PKG_MGR="pacman"
elif command -v zypper >/dev/null 2>&1;  then PKG_MGR="zypper"
else
    warn "未识别到常见包管理器（apt/dnf/yum/pacman/zypper）。"
    warn "请手动安装依赖后重新运行："
    warn "  编译器与构建工具: gcc g++ cmake make ninja"
    warn "  ASCII 版依赖:     ncurses(wide) 开发包、zlib 开发包、curl 开发包"
    warn "  多语言(可选):      gettext（提供 msgfmt）"
fi
[ -n "$PKG_MGR" ] && info "包管理器: $PKG_MGR"

# 仓库可能在 hgfs/vboxsf/NFS 等挂载点上，owner 与当前用户不一致，
# git 会报 "dubious ownership" 导致版本号取不到（仅警告，不致命，但很吵），这里提前加白名单。
if command -v git >/dev/null 2>&1; then
    if ! git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1 2>&1; then
        : # 非 git 仓库，跳过
    elif git -C "$ROOT" config --get --global --add safe.directory >/dev/null 2>&1; then
        # 已有全局 safe.directory，确保本目录在内
        if ! git config --global --get-all safe.directory 2>/dev/null | grep -qxF "$ROOT"; then
            git config --global --add safe.directory "$ROOT" 2>/dev/null \
                && info "已将仓库加入 git safe.directory 白名单"
        fi
    else
        git config --global --add safe.directory "$ROOT" 2>/dev/null \
            && info "已将仓库加入 git safe.directory 白名单"
    fi
fi

# ============================================================
echo
echo "[2/6] 检查并安装依赖"
# ============================================================

# 安装命令（按发行版）
case "$PKG_MGR" in
    apt)     INSTALL_CMD=(apt-get install -y); UPDATE_CMD=(apt-get update) ;;
    dnf)     INSTALL_CMD=(dnf install -y);     UPDATE_CMD=(dnf makecache) ;;
    yum)     INSTALL_CMD=(yum install -y);     UPDATE_CMD=(yum makecache) ;;
    pacman)  INSTALL_CMD=(pacman -S --needed --noconfirm); UPDATE_CMD=() ;;
    zypper)  INSTALL_CMD=(zypper install -y);  UPDATE_CMD=(zypper refresh) ;;
    *)       INSTALL_CMD=(); UPDATE_CMD=() ;;
esac

# 每个“角色”在某发行版下对应的包名（能力缺失时只装这一个正确的包）
role_pkg() {
    case "$PKG_MGR" in
        apt)
            case "$1" in
                compiler) echo build-essential ;;
                cmake)    echo cmake ;;
                ninja)    echo ninja-build ;;
                pkgconfig)echo pkg-config ;;
                ncurses)  echo libncurses-dev ;;
                zlib)     echo zlib1g-dev ;;
                curl)     echo libcurl4-openssl-dev ;;
                gettext)  echo gettext ;;
                # curses 构建编译期也需要 SDL2 头文件（fork 的 cursesdef.h 无条件 include sdl_wrappers.h）
                sdl2)     echo libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev ;;
            esac ;;
        dnf|yum)
            case "$1" in
                compiler) echo gcc-c++ ;;
                cmake)    echo cmake ;;
                ninja)    echo ninja-build ;;
                pkgconfig)echo pkgconf-pkg-config ;;
                ncurses)  echo ncurses-devel ;;
                zlib)     echo zlib-devel ;;
                curl)     echo libcurl-devel ;;
                gettext)  echo gettext ;;
                sdl2)     echo SDL2-devel SDL2_image-devel SDL2_ttf-devel ;;
            esac ;;
        pacman)
            case "$1" in
                compiler) echo base-devel ;;
                cmake)    echo cmake ;;
                ninja)    echo ninja ;;
                pkgconfig)echo pkgconf ;;
                ncurses)  echo ncurses ;;
                zlib)     echo zlib ;;
                curl)     echo curl ;;
                gettext)  echo gettext ;;
                sdl2)     echo sdl2 sdl2_image sdl2_ttf ;;
            esac ;;
        zypper)
            case "$1" in
                compiler) echo gcc-c++ ;;
                cmake)    echo cmake ;;
                ninja)    echo ninja ;;
                pkgconfig)echo pkg-config ;;
                ncurses)  echo ncurses-devel ;;
                zlib)     echo zlib-devel ;;
                curl)     echo libcurl-devel ;;
                gettext)  echo gettext-tools ;;
                sdl2)     echo SDL2-devel SDL2_image-devel SDL2_ttf-devel ;;
            esac ;;
    esac
}

# 每个“角色”的就绪判定（跨发行版统一用能力命令判断，不依赖包名）
role_ok() {
    case "$1" in
        compiler)  command -v g++ >/dev/null 2>&1 || command -v c++ >/dev/null 2>&1 ;;
        cmake)     command -v cmake >/dev/null 2>&1 ;;
        ninja)     command -v ninja >/dev/null 2>&1 ;;
        pkgconfig) command -v pkg-config >/dev/null 2>&1 ;;
        ncurses)   command -v ncursesw5-config >/dev/null 2>&1 || \
                   command -v ncursesw6-config >/dev/null 2>&1 || \
                   pkg-config --exists ncursesw 2>/dev/null || \
                   pkg-config --exists ncurses 2>/dev/null ;;
        zlib)      pkg-config --exists zlib 2>/dev/null ;;
        curl)      pkg-config --exists libcurl 2>/dev/null ;;
        gettext)   command -v msgfmt >/dev/null 2>&1 ;;
        # fork 的 cursesdef.h / cursesport.h 无条件引用 SDL 类型（SDL_Texture/SDL_Rect/SDL_ttf.h 等），
        # 故 curses 构建编译期也需要 SDL2 全套头文件（不链接，仅满足类型声明）。三个模块都要齐。
        sdl2)      pkg-config --exists sdl2 2>/dev/null && \
                   pkg-config --exists SDL2_image 2>/dev/null && \
                   pkg-config --exists SDL2_ttf 2>/dev/null ;;
    esac
}

ROLES=(compiler cmake ninja pkgconfig ncurses zlib curl sdl2)
[ "$LOCALIZE" = "ON" ] && ROLES+=(gettext)

MISSING_PKGS=()
for r in "${ROLES[@]}"; do
    pkg="$(role_pkg "$r")"
    if role_ok "$r"; then
        info "  $r: 已就绪"
    else
        info "  $r: 缺失（将安装 $pkg）"
        [ -n "$pkg" ] && MISSING_PKGS+=("$pkg")
    fi
done

# 去重
if [ ${#MISSING_PKGS[@]} -gt 0 ]; then
    MISSING_PKGS=($(printf '%s\n' "${MISSING_PKGS[@]}" | sort -u))
fi

if [ ${#MISSING_PKGS[@]} -eq 0 ]; then
    ok "依赖齐全"
elif [ -z "$PKG_MGR" ]; then
    die "未识别到包管理器，请手动安装依赖后重新运行：编译器(gcc/g++) cmake ninja ncurses(wide)开发包 zlib开发包 curl开发包${LOCALIZE:+ gettext}"
else
    info "缺失依赖，正在安装: ${MISSING_PKGS[*]}"
    if [ ${#UPDATE_CMD[@]} -gt 0 ]; then
        $SUDO "${UPDATE_CMD[@]}" >/dev/null 2>&1 || warn "更新软件源失败，继续尝试安装..."
    fi
    if ! $SUDO "${INSTALL_CMD[@]}" "${MISSING_PKGS[@]}"; then
        echo
        err "依赖安装失败。请手动执行后重新运行本脚本："
        err "   ${SUDO:-}${INSTALL_CMD[*]} ${MISSING_PKGS[*]}"
        exit 1
    fi
    # 装完再核一遍，确认能力真的就位（避免发行版换包名等意外）
    STILL_MISSING=()
    for r in "${ROLES[@]}"; do
        role_ok "$r" || STILL_MISSING+=("$r")
    done
    if [ ${#STILL_MISSING[@]} -gt 0 ]; then
        die "以下依赖装完后仍不可用: ${STILL_MISSING[*]}（请手动安装对应包后重试）"
    fi
    ok "依赖安装完成"
fi

if [ "$INSTALL_DEPS_ONLY" = "1" ]; then
    echo
    title "============================================================"
    title " 依赖已就绪（--install-deps，未编译）"
    title "============================================================"
    echo " 下一步: ./build_linux.sh"
    exit 0
fi

# ============================================================
echo
echo "[3/6] 准备工具链"
# ============================================================
command -v cmake >/dev/null 2>&1 || die "未找到 cmake，请确认已安装"
info "cmake: $(cmake --version | head -1)"

command -v g++ >/dev/null 2>&1 || command -v c++ >/dev/null 2>&1 || die "未找到 C++ 编译器（g++/c++）"
info "编译器: $( (g++ --version || c++ --version) 2>/dev/null | head -1)"

# 探测编译器厂商：GCC 13 把 -Wchanges-meaning 等当成默认即报错的 permerror，
# 全局 -Wno-error 压不住，需要针对 GCC 单独加 -Wno-changes-meaning。
# 注意：Ubuntu 的 c++ --version 输出是 "(Ubuntu 13.3.0...)"，不含 "gcc" 字样，
# 所以不能 grep gcc；改为“非 clang 即按 GCC 处理”（Linux 上编译器基本就是 gcc/clang）。
IS_GCC=0
if command -v c++ >/dev/null 2>&1; then
    if c++ --version 2>/dev/null | grep -qi clang; then
        IS_GCC=0
    else
        IS_GCC=1
    fi
fi

if [ -z "$CMAKE_GENERATOR" ]; then
    if command -v ninja >/dev/null 2>&1; then
        CMAKE_GENERATOR="Ninja"
    else
        CMAKE_GENERATOR="Unix Makefiles"
        info "未找到 ninja，使用 Unix Makefiles"
    fi
fi
info "生成器: $CMAKE_GENERATOR"

# 并行度：默认 = CPU 核心数，但依据物理内存收敛，避免 GCC 编译大型 .cpp 时 OOM 被杀
if [ -z "$JOBS" ]; then
    JOBS="$(nproc 2>/dev/null || echo 2)"
    MEM_GB=0
    if [ -r /proc/meminfo ]; then
        MEM_KB=$(awk '/^MemTotal:/{print $2}' /proc/meminfo 2>/dev/null || echo 0)
        MEM_GB=$(( MEM_KB / 1024 / 1024 ))
    fi
    if [ "$MEM_GB" -gt 0 ]; then
        # CDDA 单个编译单元（如 activity_actor.cpp）在 GCC 下可能占 1.5~2.5GB，
        # 按 2GB/任务预留并给系统留 1GB，估算内存允许的最大并行数。
        MAX_BY_MEM=$(( (MEM_GB - 1) / 2 ))
        [ "$MAX_BY_MEM" -lt 1 ] && MAX_BY_MEM=1
        if [ "$JOBS" -gt "$MAX_BY_MEM" ]; then
            info "内存约 ${MEM_GB}GB，已将并行任务从 ${JOBS} 降到 ${MAX_BY_MEM} 以避免 OOM"
            JOBS="$MAX_BY_MEM"
        fi
    fi
fi
info "并行任务: $JOBS"

MSGFMT=""
if [ "$LOCALIZE" = "ON" ]; then
    for p in /usr/bin/msgfmt /usr/local/bin/msgfmt; do
        [ -x "$p" ] && { MSGFMT="$p"; break; }
    done
    [ -z "$MSGFMT" ] && command -v msgfmt >/dev/null 2>&1 && MSGFMT="$(command -v msgfmt)"
    [ -n "$MSGFMT" ] || die "未找到 msgfmt（gettext），请执行: $SUDO ${INSTALL_CMD[*]} gettext  或改用 --no-localize"
    info "msgfmt: $MSGFMT"
fi

CMAKE_ARGS=(
    -S "$ROOT" -B "$BUILD_DIR" -G "$CMAKE_GENERATOR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DTILES="$TILES" -DCURSES="$CURSES" -DSOUND="$SOUND"
    -DLOCALIZE="$LOCALIZE" -DLANGUAGES="$LANGUAGES"
    -DTESTS="$TESTS" -DBACKTRACE="$BACKTRACE" -DLIBBACKTRACE="$LIBBACKTRACE"
    -DUSE_HOME_DIR="$USE_HOME_DIR" -DUSE_PREFIX_DATA_DIR="$USE_PREFIX_DATA_DIR"
    -DDYNAMIC_LINKING=ON
)
[ "$LOCALIZE" = "ON" ] && CMAKE_ARGS+=(-DGETTEXT_MSGFMT_BINARY="$MSGFMT")

# fork 的 curses 构建也需要 SDL2 头文件（cursesdef.h -> sdl_wrappers.h -> <SDL.h>），
# 但 CMake 只在 TILES=ON 时才添加 SDL2 的 include 路径，这里手动补上。
# 逐个查询，避免某个 .pc（如 SDL2_ttf）缺失导致整条 pkg-config 返回空。
SDL_CFLAGS=""
for m in sdl2 SDL2_image SDL2_ttf; do
    c="$(pkg-config --cflags "$m" 2>/dev/null)"
    [ -n "$c" ] && SDL_CFLAGS="$SDL_CFLAGS $c"
done
SDL_CFLAGS="$(echo "$SDL_CFLAGS" | tr -s ' ' | sed 's/^ //')"
if [ -n "$SDL_CFLAGS" ]; then
    info "SDL2 include 路径: $SDL_CFLAGS"
else
    warn "未检测到 SDL2 开发包（pkg-config 找不到 sdl2），curses 构建会找不到 <SDL.h>"
    warn "请先执行: ./build_linux.sh --install-deps  安装 libsdl2-dev 等"
fi

# 关 -Werror + 补 SDL2 include：写进 CMAKE_CXX_FLAGS_<CONFIG>。
# 注意：命令行 -D 会【覆盖】该变量的默认值（如 Release 的 -O3 -DNDEBUG），
# 因此这里手动把对应构建类型的默认优化 flag 拼回来，避免 Release 变成无优化。
CFG_UPPER="$(echo "$BUILD_TYPE" | tr '[:lower:]' '[:upper:]')"
case "$CFG_UPPER" in
    RELEASE)        CFG_DEFAULT="-O3 -DNDEBUG" ;;
    DEBUG)          CFG_DEFAULT="-g" ;;
    RELWITHDEBINFO) CFG_DEFAULT="-O2 -g -DNDEBUG" ;;
    MINSIZEREL)     CFG_DEFAULT="-Os -DNDEBUG" ;;
    *)              CFG_DEFAULT="" ;;
esac
EXTRA_FLAGS=""
if [ "$WERROR" = "0" ]; then
    warn "已关闭 -Werror（fork 自定义代码可能产生警告，避免编译失败）"
    EXTRA_FLAGS="$EXTRA_FLAGS -Wno-error"
fi
# GCC 专属：fork 代码（面向 clang/MSVC 写）在 GCC 下会触发默认即报错的 permerror，单独关掉
if [ "$IS_GCC" = "1" ]; then
    EXTRA_FLAGS="$EXTRA_FLAGS -Wno-changes-meaning"
fi
[ -n "$SDL_CFLAGS" ] && EXTRA_FLAGS="$EXTRA_FLAGS $SDL_CFLAGS"
CMAKE_ARGS+=("-DCMAKE_CXX_FLAGS_${CFG_UPPER}=${CFG_DEFAULT}${EXTRA_FLAGS}")

# ============================================================
echo
echo "[4/6] CMake 配置"
# ============================================================
if [ "$DO_CLEAN" = "1" ] && [ -d "$BUILD_DIR" ]; then
    info "清空 $BUILD_DIR"
    rm -rf "$BUILD_DIR"
fi

cmake "${CMAKE_ARGS[@]}" || {
    echo
    err "CMake 配置失败。常见原因："
    err "  1) 依赖没装全     -> ./build_linux.sh --install-deps"
    err "  2) 换过生成器     -> 加 --clean 重新配置"
    err "  3) 其他问题       -> 看上方 CMake 原始报错"
    exit 1
}
ok "CMake 配置完成"

if [ "$CONFIGURE_ONLY" = "1" ]; then
    echo
    title "============================================================"
    title " 配置成功（--configure-only，未编译）"
    title "============================================================"
    echo " 构建目录: $BUILD_DIR"
    echo " 继续编译: ./build_linux.sh"
    exit 0
fi

# ============================================================
echo
echo "[5/6] 编译（${JOBS} 线程，首次约需 10-30 分钟）"
# ============================================================
BUILD_START="$(date +%s)"
cmake --build "$BUILD_DIR" --config "$BUILD_TYPE" --parallel "$JOBS" || {
    echo
    err "编译失败。请把最后几段 error 输出发出来排查。"
    exit 1
}
BUILD_END="$(date +%s)"
ok "编译完成，耗时 $(( (BUILD_END - BUILD_START) / 60 )) 分 $(( (BUILD_END - BUILD_START) % 60 )) 秒"

# ASCII 版产物名为 cataclysm（tiles 版才是 cataclysm-tiles）
EXE=""
for p in "$BUILD_DIR/src/cataclysm" "$BUILD_DIR/cataclysm" \
         "$BUILD_DIR/src/$BUILD_TYPE/cataclysm" "$ROOT/cataclysm"; do
    [ -f "$p" ] && { EXE="$p"; break; }
done
[ -z "$EXE" ] && EXE="$(find "$BUILD_DIR" -name 'cataclysm' -type f -perm -u+x 2>/dev/null | head -1)"
[ -n "$EXE" ] || die "未找到编译产物 cataclysm"
ok "可执行文件: $EXE"

# ============================================================
echo
echo "[6/6] 生成启动脚本"
# ============================================================
mkdir -p "$DIST_DIR"
cat > "$RUNNER" <<RUNNER
#!/bin/sh
# CDDA-Breeze ASCII 版启动脚本
# 必须 cd 到仓库根目录，才能用相对路径找到 data / lang
DIR="\$(cd "\$(dirname "\$0")" && pwd)"
cd "\$DIR/../.." || cd "$ROOT" || exit 1
exec "\$DIR/../build/linux-${BUILD_TYPE,,}/src/cataclysm" "\$@"
RUNNER
chmod +x "$RUNNER"
ok "已生成启动脚本: $RUNNER"

echo
title "============================================================"
title " 构建成功（ASCII / curses 字符版）"
title "============================================================"
echo " 二进制:   $EXE"
echo " 构建:     $BUILD_DIR"
echo " 启动:     $RUNNER"
echo
echo " 运行方式（ASCII 版需从含 data/lang 的目录启动）："
echo "   1) $RUNNER"
echo "   2) 或: cd $ROOT && $EXE"
echo
echo " 存档/配置: ~/.cataclysm-dda/"
echo

[ "$RUN_AFTER" = "1" ] && { info "正在启动游戏..."; ( cd "$ROOT" && exec "$EXE" ); }

exit 0
