#!/usr/bin/env bash
#
# CDDA-Breeze macOS 一键构建脚本
#
# 全流程：检查/安装依赖 -> CMake 配置 -> 编译 -> 打包成可双击运行的 .app
#
# 基本用法：
#   ./build_mac.sh                  # 装依赖 + Release 编译 + 打包 out/dist/Cataclysm.app
#   ./build_mac.sh --install-deps   # 只安装依赖，不编译
#   ./build_mac.sh --help           # 查看全部参数
#
set -uo pipefail

# ========================= 默认配置 =========================
BUILD_TYPE="Release"
JOBS=""
DO_CLEAN=0
MAKE_DIST=1
COPY_DATA=0
BUNDLE_DYLIBS=1
DO_SIGN=1
SIGN_IDENTITY="-"          # ad-hoc 签名：本机可直接运行；分发给他人需用开发者证书
DO_RUN=0
WERROR=0                   # 默认关闭 -Werror：Apple clang 17 会对本项目代码报新错误
TILES=ON
SOUND=ON
LOCALIZE=ON
LANGUAGES="zh_CN"
TESTS=OFF
ARCH=""
DEPLOY_TARGET=""
CMAKE_GENERATOR=""
APP_NAME="Cataclysm"
INSTALL_DEPS_ONLY=0
CONFIGURE_ONLY=0
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

用法: build_mac.sh [release | debug | relwithdebinfo | minsizerel] [选项]

构建类型（默认 release）:
  release               发布版（推荐）
  debug                 调试版（二进制输出到仓库根目录）
  relwithdebinfo        带调试信息的发布版
  minsizerel            最小体积发布版

依赖相关:
  --install-deps        只检查并安装依赖，然后退出（不编译）
  --deps                同 --install-deps

通用选项:
  --jobs N              并行编译线程数（默认 = CPU 核心数）
  --clean               清空构建目录后重新编译
  --no-dist             只编译，不打包 .app
  --copy-data           把 data/gfx/lang 复制进 .app（默认软链接，省约 110MB）
  --no-bundle           不把 Homebrew 的 dylib 拷进 .app（依赖本机 /opt/homebrew）
  --no-sign             跳过代码签名
  --sign-identity ID    指定签名身份，例如 "Apple Development: xxx (TEAMID)"
  --run                 构建完成后直接启动游戏
  --configure-only      只跑 CMake 配置就退出（用于排查依赖问题）
  --help                显示本帮助

构建选项:
  --curses              编译 curses 字符版（默认 tiles 图形版）
  --no-sound            关闭音效支持（不需要 SDL2_mixer）
  --no-localize         关闭多语言（不需要 gettext）
  --languages LIST      要编译的语言，空格分隔，如 "zh_CN zh_TW"（默认 zh_CN）
  --tests ON|OFF        是否编译单元测试（默认 OFF）
  --no-werror           关闭 -Werror（默认就是关闭的）
  --werror              开启 -Werror（想把警告当错误处理时用）
  --arch arm64|x86_64|universal   目标架构（默认本机；universal 需依赖也是通用二进制）
  --deploy-target VER   设置 CMAKE_OSX_DEPLOYMENT_TARGET，例如 12.0
  --generator NAME      CMake 生成器（默认优先 Ninja，其次 Make）
  --app-name NAME       .app 名称（默认 Cataclysm）

产物:
  构建目录: out/build/mac-<arch>
  应用:     out/dist/<AppName>.app
  存档目录: ~/Library/Application Support/Cataclysm/

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
        --no-dist)        MAKE_DIST=0;    shift ;;
        --copy-data)      COPY_DATA=1;    shift ;;
        --no-bundle)      BUNDLE_DYLIBS=0; shift ;;
        --no-sign)        DO_SIGN=0;      shift ;;
        --sign-identity)  [ $# -ge 2 ] || die "--sign-identity 缺少参数"; SIGN_IDENTITY="$2"; shift 2 ;;
        --run)            DO_RUN=1;       shift ;;
        --configure-only) CONFIGURE_ONLY=1; MAKE_DIST=0; shift ;;
        --install-deps|--deps) INSTALL_DEPS_ONLY=1; shift ;;
        --curses)         TILES=OFF;      shift ;;
        --no-sound)       SOUND=OFF;      shift ;;
        --no-localize)    LOCALIZE=OFF;   shift ;;
        --languages)      [ $# -ge 2 ] || die "--languages 缺少参数"; LANGUAGES="$2"; shift 2 ;;
        --tests)          [ $# -ge 2 ] || die "--tests 缺少参数"; TESTS="$2"; shift 2 ;;
        --werror)         WERROR=1;       shift ;;
        --no-werror)      WERROR=0;       shift ;;
        --arch)           [ $# -ge 2 ] || die "--arch 缺少参数"; ARCH="$2"; shift 2 ;;
        --deploy-target)  [ $# -ge 2 ] || die "--deploy-target 缺少参数"; DEPLOY_TARGET="$2"; shift 2 ;;
        --generator)      [ $# -ge 2 ] || die "--generator 缺少参数"; CMAKE_GENERATOR="$2"; shift 2 ;;
        --app-name)       [ $# -ge 2 ] || die "--app-name 缺少参数"; APP_NAME="$2"; shift 2 ;;
        --help|-h)        SHOW_HELP=1;    shift ;;
        *)                err "未知参数: $1"; usage; exit 1 ;;
    esac
done

if [ "$SHOW_HELP" = "1" ]; then usage; exit 0; fi

# ---------- 路径 ----------
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NATIVE_ARCH="$(uname -m)"
[ -n "$ARCH" ] || ARCH="$NATIVE_ARCH"
BUILD_DIR="$ROOT/out/build/mac-$ARCH"
DIST_DIR="$ROOT/out/dist"
APP="$DIST_DIR/$APP_NAME.app"

title "============================================================"
title " CDDA-Breeze macOS 构建"
title " 仓库: $ROOT"
title " 类型: $BUILD_TYPE   架构: $ARCH"
title "============================================================"
echo

# ============================================================
echo "[1/6] 检查环境"
# ============================================================
[ "$(uname -s)" = "Darwin" ] || die "此脚本只能在 macOS 上运行（当前: $(uname -s)）"
info "macOS $(sw_vers -productVersion) / $NATIVE_ARCH"

command -v brew >/dev/null 2>&1 || die "未找到 Homebrew，请先安装: https://brew.sh"
BREW_PREFIX="$(brew --prefix)"
info "Homebrew: $BREW_PREFIX"

# 关掉 brew 自动更新，否则首次运行会卡在 "Updating Homebrew..." 好几分钟
export HOMEBREW_NO_AUTO_UPDATE="${HOMEBREW_NO_AUTO_UPDATE:-1}"
export HOMEBREW_NO_ANALYTICS="${HOMEBREW_NO_ANALYTICS:-1}"
export HOMEBREW_NO_ENV_HINTS=1
export HOMEBREW_NO_INSTALL_CLEANUP=1
# 记住用户是否自己指定了镜像（影响安装失败后的重试策略）
if [ -n "${HOMEBREW_BOTTLE_DOMAIN:-}" ]; then BOTTLE_DOMAIN_BY_USER=1; else BOTTLE_DOMAIN_BY_USER=0; fi

if ! command -v cmake >/dev/null 2>&1; then
    info "未找到 cmake，正在安装..."
    brew install cmake || die "cmake 安装失败，请手动执行: brew install cmake"
    hash -r
fi
command -v cmake >/dev/null 2>&1 || die "cmake 不可用，请手动执行: brew install cmake"
info "cmake: $(cmake --version | head -1)"

command -v clang++ >/dev/null 2>&1 || die "未找到 C++ 编译器，请先安装 Xcode 命令行工具: xcode-select --install"
info "编译器: $(clang++ --version | head -1)"

# ============================================================
echo
echo "[2/6] 检查并安装依赖"
# ============================================================

# 注意：brew --prefix 对未安装的配方也可能返回路径，必须再确认真的装了
dep_installed() { brew list --versions "$1" >/dev/null 2>&1; }

dep_prefix() {
    local p
    for p in "$(brew --prefix "$1" 2>/dev/null)" "$BREW_PREFIX/opt/$1" "$BREW_PREFIX"; do
        if [ -n "$p" ] && [ -d "$p" ]; then echo "$p"; return 0; fi
    done
    echo "$BREW_PREFIX"
}

# brew install；默认镜像失败时自动换腾讯镜像重试一次
brew_install() {
    if brew install "$@"; then return 0; fi
    if [ "$BOTTLE_DOMAIN_BY_USER" = "0" ]; then
        warn "默认镜像安装失败，改用腾讯镜像重试..."
        if HOMEBREW_BOTTLE_DOMAIN="https://mirrors.cloud.tencent.com/homebrew-bottles" \
           brew install "$@"; then
            return 0
        fi
    fi
    return 1
}

# 构建所需依赖
DEPS=()
if [ "$TILES" = "ON" ]; then
    # Homebrew 的 sdl2 实际是 sdl2-compat：只提供 SDL2 API 外壳，
    # 运行时用 dlopen("libSDL3.dylib") 加载真正的实现库，所以必须一并安装 sdl3。
    DEPS+=(sdl2 sdl2_image sdl2_ttf sdl3)
    CURSES=OFF
    [ "$SOUND" = "ON" ] && DEPS+=(sdl2_mixer)
else
    DEPS+=(ncurses)
    CURSES=ON
fi
[ "$LOCALIZE" = "ON" ] && DEPS+=(gettext)
TOOLS=(ninja ccache pkg-config)

MISSING=()
for f in "${DEPS[@]}" "${TOOLS[@]}"; do
    if dep_installed "$f" && [ -n "$(dep_prefix "$f")" ]; then
        info "  $f: 已安装"
    else
        info "  $f: 缺失"
        MISSING+=("$f")
    fi
done

if [ ${#MISSING[@]} -gt 0 ]; then
    info "正在安装 ${#MISSING[@]} 个依赖: ${MISSING[*]}"
    info "（首次安装需要下载，请耐心等待）"
    if ! brew_install "${MISSING[@]}"; then
        echo
        err "以下依赖安装失败，请手动执行后重新运行本脚本："
        err "    brew install ${MISSING[*]}"
        err "若卡在下载，可换镜像："
        err "    export HOMEBREW_BOTTLE_DOMAIN=https://mirrors.cloud.tencent.com/homebrew-bottles"
        exit 1
    fi
    hash -r
    FAILED=()
    for f in "${DEPS[@]}"; do
        dep_installed "$f" || FAILED+=("$f")
    done
    [ ${#FAILED[@]} -eq 0 ] || die "必需依赖仍未就绪: ${FAILED[*]}"
    ok "依赖安装完成"
else
    ok "依赖齐全"
fi

if [ "$INSTALL_DEPS_ONLY" = "1" ]; then
    echo
    title "============================================================"
    title " 依赖已就绪（--install-deps，未编译）"
    title "============================================================"
    echo " 下一步: ./build_mac.sh"
    exit 0
fi

# ============================================================
echo
echo "[3/6] 准备工具链"
# ============================================================

# keg-only 的配方不进 PATH，这里补上
for f in ninja gettext ccache pkg-config bison flex; do
    p="$(dep_prefix "$f")"
    [ -n "$p" ] && [ -d "$p/bin" ] && PATH="$p/bin:$PATH"
done
export PATH

if [ -z "$CMAKE_GENERATOR" ]; then
    if command -v ninja >/dev/null 2>&1; then
        CMAKE_GENERATOR="Ninja"
    else
        CMAKE_GENERATOR="Unix Makefiles"
        info "未找到 ninja，使用 Unix Makefiles"
    fi
fi
if [ "$CMAKE_GENERATOR" = "Ninja" ] && ! command -v ninja >/dev/null 2>&1; then
    warn "ninja 不可用，改用 Unix Makefiles"
    CMAKE_GENERATOR="Unix Makefiles"
fi
info "生成器: $CMAKE_GENERATOR"

[ -n "$JOBS" ] || JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
info "并行任务: $JOBS"

MSGFMT=""
for p in "$BREW_PREFIX/bin/msgfmt" "$BREW_PREFIX/opt/gettext/bin/msgfmt" /usr/local/bin/msgfmt; do
    [ -x "$p" ] && { MSGFMT="$p"; break; }
done
[ -z "$MSGFMT" ] && command -v msgfmt >/dev/null 2>&1 && MSGFMT="$(command -v msgfmt)"
if [ "$LOCALIZE" = "ON" ]; then
    [ -n "$MSGFMT" ] || die "未找到 msgfmt，请执行: brew install gettext"
    info "msgfmt: $MSGFMT"
fi

PREFIX_PATH="$BREW_PREFIX"
for f in "${DEPS[@]}"; do
    p="$(dep_prefix "$f")"
    [ -n "$p" ] && [ "$p" != "$BREW_PREFIX" ] && PREFIX_PATH="$PREFIX_PATH;$p"
done
info "CMAKE_PREFIX_PATH: $PREFIX_PATH"

CMAKE_ARGS=(
    -S "$ROOT" -B "$BUILD_DIR" -G "$CMAKE_GENERATOR"
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DCMAKE_PREFIX_PATH="$PREFIX_PATH"
    -DTILES="$TILES" -DSOUND="$SOUND" -DCURSES="$CURSES"
    -DTESTS="$TESTS" -DRELEASE=ON
    -DLOCALIZE="$LOCALIZE" -DLANGUAGES="$LANGUAGES"
    # 关键：不写死 PREFIX，让 .app 用相对路径找 data/gfx/lang
    -DUSE_PREFIX_DATA_DIR=OFF
    -DUSE_HOME_DIR=ON
    -DBACKTRACE=ON -DLIBBACKTRACE=OFF
)
[ "$LOCALIZE" = "ON" ] && CMAKE_ARGS+=(-DGETTEXT_MSGFMT_BINARY="$MSGFMT")
[ "$ARCH" = "universal" ] && CMAKE_ARGS+=(-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64")
if [ -n "$ARCH" ] && [ "$ARCH" != "universal" ] && [ "$ARCH" != "$NATIVE_ARCH" ]; then
    CMAKE_ARGS+=(-DCMAKE_OSX_ARCHITECTURES="$ARCH")
fi
[ -n "$DEPLOY_TARGET" ] && CMAKE_ARGS+=(-DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOY_TARGET")

# 关 -Werror：写进 CMAKE_CXX_FLAGS_<CONFIG>，确保排在项目自己的 -Werror 之后
if [ "$WERROR" = "0" ]; then
    warn "已关闭 -Werror"
    CMAKE_ARGS+=("-DCMAKE_CXX_FLAGS_$(echo "$BUILD_TYPE" | tr '[:lower:]' '[:upper:]')=-Wno-error")
fi

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
    err "  1) 依赖没装全     -> ./build_mac.sh --install-deps"
    err "  2) 换过生成器/架构 -> 加 --clean 重新配置"
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
    echo " 继续编译: ./build_mac.sh"
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

EXE=""
for p in "$BUILD_DIR/src/cataclysm-tiles" "$BUILD_DIR/cataclysm-tiles" \
         "$BUILD_DIR/src/$BUILD_TYPE/cataclysm-tiles" "$ROOT/cataclysm-tiles"; do
    [ -f "$p" ] && { EXE="$p"; break; }
done
[ -z "$EXE" ] && EXE="$(find "$BUILD_DIR" -name 'cataclysm-tiles' -type f -perm -u+x 2>/dev/null | head -1)"
if [ -z "$EXE" ] && [ "$TILES" = "OFF" ]; then
    EXE="$(find "$BUILD_DIR" -name 'cataclysm' -type f -perm -u+x 2>/dev/null | head -1)"
fi
[ -n "$EXE" ] || die "未找到编译产物 cataclysm-tiles"
ok "可执行文件: $EXE"

if [ "$MAKE_DIST" = "0" ]; then
    echo
    title "============================================================"
    title " 编译成功（未打包）"
    title "============================================================"
    echo " 二进制: $EXE"
    echo " 运行前请 cd 到含 data / gfx / lang 的目录，例如："
    echo "   cd $ROOT && $EXE"
    exit 0
fi

# ============================================================
echo
echo "[6/6] 打包 .app"
# ============================================================
CONTENTS="$APP/Contents"
MACOS_DIR="$CONTENTS/MacOS"
RES_DIR="$CONTENTS/Resources"
FW_DIR="$CONTENTS/Frameworks"

rm -rf "$APP"
mkdir -p "$MACOS_DIR" "$RES_DIR"

cp -L "$EXE" "$MACOS_DIR/cataclysm-tiles"
chmod +x "$MACOS_DIR/cataclysm-tiles"
info "已复制可执行文件"

# 6.2 把依赖的 dylib 拷进 Contents/Frameworks 并重写 install_name。
#     依赖链是多层的：有的库用绝对路径引用，有的用 @rpath/ 引用，两种都要处理。
if [ "$BUNDLE_DYLIBS" = "1" ]; then
    mkdir -p "$FW_DIR"
    info "收集依赖的动态库..."

    # 按文件名定位库（先找已打包的，再找 brew 的）
    find_lib() {
        local base="$1" p
        [ -f "$FW_DIR/$base" ] && { echo "$FW_DIR/$base"; return 0; }
        for p in "$BREW_PREFIX/lib/$base" "$BREW_PREFIX"/opt/*/lib/$base /usr/local/lib/$base; do
            [ -f "$p" ] && { echo "$p"; return 0; }
        done
        return 1
    }

    # 只取真正的依赖（LC_LOAD_DYLIB）。
    # 注意不能用 otool -L 直接跳首行：dylib 的第 2 行是它自己的 LC_ID_DYLIB，不是依赖。
    list_deps() {
        otool -l "$1" 2>/dev/null | awk '
            /cmd LC_LOAD_DYLIB/ { want = 1; next }
            want && /^[[:space:]]*name / { print $2; want = 0 }
        '
    }

    # 运行时才 dlopen 的库：不在 LC_LOAD_DYLIB 里，只能扫字符串找出裸文件名。
    # 典型例子：sdl2-compat 用 dlopen("libSDL3.dylib") 加载真正的 SDL3 实现。
    list_dlopen_libs() {
        strings -a "$1" 2>/dev/null | awk '/^lib[A-Za-z0-9_.+-]*\.dylib$/ { print }' | sort -u
    }

    ROUND=0
    CHANGED=1
    while [ "$CHANGED" = "1" ] && [ "$ROUND" -lt 15 ]; do
        CHANGED=0
        ROUND=$((ROUND + 1))
        SCAN=("$MACOS_DIR/cataclysm-tiles")
        while IFS= read -r lib; do
            [ -n "$lib" ] && SCAN+=("$lib")
        done < <(find "$FW_DIR" -type f -name '*.dylib' 2>/dev/null)

        for f in "${SCAN[@]}"; do
            [ -f "$f" ] || continue
            while IFS= read -r dep; do
                case "$dep" in
                    /System/*|/usr/lib/*|@executable_path/*|@loader_path/*) continue ;;
                    /*) src="$dep" ;;
                    @rpath/*)
                        src="$(find_lib "$(basename "$dep")")" || {
                            warn "无法定位 $(basename "$dep")"
                            continue
                        } ;;
                    *) continue ;;
                esac
                [ -f "$src" ] || continue
                base="$(basename "$src")"
                if [ ! -f "$FW_DIR/$base" ] && cp -L "$src" "$FW_DIR/$base" 2>/dev/null; then
                    chmod u+w "$FW_DIR/$base"
                    CHANGED=1
                fi
                # 统一改写成 @rpath/<文件名>，再靠下面的 rpath 定位到 Frameworks
                if [ -f "$FW_DIR/$base" ] && [ "$dep" != "@rpath/$base" ]; then
                    install_name_tool -change "$dep" "@rpath/$base" "$f" >/dev/null 2>&1 && CHANGED=1
                fi
            done < <(list_deps "$f")
        done

        # 第二遍：把运行时 dlopen 的库也补进来（补进来的库下一轮会继续解析它自己的依赖）
        for f in "${SCAN[@]}"; do
            [ -f "$f" ] || continue
            while IFS= read -r name; do
                [ -n "$name" ] || continue
                [ -f "$FW_DIR/$name" ] && continue
                src="$(find_lib "$name")" || continue
                if cp -L "$src" "$FW_DIR/$name" 2>/dev/null; then
                    chmod u+w "$FW_DIR/$name"
                    install_name_tool -id "@rpath/$name" "$FW_DIR/$name" >/dev/null 2>&1 || true
                    info "  补充运行时加载的库: $name"
                    CHANGED=1
                fi
            done < <(list_dlopen_libs "$f")
        done
    done

    # 让 @rpath/xxx 都能落到位（主程序和每个打包库都要设）
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$MACOS_DIR/cataclysm-tiles" >/dev/null 2>&1 || true
    while IFS= read -r lib; do
        install_name_tool -add_rpath "@loader_path" "$lib" >/dev/null 2>&1 || true
        install_name_tool -add_rpath "@executable_path/../Frameworks" "$lib" >/dev/null 2>&1 || true
    done < <(find "$FW_DIR" -type f -name '*.dylib' 2>/dev/null)

    # 校验：确认没有指向 Frameworks 之外的依赖
    MISSING_LIBS=()
    while IFS= read -r f; do
        while IFS= read -r dep; do
            case "$dep" in
                @rpath/*)
                    b="$(basename "$dep")"
                    [ -f "$FW_DIR/$b" ] || MISSING_LIBS+=("$b") ;;
                /*)
                    case "$dep" in /System/*|/usr/lib/*) ;; *) MISSING_LIBS+=("$dep") ;; esac ;;
            esac
        done < <(list_deps "$f")
        # 运行时 dlopen 的库也要在 bundle 里
        while IFS= read -r name; do
            [ -n "$name" ] || continue
            [ -f "$FW_DIR/$name" ] && continue
            # 只把“本机确实存在、却没打进 bundle”的算问题。
            # libvulkan / libEGL / libMoltenVK / libsteam_api 之类是 SDL 的机会性后端，
            # 本机没装时压根不会被加载，缺失属正常，不报警。
            find_lib "$name" >/dev/null 2>&1 && MISSING_LIBS+=("$name (dlopen)")
        done < <(list_dlopen_libs "$f")
    done < <(find "$MACOS_DIR" "$FW_DIR" -type f -name '*.dylib' 2>/dev/null; echo "$MACOS_DIR/cataclysm-tiles")
    if [ ${#MISSING_LIBS[@]} -gt 0 ]; then
        warn "以下依赖未能打包，运行时可能报错: $(printf '%s ' "${MISSING_LIBS[@]}" | sort -u | tr '\n' ' ')"
    fi

    N_LIBS="$(find "$FW_DIR" -type f -name '*.dylib' 2>/dev/null | wc -l | tr -d ' ')"
    ok "已打包 $N_LIBS 个动态库到 Contents/Frameworks"
else
    warn "跳过 dylib 打包，运行时依赖本机 $BREW_PREFIX"
fi

# 6.3 资源目录：默认软链接，--copy-data 时复制
for d in data gfx lang; do
    if [ "$COPY_DATA" = "1" ]; then
        cp -R "$ROOT/$d" "$RES_DIR/"
    else
        ln -sfh "$ROOT/$d" "$RES_DIR/$d"
    fi
done
if [ "$COPY_DATA" = "1" ]; then
    ok "已复制 data / gfx / lang"
else
    ok "已软链接 data / gfx / lang（要拷贝/分发 .app 请改用 --copy-data）"
fi

# 6.4 图标
if [ -d "$ROOT/build-data/osx/AppIcon.iconset" ] && command -v iconutil >/dev/null 2>&1; then
    iconutil -c icns "$ROOT/build-data/osx/AppIcon.iconset" -o "$RES_DIR/AppIcon.icns" >/dev/null 2>&1 \
        && ok "已生成 AppIcon.icns"
fi

# 6.5 启动器：二进制在 Contents/MacOS，但必须 cd 到 Resources 才能用相对路径找到 data/gfx/lang
cat > "$MACOS_DIR/$APP_NAME" <<LAUNCHER
#!/bin/sh
# CDDA-Breeze macOS 启动器
V_DIR="\$(cd "\$(dirname "\$0")" && pwd)"
V_RES="\$V_DIR/../Resources"
cd "\$V_RES" || exit 1
export DYLD_LIBRARY_PATH="\$V_DIR/../Frameworks:\${DYLD_LIBRARY_PATH:-}"
export DYLD_FRAMEWORK_PATH="\$V_DIR/../Frameworks:\${DYLD_FRAMEWORK_PATH:-}"
exec "\$V_DIR/cataclysm-tiles" "\$@"
LAUNCHER
chmod +x "$MACOS_DIR/$APP_NAME"

# 6.6 Info.plist
GIT_VER="$(git -C "$ROOT" describe --tags --always --dirty --match '[0-9A-Z]*.[0-9A-Z]*' 2>/dev/null || echo '0.G')"
cat > "$CONTENTS/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleExecutable</key>
	<string>$APP_NAME</string>
	<key>CFBundleIconFile</key>
	<string>AppIcon.icns</string>
	<key>CFBundleIdentifier</key>
	<string>com.cataclysmdda.breeze.cataclysm</string>
	<key>CFBundleInfoDictionaryVersion</key>
	<string>6.0</string>
	<key>CFBundleName</key>
	<string>$APP_NAME</string>
	<key>CFBundlePackageType</key>
	<string>APPL</string>
	<key>CFBundleShortVersionString</key>
	<string>$GIT_VER</string>
	<key>CFBundleSignature</key>
	<string>????</string>
	<key>CFBundleVersion</key>
	<string>$GIT_VER</string>
	<key>LSApplicationCategoryType</key>
	<string>public.app-category.role-playing-games</string>
	<key>NSHighResolutionCapable</key>
	<true/>
	<key>NSPrincipalClass</key>
	<string>NSApplication</string>
</dict>
</plist>
PLIST

# 6.7 命令行启动脚本
cat > "$DIST_DIR/run-cataclysm.sh" <<RUNNER
#!/bin/sh
# 命令行运行（会 cd 到 Resources 目录，保证 data/gfx/lang 用相对路径能找到）
DIR="\$(cd "\$(dirname "\$0")" && pwd)"
cd "\$DIR/$APP_NAME.app/Contents/Resources" || exit 1
exec "\$DIR/$APP_NAME.app/Contents/MacOS/cataclysm-tiles" "\$@"
RUNNER
chmod +x "$DIST_DIR/run-cataclysm.sh"

# 6.8 签名
if [ "$DO_SIGN" = "1" ]; then
    info "签名中（身份: ${SIGN_IDENTITY}）..."
    xattr -dr com.apple.quarantine "$APP" >/dev/null 2>&1 || true
    if ! codesign --force --deep --sign "$SIGN_IDENTITY" "$APP" >/dev/null 2>&1; then
        while IFS= read -r f; do
            codesign --force --sign "$SIGN_IDENTITY" "$f" >/dev/null 2>&1 || true
        done < <(find "$MACOS_DIR" "$FW_DIR" -type f 2>/dev/null)
        codesign --force --sign "$SIGN_IDENTITY" "$APP" >/dev/null 2>&1 || true
        warn "签名未完全成功（ad-hoc 一般不影响本机运行；分发请用开发者证书）"
    else
        ok "签名完成"
    fi
fi

echo
title "============================================================"
title " 构建成功"
title "============================================================"
echo " 应用:   $APP"
echo " 版本:   $GIT_VER"
echo " 构建:   $BUILD_DIR"
echo
echo " 运行方式："
echo "   1) 双击 $APP_NAME.app"
echo "   2) 命令行: $DIST_DIR/run-cataclysm.sh"
echo "   3) 或: cd $APP/Contents/Resources && ../MacOS/cataclysm-tiles"
echo
echo " 存档/配置: ~/Library/Application Support/Cataclysm/"
echo

[ "$DO_RUN" = "1" ] && { info "正在启动游戏..."; open "$APP"; }

exit 0
