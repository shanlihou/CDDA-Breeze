@echo off
chcp 936 >nul 2>&1
setlocal EnableDelayedExpansion
title CDDA-Breeze Windows Build

REM ========================= 配置区 =========================
set "DEPS_DIR=%~dp0deps"
set "BUILD_TYPE=Release"
set "JOBS=16"
set "LOCALIZE=ON"
set "LANGUAGES=zh_CN"
set "GENERATOR=Ninja"
set "DO_CLEAN=0"
set "MAKE_DIST=1"
set "COPY_DATA=0"
set "PAUSE_AT_END=1"
set "MSGFMT_EXE="
set "SDL2_VER=2.32.10"
set "SDL2_IMAGE_VER=2.8.12"
set "SDL2_MIXER_VER=2.8.2"
set "SDL2_TTF_VER=2.24.0"
set "ZLIB_VER=1.3.1"
set "CURL_VER=8_22_0"
set "ZSTD_VER=1.5.7"
set "GETTEXT_PKG=mingw-w64-x86_64-gettext-0.22.4-3"
REM ==========================================================

:parse
if "%~1"=="" goto :afterparse
if /i "%~1"=="release"          ( set "BUILD_TYPE=Release" & shift & goto :parse )
if /i "%~1"=="debug"            ( set "BUILD_TYPE=Debug" & shift & goto :parse )
if /i "%~1"=="relwithdebinfo"   ( set "BUILD_TYPE=RelWithDebInfo" & shift & goto :parse )
if /i "%~1"=="--jobs"           ( set "JOBS=%~2" & shift & shift & goto :parse )
if /i "%~1"=="--deps"           ( set "DEPS_DIR=%~2" & shift & shift & goto :parse )
if /i "%~1"=="--msgfmt"         ( set "MSGFMT_EXE=%~2" & shift & shift & goto :parse )
if /i "%~1"=="--generator"      ( set "GENERATOR=%~2" & shift & shift & goto :parse )
if /i "%~1"=="--clean"          ( set "DO_CLEAN=1" & shift & goto :parse )
if /i "%~1"=="--no-dist"        ( set "MAKE_DIST=0" & shift & goto :parse )
if /i "%~1"=="--copy-data"      ( set "COPY_DATA=1" & shift & goto :parse )
if /i "%~1"=="--no-pause"       ( set "PAUSE_AT_END=0" & shift & goto :parse )
if /i "%~1"=="--help"           ( goto :usage )
if /i "%~1"=="-h"               ( goto :usage )
echo [错误] 未知参数: %~1
goto :usage

:usage
echo.
echo 用法: build_win.bat [release ^| debug ^| relwithdebinfo] [选项]
echo.
echo   release              构建 Release 版本（默认）
echo   debug                构建 Debug 版本
echo   relwithdebinfo       构建带调试信息的 Release 版本
echo.
echo   --jobs N             并行编译线程数（默认 16）
echo   --deps DIR           依赖目录（默认 %~dp0deps）
echo   --msgfmt PATH        指定 msgfmt.exe，用于编译 .po 语言包
echo   --generator NAME     CMake 生成器（默认 Ninja，找不到自动改用 VS2022）
echo   --clean              先清空构建目录再编译
echo   --no-dist            只编译，不生成运行目录
echo   --copy-data          运行目录里复制 data/gfx/lang 而不是建目录链接
echo   --no-pause           结束时不停留（从终端调用时用）
echo   --help               显示本帮助
echo.
echo 说明:
echo   首次运行会自动下载 SDL2/SDL2_image/SDL2_mixer/SDL2_ttf 官方预编译包，
echo   并用 MSVC 从源码编译 zlib 与 libcurl；同时会下载 MSYS2 的 gettext
   （提供 msgfmt/msgcat 用于编译中文语言包）。之后会自动跳过。
echo   产物: out\dist\cataclysm-tiles.exe
echo.
if "%PAUSE_AT_END%"=="1" pause
exit /b 0

:afterparse
set "ROOT=%~dp0"
if "%ROOT:~-1%"=="\" set "ROOT=%ROOT:~0,-1%"
set "BUILD_DIR=%ROOT%\out\build\win64"
set "DIST_DIR=%ROOT%\out\dist"

echo ============================================================
echo  CDDA-Breeze Windows 构建
echo  仓库: %ROOT%
echo  类型: %BUILD_TYPE%
echo ============================================================

echo.
echo [1/6] 检查工具
where cmake >nul 2>&1
if errorlevel 1 (
    echo   [错误] 未找到 cmake，请安装并加入 PATH
    goto :fail
)
set "CMAKE_BIN="
for /f "delims=" %%i in ('where cmake 2^>nul') do if not defined CMAKE_BIN set "CMAKE_BIN=%%i"
echo   cmake : %CMAKE_BIN%
if /i "%GENERATOR%"=="Ninja" (
    where ninja >nul 2>&1
    if errorlevel 1 (
        echo   未找到 ninja，改用 Visual Studio 17 2022 生成器
        set "GENERATOR=Visual Studio 17 2022"
    ) else (
        for /f "delims=" %%i in ('where ninja 2^>nul') do if not defined NINJA_BIN set "NINJA_BIN=%%i"
        echo   ninja : !NINJA_BIN!
    )
)

echo.
echo [2/6] 准备 MSVC 编译环境
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_INST="
set "VSDEVCMD="
if exist "%VSWHERE%" (
    "%VSWHERE%" -all -products "*" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath > "%TEMP%\cdda_vswhere.txt" 2>nul
    if exist "%TEMP%\cdda_vswhere.txt" set /p VS_INST=<"%TEMP%\cdda_vswhere.txt"
    del /q "%TEMP%\cdda_vswhere.txt" >nul 2>&1
)
if defined VS_INST if exist "%VS_INST%\Common7\Tools\VsDevCmd.bat" set "VSDEVCMD=%VS_INST%\Common7\Tools\VsDevCmd.bat"
if not defined VSDEVCMD (
    for %%p in ("%ProgramFiles%\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat" "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\Common7\Tools\VsDevCmd.bat" "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\Common7\Tools\VsDevCmd.bat" "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat") do (
        if not defined VSDEVCMD if exist %%~p set "VSDEVCMD=%%~p"
    )
)
if not defined VSDEVCMD (
    echo   [错误] 未找到 VsDevCmd.bat，请安装 Visual Studio 的"桌面 C++ 开发"工作负载
    goto :fail
)
echo   VS   : %VSDEVCMD%
call "%VSDEVCMD%" -arch=amd64 -no_logo
where cl >nul 2>&1
if errorlevel 1 (
    echo   [错误] cl.exe 不可用，MSVC 环境初始化失败
    goto :fail
)

REM ---------------------------------------------------------------------------
REM VsDevCmd.bat 靠 reg.exe 探测 Windows SDK。若 reg.exe 被安全策略拦截，
REM INCLUDE/LIB 里就不会有 ucrt/um，编译会报“无法打开包括文件: math.h”。
REM 这里直接在磁盘上找一份可用的 SDK 手工补上。
REM ---------------------------------------------------------------------------
echo !INCLUDE! | find /i "ucrt" >nul 2>&1
if errorlevel 1 (
    set "WINSDK_ROOT=%ProgramFiles(x86)%\Windows Kits\10"
    if not defined WindowsSdkDir set "WindowsSdkDir=!WINSDK_ROOT!\"
    set "WINSDK_VER="
    for /f "delims=" %%v in ('dir /b /ad /o-n "!WINSDK_ROOT!\Include" 2^>nul') do (
        if not defined WINSDK_VER if exist "!WINSDK_ROOT!\Include\%%v\ucrt\stdio.h" set "WINSDK_VER=%%v"
    )
    if not defined WINSDK_VER (
        echo   [错误] 未找到 Windows SDK（ucrt 头文件缺失），请安装 Windows 10 SDK
        goto :fail
    )
    set "INCLUDE=!WINSDK_ROOT!\Include\!WINSDK_VER!\ucrt;!WINSDK_ROOT!\Include\!WINSDK_VER!\um;!WINSDK_ROOT!\Include\!WINSDK_VER!\shared;!WINSDK_ROOT!\Include\!WINSDK_VER!\winrt;!INCLUDE!"
    set "LIB=!WINSDK_ROOT!\Lib\!WINSDK_VER!\ucrt\x64;!WINSDK_ROOT!\Lib\!WINSDK_VER!\um\x64;!LIB!"
    set "PATH=!WINSDK_ROOT!\bin\!WINSDK_VER!\x64;!PATH!"
    set "WindowsSDKVersion=!WINSDK_VER!\"
    echo   [提示] 已手工补上 Windows SDK !WINSDK_VER!
)

echo.
echo [3/6] 准备依赖（首次运行会下载并编译，之后自动跳过）
call :prep_sdl SDL2 "SDL2-%SDL2_VER%" "https://github.com/libsdl-org/SDL/releases/download/release-%SDL2_VER%/SDL2-devel-%SDL2_VER%-VC.zip"
if errorlevel 1 goto :fail
call :prep_sdl SDL2_image "SDL2_image-%SDL2_IMAGE_VER%" "https://github.com/libsdl-org/SDL_image/releases/download/release-%SDL2_IMAGE_VER%/SDL2_image-devel-%SDL2_IMAGE_VER%-VC.zip"
if errorlevel 1 goto :fail
call :prep_sdl SDL2_mixer "SDL2_mixer-%SDL2_MIXER_VER%" "https://github.com/libsdl-org/SDL_mixer/releases/download/release-%SDL2_MIXER_VER%/SDL2_mixer-devel-%SDL2_MIXER_VER%-VC.zip"
if errorlevel 1 goto :fail
call :prep_sdl SDL2_ttf "SDL2_ttf-%SDL2_TTF_VER%" "https://github.com/libsdl-org/SDL_ttf/releases/download/release-%SDL2_TTF_VER%/SDL2_ttf-devel-%SDL2_TTF_VER%-VC.zip"
if errorlevel 1 goto :fail
call :prep_zlib
if errorlevel 1 goto :fail
call :prep_curl
if errorlevel 1 goto :fail
if not defined MSGFMT_EXE (
    call :prep_gettext
    if errorlevel 1 goto :fail
)

echo.
echo [4/6] 检查 msgfmt（用于编译中文语言包）
if not defined MSGFMT_EXE (
    for %%p in ("%DEPS_DIR%\win64\gettext\bin\msgfmt.exe" "%ProgramFiles%\Git\usr\bin\msgfmt.exe" "C:\Program Files\Git\usr\bin\msgfmt.exe" "C:\msys64\usr\bin\msgfmt.exe" "C:\msys64\mingw64\bin\msgfmt.exe") do (
        if not defined MSGFMT_EXE if exist %%~p set "MSGFMT_EXE=%%~p"
    )
)
if not defined MSGFMT_EXE (
    for /f "delims=" %%p in ('where msgfmt 2^>nul') do if not defined MSGFMT_EXE set "MSGFMT_EXE=%%p"
)
if defined MSGFMT_EXE (
    echo   msgfmt: %MSGFMT_EXE%
    set "MSGFMT_ARG=-DGETTEXT_MSGFMT_BINARY=%MSGFMT_EXE%"
) else (
    echo   [错误] 未找到 msgfmt.exe，无法编译语言包
    goto :fail
)

echo.
echo [5/6] 配置 CMake
if "%DO_CLEAN%"=="1" if exist "%BUILD_DIR%" (
    echo   清理 %BUILD_DIR%
    rmdir /s /q "%BUILD_DIR%"
)
set "PREFIX=%DEPS_DIR%\win64\SDL2;%DEPS_DIR%\win64\SDL2_image;%DEPS_DIR%\win64\SDL2_mixer;%DEPS_DIR%\win64\SDL2_ttf;%DEPS_DIR%\win64\zlib;%DEPS_DIR%\win64\curl"
cmake -G "%GENERATOR%" -S "%ROOT%" -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE% -DCMAKE_PREFIX_PATH="%PREFIX%" -DCMAKE_C_FLAGS="/utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN" -DCMAKE_CXX_FLAGS="/utf-8 /DNOMINMAX /DWIN32_LEAN_AND_MEAN" -DCMAKE_EXE_LINKER_FLAGS="/MANIFEST:NO" -DTILES=ON -DSOUND=ON -DCURSES=OFF -DTESTS=OFF -DRELEASE=ON -DDYNAMIC_LINKING=ON -DLOCALIZE=%LOCALIZE% -DLANGUAGES="%LANGUAGES%" %MSGFMT_ARG%
if errorlevel 1 (
    echo   [错误] CMake 配置失败
    goto :fail
)

echo.
echo [6/6] 编译（并行 %JOBS%）
cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel %JOBS%
if errorlevel 1 (
    echo   [错误] 编译失败
    goto :fail
)

if "%MAKE_DIST%"=="1" (
    call :make_dist
    if errorlevel 1 goto :fail
)

echo.
echo ============================================================
echo  构建成功
echo ============================================================
if "%MAKE_DIST%"=="1" (
    echo  运行目录: %DIST_DIR%
    echo  直接运行: %DIST_DIR%\cataclysm-tiles.exe
)
echo  构建目录: %BUILD_DIR%
echo.
if "%PAUSE_AT_END%"=="1" pause
exit /b 0

REM ====================== 子过程 ======================

:download
if exist "%~2" (
    echo   已存在，跳过下载: %~2
    exit /b 0
)
echo   下载: %~1
curl.exe -L -f --retry 2 --connect-timeout 20 -o "%~2" "%~1"
if not errorlevel 1 if exist "%~2" exit /b 0
echo   curl 下载失败，改用 PowerShell
powershell -NoProfile -Command "[Net.ServicePointManager]::SecurityProtocol='Tls12'; (New-Object Net.WebClient).DownloadFile('%~1','%~2')"
if not errorlevel 1 if exist "%~2" exit /b 0
echo   [错误] 下载失败: %~1
exit /b 1

:unzip
echo   解压: %~1
powershell -NoProfile -Command "Add-Type -AssemblyName System.IO.Compression.FileSystem; [IO.Compression.ZipFile]::ExtractToDirectory('%~1','%~2')"
if errorlevel 1 exit /b 1
exit /b 0

:prep_sdl
set "_T=%DEPS_DIR%\win64\%~1"
if exist "%_T%\.ready" (
    echo   %~1: 已就绪
    exit /b 0
)
set "_SRC=%DEPS_DIR%\src\%~2"
if not exist "%_SRC%" (
    if not exist "%DEPS_DIR%\dl" mkdir "%DEPS_DIR%\dl"
    if not exist "%DEPS_DIR%\src" mkdir "%DEPS_DIR%\src"
    call :download "%~3" "%DEPS_DIR%\dl\%~2.zip"
    if errorlevel 1 exit /b 1
    call :unzip "%DEPS_DIR%\dl\%~2.zip" "%DEPS_DIR%\src"
    if errorlevel 1 exit /b 1
)
if not exist "%_SRC%" (
    echo   [错误] 解压后未找到目录: %_SRC%
    exit /b 1
)
echo   %~1: 整理头文件与库
if not exist "%_T%\include" mkdir "%_T%\include"
if not exist "%_T%\lib" mkdir "%_T%\lib"
if not exist "%_T%\bin" mkdir "%_T%\bin"
xcopy /e /y /q "%_SRC%\include\*" "%_T%\include\" >nul
copy /y "%_SRC%\lib\x64\*.lib" "%_T%\lib\" >nul
copy /y "%_SRC%\lib\x64\*.dll" "%_T%\bin\" >nul
if exist "%_SRC%\lib\x64\optional" copy /y "%_SRC%\lib\x64\optional\*.dll" "%_T%\bin\" >nul
echo ready>"%_T%\.ready"
exit /b 0

:prep_zlib
set "_T=%DEPS_DIR%\win64\zlib"
if exist "%_T%\.ready" (
    echo   zlib: 已就绪
    exit /b 0
)
set "_SRC=%DEPS_DIR%\src\zlib-%ZLIB_VER%"
if not exist "%_SRC%" (
    if not exist "%DEPS_DIR%\dl" mkdir "%DEPS_DIR%\dl"
    if not exist "%DEPS_DIR%\src" mkdir "%DEPS_DIR%\src"
    call :download "https://github.com/madler/zlib/archive/refs/tags/v%ZLIB_VER%.zip" "%DEPS_DIR%\dl\zlib.zip"
    if errorlevel 1 exit /b 1
    call :unzip "%DEPS_DIR%\dl\zlib.zip" "%DEPS_DIR%\src"
    if errorlevel 1 exit /b 1
)
echo   zlib: 编译（MSVC）
cmake -G "%GENERATOR%" -S "%_SRC%" -B "%DEPS_DIR%\build\zlib" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="%_T%" -DZLIB_BUILD_EXAMPLES=OFF
if errorlevel 1 exit /b 1
cmake --build "%DEPS_DIR%\build\zlib" --config Release
if errorlevel 1 exit /b 1
cmake --install "%DEPS_DIR%\build\zlib" --config Release
if errorlevel 1 exit /b 1
echo ready>"%_T%\.ready"
exit /b 0

:prep_curl
set "_T=%DEPS_DIR%\win64\curl"
if exist "%_T%\.ready" (
    echo   curl: 已就绪
    exit /b 0
)
set "_SRC=%DEPS_DIR%\src\curl-curl-%CURL_VER%"
if not exist "%_SRC%" (
    if not exist "%DEPS_DIR%\dl" mkdir "%DEPS_DIR%\dl"
    if not exist "%DEPS_DIR%\src" mkdir "%DEPS_DIR%\src"
    call :download "https://github.com/curl/curl/archive/refs/tags/curl-%CURL_VER%.zip" "%DEPS_DIR%\dl\curl.zip"
    if errorlevel 1 exit /b 1
    call :unzip "%DEPS_DIR%\dl\curl.zip" "%DEPS_DIR%\src"
    if errorlevel 1 exit /b 1
)
echo   curl: 编译（MSVC，静态库 + Schannel）
cmake -G "%GENERATOR%" -S "%_SRC%" -B "%DEPS_DIR%\build\curl" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="%_T%" -DBUILD_SHARED_LIBS=OFF -DBUILD_CURL_EXE=OFF -DCURL_USE_SCHANNEL=ON -DCURL_ZLIB=OFF -DCURL_DISABLE_LDAP=ON -DCURL_USE_LIBSSH2=OFF -DCURL_USE_LIBPSL=OFF
if errorlevel 1 exit /b 1
cmake --build "%DEPS_DIR%\build\curl" --config Release
if errorlevel 1 exit /b 1
cmake --install "%DEPS_DIR%\build\curl" --config Release
if errorlevel 1 exit /b 1
echo ready>"%_T%\.ready"
exit /b 0

:prep_gettext
set "_T=%DEPS_DIR%\win64\gettext"
if exist "%_T%\bin\msgfmt.exe" (
    echo   gettext: 已就绪
    exit /b 0
)
if not exist "%DEPS_DIR%\dl" mkdir "%DEPS_DIR%\dl"
if not exist "%DEPS_DIR%\tools" mkdir "%DEPS_DIR%\tools"
set "ZSTD_EXE="
if exist "%DEPS_DIR%\tools\zstd.exe" set "ZSTD_EXE=%DEPS_DIR%\tools\zstd.exe"
if not defined ZSTD_EXE (
    echo   gettext: 下载 zstd（解压工具）
    call :download "https://github.com/facebook/zstd/releases/download/v%ZSTD_VER%/zstd-v%ZSTD_VER%-win64.zip" "%DEPS_DIR%\dl\zstd.zip"
    if errorlevel 1 exit /b 1
    call :unzip "%DEPS_DIR%\dl\zstd.zip" "%DEPS_DIR%\tools\zstd"
    if errorlevel 1 exit /b 1
    for /r "%DEPS_DIR%\tools" %%f in (zstd*.exe) do if not defined ZSTD_EXE if exist "%%f" set "ZSTD_EXE=%%f"
)
if not defined ZSTD_EXE (
    echo   [错误] 未找到 zstd.exe
    exit /b 1
)
for %%f in ("%ZSTD_EXE%") do set "ZSTD_DIR=%%~dpf"
set "PATH=%ZSTD_DIR%;%PATH%"
echo   gettext: 下载 MSYS2 预编译包
call :download "https://repo.msys2.org/mingw/mingw64/%GETTEXT_PKG%-any.pkg.tar.zst" "%DEPS_DIR%\dl\gettext.pkg.tar.zst"
if errorlevel 1 exit /b 1
if not exist "%DEPS_DIR%\build\gettext" mkdir "%DEPS_DIR%\build\gettext"
"%SystemRoot%\System32\tar.exe" -xf "%DEPS_DIR%\dl\gettext.pkg.tar.zst" -C "%DEPS_DIR%\build\gettext"
if errorlevel 1 exit /b 1
if not exist "%_T%\bin" mkdir "%_T%\bin"
xcopy /e /y /q "%DEPS_DIR%\build\gettext\mingw64\bin\*" "%_T%\bin\" >nul
"%_T%\bin\msgfmt.exe" --version >nul 2>&1
if errorlevel 1 (
    echo   [错误] msgfmt.exe 无法运行
    exit /b 1
)
echo   gettext: 完成
exit /b 0

:make_dist
echo.
echo 生成运行目录: %DIST_DIR%
if exist "%DIST_DIR%\data" rmdir /q "%DIST_DIR%\data" 2>nul
if exist "%DIST_DIR%\gfx" rmdir /q "%DIST_DIR%\gfx" 2>nul
if exist "%DIST_DIR%\lang" rmdir /q "%DIST_DIR%\lang" 2>nul
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%"
set "EXE="
for %%p in ("%BUILD_DIR%\src\cataclysm-tiles.exe" "%BUILD_DIR%\src\%BUILD_TYPE%\cataclysm-tiles.exe" "%BUILD_DIR%\cataclysm-tiles.exe") do (
    if not defined EXE if exist %%~p set "EXE=%%~p"
)
if not defined EXE (
    for /r "%BUILD_DIR%" %%f in (cataclysm-tiles*.exe) do if not defined EXE if exist "%%f" set "EXE=%%f"
)
if not defined EXE (
    echo   [错误] 未找到 cataclysm-tiles.exe
    exit /b 1
)
copy /y "%EXE%" "%DIST_DIR%\cataclysm-tiles.exe" >nul
if errorlevel 1 (
    echo   [错误] 复制 exe 失败
    exit /b 1
)
for %%d in (SDL2 SDL2_image SDL2_mixer SDL2_ttf zlib) do (
    if exist "%DEPS_DIR%\win64\%%d\bin" copy /y "%DEPS_DIR%\win64\%%d\bin\*.dll" "%DIST_DIR%\" >nul
)
if "%COPY_DATA%"=="1" (
    echo   复制 data / gfx / lang（约 110 MB）
    xcopy /e /i /q /y "%ROOT%\data" "%DIST_DIR%\data" >nul
    xcopy /e /i /q /y "%ROOT%\gfx" "%DIST_DIR%\gfx" >nul
    xcopy /e /i /q /y "%ROOT%\lang" "%DIST_DIR%\lang" >nul
) else (
    echo   建立目录链接 data / gfx / lang（不占额外空间）
    mklink /J "%DIST_DIR%\data" "%ROOT%\data" >nul
    mklink /J "%DIST_DIR%\gfx" "%ROOT%\gfx" >nul
    mklink /J "%DIST_DIR%\lang" "%ROOT%\lang" >nul
)
echo   完成: %DIST_DIR%\cataclysm-tiles.exe
exit /b 0

:fail
echo.
echo ============================================================
echo  构建失败
echo ============================================================
echo.
if "%PAUSE_AT_END%"=="1" pause
exit /b 1
