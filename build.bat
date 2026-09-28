@echo off
chcp 936 >nul 2>&1
setlocal EnableDelayedExpansion
title CDDA-Breeze APK Build

REM ========================= 配置区 =========================
set "SDK_DIR=D:\soft\android_sdk"
set "NDK_VER=25.2.9519653"
set "JOBS=16"
set "ABI_ARM32=false"
set "ABI_ARM64=true"
set "LOCALIZE=false"
set "BUILD_TYPE=Release"
set "VERSION_OVERRIDE="
set "DO_CLEAN=0"
set "PAUSE_AT_END=1"
REM ==========================================================

:parse
if "%~1"=="" goto :afterparse
if /i "%~1"=="release"   ( set "BUILD_TYPE=Release" & shift & goto :parse )
if /i "%~1"=="debug"     ( set "BUILD_TYPE=Debug"   & shift & goto :parse )
if /i "%~1"=="--sdk"     ( set "SDK_DIR=%~2"           & shift & shift & goto :parse )
if /i "%~1"=="--ndk"     ( set "NDK_VER=%~2"           & shift & shift & goto :parse )
if /i "%~1"=="--jobs"    ( set "JOBS=%~2"              & shift & shift & goto :parse )
if /i "%~1"=="--version" ( set "VERSION_OVERRIDE=%~2"  & shift & shift & goto :parse )
if /i "%~1"=="--clean"   ( set "DO_CLEAN=1"            & shift & goto :parse )
if /i "%~1"=="--abi32"   ( set "ABI_ARM32=true"        & shift & goto :parse )
if /i "%~1"=="--no-pause" ( set "PAUSE_AT_END=0"       & shift & goto :parse )
if /i "%~1"=="--help"    ( goto :usage )
if /i "%~1"=="-h"        ( goto :usage )
echo [错误] 未知参数: %~1
goto :usage

:usage
echo.
echo 用法: build.bat [release ^| debug] [选项]
echo.
echo   release           构建 release 包（默认，用仓库自带的 a.keystore 签名）
echo   debug             构建 debug 包
echo   --sdk  ^<路径^>    指定 Android SDK 目录，默认 D:\soft\android_sdk
echo   --ndk  ^<版本^>    指定 NDK 版本，默认 25.2.9519653
echo   --jobs ^<N^>       原生编译并行任务数，默认 16
echo   --version ^<串^>   覆盖版本号，例如 CDDA-Breeze-12.7
echo   --abi32           同时构建 armeabi-v7a（会多跑一轮原生编译）
echo   --clean           构建前先执行 gradlew clean
echo   --no-pause        结束后不暂停
echo.
echo 示例:
echo   build.bat
echo   build.bat debug --jobs 20
echo   build.bat --version CDDA-Breeze-12.7 --clean
echo.
exit /b 2

:afterparse
set "ROOT=%~dp0"
set "ANDROID_DIR=%ROOT%android"

echo.
echo ============================================================
echo   CDDA-Breeze Android APK 构建
echo ============================================================
echo   仓库根目录 : %ROOT%
echo   SDK        : %SDK_DIR%
echo   构建类型   : %BUILD_TYPE%
echo   并行任务数 : %JOBS%
echo   ABI        : arm64-v8a=%ABI_ARM64%  armeabi-v7a=%ABI_ARM32%
echo.

REM ---------- 检查工程结构 ----------
if not exist "%ANDROID_DIR%\gradlew.bat" (
    echo [错误] 找不到 %ANDROID_DIR%\gradlew.bat
    echo        请把 build.bat 放在仓库根目录再运行。
    exit /b 1
)

REM ---------- 检查 Java ----------
if defined JAVA_HOME (
    if not exist "%JAVA_HOME%\bin\java.exe" (
        echo [错误] JAVA_HOME 指向的位置没有 java.exe: %JAVA_HOME%
        echo        本工程需要 JDK 11 或 17，推荐安装 JDK 17 并设置 JAVA_HOME。
        exit /b 1
    )
    echo   Java       : %JAVA_HOME%
) else (
    where java >nul 2>&1
    if errorlevel 1 (
        echo [错误] 未找到 java，且未设置 JAVA_HOME。
        echo        本工程需要 JDK 11 或 17，推荐安装 JDK 17 并设置 JAVA_HOME。
        exit /b 1
    )
    echo   Java       : 使用 PATH 中的 java
)

REM ---------- 检查 SDK ----------
if not exist "%SDK_DIR%\platform-tools" (
    echo [错误] SDK 路径无效: %SDK_DIR%
    echo        请修改脚本顶部的 SDK_DIR，或用 --sdk 指定正确路径。
    exit /b 1
)

REM ---------- 检查 NDK ----------
if not exist "%SDK_DIR%\ndk\%NDK_VER%\ndk-build.cmd" (
    echo [警告] 未找到 NDK %NDK_VER%，尝试从已安装版本中自动选择...
    set "NDK_VER="
    for /f "delims=" %%d in ('dir /b /o-n "%SDK_DIR%\ndk" 2^>nul') do (
        if not defined NDK_VER (
            if exist "%SDK_DIR%\ndk\%%d\ndk-build.cmd" set "NDK_VER=%%d"
        )
    )
    if not defined NDK_VER (
        echo [错误] SDK 中没有可用的 NDK。
        echo        请安装项目固定版本: sdkmanager "ndk;25.2.9519653"
        echo        或下载 android-ndk-r25c-windows.zip 解压到 %SDK_DIR%\ndk\25.2.9519653
        exit /b 1
    )
    echo         已改用 NDK !NDK_VER!
)
echo   NDK        : %NDK_VER%

REM ---------- 生成本地配置 ----------
set "NEED_KS=0"
if /i "%BUILD_TYPE%"=="Release" (
    if not exist "%ANDROID_DIR%\keystore.properties" set "NEED_KS=1"
)
call :write_local_props
if "%NEED_KS%"=="1" call :write_keystore_props
if "%NEED_KS%"=="1" (
    echo   签名配置   : 已生成 keystore.properties，使用仓库自带的 a.keystore
) else (
    if /i "%BUILD_TYPE%"=="Release" echo   签名配置   : 沿用已有 keystore.properties
)

REM ---------- 可选清理 ----------
if "%DO_CLEAN%"=="1" (
    echo.
    echo 执行清理...
    pushd "%ANDROID_DIR%"
    call gradlew.bat clean --console=plain
    popd
)

REM ---------- 构建 ----------
set "GRADLE_OPTS=-Dorg.gradle.internal.http.connectionTimeout=120000 -Dorg.gradle.internal.http.socketTimeout=120000 %GRADLE_OPTS%"
set "GRADLE_ARGS=assemble%BUILD_TYPE% --console=plain -Poverride_ndkVersion=%NDK_VER% -Pabi_arm_32=%ABI_ARM32% -Pabi_arm_64=%ABI_ARM64%"
if not "%VERSION_OVERRIDE%"=="" set "GRADLE_ARGS=%GRADLE_ARGS% -Poverride_version=%VERSION_OVERRIDE%"

echo.
echo 开始构建，首次需要编译 385 个 C++ 文件，请耐心等待...
echo 命令: gradlew.bat %GRADLE_ARGS%
echo.
pushd "%ANDROID_DIR%"
call gradlew.bat %GRADLE_ARGS%
set "RC=%ERRORLEVEL%"
popd

if not "%RC%"=="0" (
    echo.
    echo [失败] Gradle 退出码 %RC%
    echo        排查时可加 --stacktrace 或 --info 查看详细日志。
    if "%PAUSE_AT_END%"=="1" pause
    exit /b %RC%
)

REM ---------- 列出产物 ----------
if /i "%BUILD_TYPE%"=="Release" ( set "TYPE_DIR=release" ) else ( set "TYPE_DIR=debug" )
set "OUT_DIR=%ANDROID_DIR%\app\build\outputs\apk\stable\%TYPE_DIR%"
set "APK_COUNT=0"
set "APK="

echo.
echo ============================================================
echo   构建成功
echo ============================================================
for /f "delims=" %%f in ('dir /b /s "%OUT_DIR%\*.apk" 2^>nul') do (
    set /a APK_COUNT+=1
    set "APK=%%f"
    echo.
    echo   [!APK_COUNT!] %%~nxf
    echo       大小 : %%~zf 字节
    echo       路径 : %%f
)
echo.
echo   输出目录 : %OUT_DIR%
echo ============================================================

if "%APK_COUNT%"=="0" (
    echo.
    echo [失败] 构建返回成功，但输出目录下没有找到 APK。
    if "%PAUSE_AT_END%"=="1" pause
    exit /b 1
)

echo.
echo   安装到手机: adb install -r "%APK%"
echo.
if "%PAUSE_AT_END%"=="1" pause
exit /b 0

REM ========================= 子过程 =========================
:write_local_props
set "SDK_PROP=%SDK_DIR:\=/%"
>  "%ANDROID_DIR%\local.properties" echo # 由 build.bat 自动生成，git 已忽略此文件；可手工修改或删除后重新生成
>> "%ANDROID_DIR%\local.properties" echo sdk.dir=%SDK_PROP%
>> "%ANDROID_DIR%\local.properties" echo localize=%LOCALIZE%
>> "%ANDROID_DIR%\local.properties" echo j=%JOBS%
exit /b 0

:write_keystore_props
>  "%ANDROID_DIR%\keystore.properties" echo storeFile=../a.keystore
>> "%ANDROID_DIR%\keystore.properties" echo storePassword=123456
>> "%ANDROID_DIR%\keystore.properties" echo keyAlias=a
>> "%ANDROID_DIR%\keystore.properties" echo keyPassword=123456
exit /b 0
