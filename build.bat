@echo off
chcp 936 >nul 2>&1
setlocal EnableDelayedExpansion
title CDDA-Breeze APK Build

REM ========================= ������ =========================
set "SDK_DIR=D:\soft\android_sdk"
set "NDK_VER=25.2.9519653"
set "JDK_DIR="
set "JMAJOR="
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
if /i "%~1"=="--jdk"     ( set "JDK_DIR=%~2"           & shift & shift & goto :parse )
if /i "%~1"=="--jobs"    ( set "JOBS=%~2"              & shift & shift & goto :parse )
if /i "%~1"=="--version" ( set "VERSION_OVERRIDE=%~2"  & shift & shift & goto :parse )
if /i "%~1"=="--clean"   ( set "DO_CLEAN=1"            & shift & goto :parse )
if /i "%~1"=="--abi32"   ( set "ABI_ARM32=true"        & shift & goto :parse )
if /i "%~1"=="--no-pause" ( set "PAUSE_AT_END=0"       & shift & goto :parse )
if /i "%~1"=="--help"    ( goto :usage )
if /i "%~1"=="-h"        ( goto :usage )
echo [����] δ֪����: %~1
goto :usage

:usage
echo.
echo �÷�: build.bat [release ^| debug] [ѡ��]
echo.
echo   release           ���� release ����Ĭ�ϣ��òֿ��Դ��� a.keystore ǩ����
echo   debug             ���� debug ��
echo   --sdk  ^<·��^>    ָ�� Android SDK Ŀ¼��Ĭ�� D:\soft\android_sdk
echo   --ndk  ^<�汾^>    ָ�� NDK �汾��Ĭ�� 25.2.9519653
echo   --jobs ^<N^>       ԭ�����벢����������Ĭ�� 16
echo   --version ^<��^>   ���ǰ汾�ţ����� CDDA-Breeze-12.7
echo   --abi32           ͬʱ���� armeabi-v7a�������һ��ԭ�����룩
echo   --clean           ����ǰ��ִ�� gradlew clean
echo   --no-pause        ��������ͣ
echo.
echo ʾ��:
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
echo   CDDA-Breeze Android APK ����
echo ============================================================
echo   �ֿ��Ŀ¼ : %ROOT%
echo   SDK        : %SDK_DIR%
echo   ��������   : %BUILD_TYPE%
echo   ���������� : %JOBS%
echo   ABI        : arm64-v8a=%ABI_ARM64%  armeabi-v7a=%ABI_ARM32%
echo.

REM ---------- ��鹤�̽ṹ ----------
if not exist "%ANDROID_DIR%\gradlew.bat" (
    echo [����] �Ҳ��� %ANDROID_DIR%\gradlew.bat
    echo        ��� build.bat ���ڲֿ��Ŀ¼�����С�
    exit /b 1
)

REM ---------- ��� Java ----------
if defined JAVA_HOME (
    if not exist "%JAVA_HOME%\bin\java.exe" (
        echo [����] JAVA_HOME ָ���λ��û�� java.exe: %JAVA_HOME%
        echo        ��������Ҫ JDK 11 �� 17���Ƽ���װ JDK 17 ������ JAVA_HOME��
        exit /b 1
    )
    echo   Java       : %JAVA_HOME%
) else (
    where java >nul 2>&1
    if errorlevel 1 (
        echo [����] δ�ҵ� java����δ���� JAVA_HOME��
        echo        ��������Ҫ JDK 11 �� 17���Ƽ���װ JDK 17 ������ JAVA_HOME��
        exit /b 1
    )
    echo   Java       : ʹ�� PATH �е� java
)

REM ---------- ��� SDK ----------
REM ---------- Resolve JDK: R8/D8 3.3.70 bundled with AGP 7.3.0 NPEs on JDK 21+ ----------
call :resolve_jdk
if errorlevel 1 exit /b 1
set "JAVA_HOME=%JDK_DIR%"
echo   Java(used) : %JDK_DIR%  ^(JDK %JMAJOR%^)

if not exist "%SDK_DIR%\platform-tools" (
    echo [����] SDK ·����Ч: %SDK_DIR%
    echo        ���޸Ľű������� SDK_DIR������ --sdk ָ����ȷ·����
    exit /b 1
)

REM ---------- ��� NDK ----------
if not exist "%SDK_DIR%\ndk\%NDK_VER%\ndk-build.cmd" (
    echo [����] δ�ҵ� NDK %NDK_VER%�����Դ��Ѱ�װ�汾���Զ�ѡ��...
    set "NDK_VER="
    for /f "delims=" %%d in ('dir /b /o-n "%SDK_DIR%\ndk" 2^>nul') do (
        if not defined NDK_VER (
            if exist "%SDK_DIR%\ndk\%%d\ndk-build.cmd" set "NDK_VER=%%d"
        )
    )
    if not defined NDK_VER (
        echo [����] SDK ��û�п��õ� NDK��
        echo        �밲װ��Ŀ�̶��汾: sdkmanager "ndk;25.2.9519653"
        echo        ������ android-ndk-r25c-windows.zip ��ѹ�� %SDK_DIR%\ndk\25.2.9519653
        exit /b 1
    )
    echo         �Ѹ��� NDK !NDK_VER!
)
echo   NDK        : %NDK_VER%

REM ---------- ���ɱ������� ----------
set "NEED_KS=0"
if /i "%BUILD_TYPE%"=="Release" (
    if not exist "%ANDROID_DIR%\keystore.properties" set "NEED_KS=1"
)
call :write_local_props
if "%NEED_KS%"=="1" call :write_keystore_props
if "%NEED_KS%"=="1" (
    echo   ǩ������   : ������ keystore.properties��ʹ�òֿ��Դ��� a.keystore
) else (
    if /i "%BUILD_TYPE%"=="Release" echo   ǩ������   : �������� keystore.properties
)

REM ---------- ��ѡ���� ----------
if "%DO_CLEAN%"=="1" (
    echo.
    echo ִ������...
    pushd "%ANDROID_DIR%"
    call gradlew.bat clean --console=plain
    popd
)

REM ---------- ���� ----------
set "GRADLE_OPTS=-Dorg.gradle.internal.http.connectionTimeout=120000 -Dorg.gradle.internal.http.socketTimeout=120000 %GRADLE_OPTS%"
set "GRADLE_ARGS=assemble%BUILD_TYPE% --console=plain -Poverride_ndkVersion=%NDK_VER% -Pabi_arm_32=%ABI_ARM32% -Pabi_arm_64=%ABI_ARM64%"
if not "%VERSION_OVERRIDE%"=="" set "GRADLE_ARGS=%GRADLE_ARGS% -Poverride_version=%VERSION_OVERRIDE%"

echo.
echo ��ʼ�������״���Ҫ���� 385 �� C++ �ļ��������ĵȴ�...
echo ����: gradlew.bat %GRADLE_ARGS%
echo.
pushd "%ANDROID_DIR%"
call gradlew.bat %GRADLE_ARGS%
set "RC=%ERRORLEVEL%"
popd

if not "%RC%"=="0" (
    echo.
    echo [ʧ��] Gradle �˳��� %RC%
    echo        �Ų�ʱ�ɼ� --stacktrace �� --info �鿴��ϸ��־��
    if "%PAUSE_AT_END%"=="1" pause
    exit /b %RC%
)

REM ---------- �г����� ----------
if /i "%BUILD_TYPE%"=="Release" ( set "TYPE_DIR=release" ) else ( set "TYPE_DIR=debug" )
set "OUT_DIR=%ANDROID_DIR%\app\build\outputs\apk\stable\%TYPE_DIR%"
set "APK_COUNT=0"
set "APK="

echo.
echo ============================================================
echo   �����ɹ�
echo ============================================================
for /f "delims=" %%f in ('dir /b /s "%OUT_DIR%\*.apk" 2^>nul') do (
    set /a APK_COUNT+=1
    set "APK=%%f"
    echo.
    echo   [!APK_COUNT!] %%~nxf
    echo       ��С : %%~zf �ֽ�
    echo       ·�� : %%f
)
echo.
echo   ���Ŀ¼ : %OUT_DIR%
echo ============================================================

if "%APK_COUNT%"=="0" (
    echo.
    echo [ʧ��] �������سɹ��������Ŀ¼��û���ҵ� APK��
    if "%PAUSE_AT_END%"=="1" pause
    exit /b 1
)

echo.
echo   ��װ���ֻ�: adb install -r "%APK%"
echo.
if "%PAUSE_AT_END%"=="1" pause
exit /b 0

REM ========================= �ӹ��� =========================
REM ========================= Subroutines =========================

REM Prints the JDK major version of %1 into JMAJOR
:jdk_major
set "JMAJOR="
set "JVER="
REM NOTE: do not pipe a quoted exe path inside for /f - cmd /c swallows the first
REM pair of quotes and the command fails with "The filename ... is incorrect".
"%~1\bin\java.exe" -version > "%TEMP%\cdda_jdkver.txt" 2>&1
for /f "tokens=3" %%v in ('findstr /i "version" "%TEMP%\cdda_jdkver.txt"') do (
    if not defined JVER set "JVER=%%~v"
)
if not defined JVER exit /b 0
set "JM1="
set "JM2="
for /f "tokens=1,2 delims=." %%a in ("%JVER%") do ( set "JM1=%%a" & set "JM2=%%b" )
if "%JM1%"=="1" ( set "JMAJOR=%JM2%" ) else ( set "JMAJOR=%JM1%" )
exit /b 0

REM Accepts %1 as a JDK home if it provides a usable java.exe in the 11..18 range
:try_jdk
if not "!JDK_DIR!"=="" exit /b 0
if "%~1"=="" exit /b 0
if not exist "%~1\bin\java.exe" exit /b 0
call :jdk_major "%~1"
if not defined JMAJOR exit /b 0
if !JMAJOR! GEQ 21 exit /b 0
if !JMAJOR! LSS 11 exit /b 0
set "JDK_DIR=%~1"
echo   JDK found  : %~1  ^(JDK %JMAJOR%^)
exit /b 0

REM Picks a JDK that AGP 7.3.0 / R8 3.3.70 can actually run on
:resolve_jdk
if not "!JDK_DIR!"=="" goto :jdk_validate
if defined JAVA_HOME if exist "%JAVA_HOME%\bin\java.exe" set "JDK_DIR=%JAVA_HOME%"
:jdk_validate
if not "!JDK_DIR!"=="" (
    if not exist "!JDK_DIR!\bin\java.exe" set "JDK_DIR="
)
if not "!JDK_DIR!"=="" (
    call :jdk_major "!JDK_DIR!"
    if defined JMAJOR if !JMAJOR! GEQ 21 (
        echo   JDK reject : !JDK_DIR! ^(JDK !JMAJOR!^) - D8 dexing crashes on JDK 21+
        set "JDK_DIR="
    )
)
if not "!JDK_DIR!"=="" exit /b 0
echo   Scanning for a compatible JDK ^(11 - 18^) ...
for /d %%d in ("D:\soft\Java\*") do call :try_jdk "%%d"
if not "!JDK_DIR!"=="" exit /b 0
for /d %%d in ("C:\Program Files\Java\*") do call :try_jdk "%%d"
if not "!JDK_DIR!"=="" exit /b 0
echo [ERROR] No compatible JDK found.
echo         AGP 7.3.0 needs JDK 11-18. JDK 21+ makes D8 throw NullPointerException
echo         while dexing. Install JDK 17, or pass --jdk "<path>".
exit /b 1

:write_local_props
set "SDK_PROP=%SDK_DIR:\=/%"
>  "%ANDROID_DIR%\local.properties" echo # �� build.bat �Զ����ɣ�git �Ѻ��Դ��ļ������ֹ��޸Ļ�ɾ������������
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
