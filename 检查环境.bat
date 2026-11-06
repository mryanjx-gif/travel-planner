@echo off
chcp 65001 >nul
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

rem ===== 可按本机实际情况修改的路径（也可以在系统环境变量里设同名变量） =====
if not defined TP_QT set "TP_QT=D:\Qt\6.12.0\msvc2022_64"
if not defined TP_MLN_VK set "TP_MLN_VK=%~dp0third_party\maplibre-native-qt\install-vulkan"
if not defined TP_MLN_GL set "TP_MLN_GL=%~dp0third_party\maplibre-native-qt\install-opengl"
rem ==========================================================================

set "LOG=%~dp0检查环境-结果.txt"
set /a OK=0, BAD=0, WARN=0
> "%LOG%" echo 旅行计划 原生版 · 开发环境检查  %date% %time%
>>"%LOG%" echo.

echo ==============================================
echo   旅行计划 原生版 · 开发环境检查
echo ==============================================
echo.

rem ---------- 1. 系统 ----------
for /f "tokens=4-6 delims=[]. " %%a in ('ver') do set "WINVER=%%a.%%b.%%c"
if /i "%PROCESSOR_ARCHITECTURE%"=="AMD64" (
  call :ok "Windows 64 位（版本 !WINVER!）"
) else (
  call :bad "需要 64 位 Windows，当前是 %PROCESSOR_ARCHITECTURE%"
)

rem ---------- 2. Visual Studio 2022 Build Tools ----------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VSPATH="
if exist "%VSWHERE%" (
  for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
)
if defined VSPATH (
  call :ok "VS 2022 C++ 编译工具：!VSPATH!"
) else (
  call :bad "没找到 VS 2022 的 C++ 编译工具（MSVC v143 x64/x86）"
  goto :qt
)

set "HASCMAKECOMP="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.VC.CMake.Project -property installationPath`) do set "HASCMAKECOMP=1"
if defined HASCMAKECOMP (call :ok "组件：用于 Windows 的 C++ CMake 工具") else (call :bad "缺组件：用于 Windows 的 C++ CMake 工具（自带 CMake 和 Ninja）")

set "HASSDK="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.Windows11SDK.26100 -property installationPath`) do set "HASSDK=1"
if not defined HASSDK for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.Windows11SDK.22621 -property installationPath`) do set "HASSDK=1"
if defined HASSDK (call :ok "组件：Windows 11 SDK") else (call :warn "没检测到 Windows 11 SDK（10.0.22621 或 26100）；如果下面 cl 正常可以忽略")

rem 进入 x64 编译环境，检查 cl / cmake / ninja
call "!VSPATH!\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
where cl >nul 2>&1 && (
  rem 中文系统输出"……编译器 19.44.35221 版"，英文是"Version 19.44.35221"：按版本号的数字格式找，不认语言
  set "CLV="
  for /f "tokens=*" %%v in ('cl 2^>^&1 ^| findstr /r "[0-9][0-9]\.[0-9][0-9]\.[0-9]"') do if not defined CLV set "CLV=%%v"
  call :ok "cl：!CLV!"
) || call :bad "vcvars64.bat 之后仍找不到 cl.exe"

where cmake >nul 2>&1 && (
  for /f "tokens=3" %%v in ('cmake --version ^| findstr /b /c:"cmake version"') do set "CMV=%%v"
  call :ok "CMake !CMV!（需要 3.24 或更新）"
) || call :bad "找不到 cmake"

where ninja >nul 2>&1 && (
  for /f "tokens=*" %%v in ('ninja --version') do set "NJV=%%v"
  call :ok "Ninja !NJV!"
) || call :bad "找不到 ninja"

:qt
rem ---------- 3. Qt 6.12（MSVC 2022 64 位） ----------
if exist "%TP_QT%\bin\qtpaths.exe" (
  for /f "tokens=*" %%v in ('"%TP_QT%\bin\qtpaths.exe" --qt-query QT_VERSION') do set "QTV=%%v"
  call :ok "Qt !QTV!：%TP_QT%"
  if not "!QTV:~0,5!"=="6.12." call :bad "Qt 版本应为 6.12.x，当前是 !QTV!"
) else (
  call :bad "没找到 Qt：%TP_QT%\bin\qtpaths.exe（如果装在别处，改本文件顶部的 TP_QT）"
  goto :mln
)
call :need "%TP_QT%\bin\Qt6Quick.dll"            "Qt Quick"
call :need "%TP_QT%\bin\Qt6QuickEffects.dll"     "Qt Quick Effects"
call :need "%TP_QT%\bin\Qt6Positioning.dll"      "Qt Positioning（附加模块）"
call :need "%TP_QT%\bin\Qt6Svg.dll"              "Qt SVG"
call :need "%TP_QT%\bin\qsb.exe"                 "Qt Shader Tools（只在编译时用）"
call :need "%TP_QT%\bin\lrelease.exe"            "Qt Linguist 工具（lrelease，生成翻译文件）"
call :need "%TP_QT%\bin\windeployqt.exe"         "windeployqt"
call :need "%TP_QT%\lib\cmake\Qt6\Qt6Config.cmake" "Qt CMake 文件"
if exist "%TP_QT%\bin\Qt6Quick3D.dll" call :warn "装了 Qt Quick 3D（GPL 模块，本项目不用；不影响编译，但打包时不会带上）"

:mln
rem ---------- 4. maplibre-native-qt（两套：Vulkan 版、OpenGL 版） ----------
if exist "%~dp0third_party\maplibre-native-qt\src\.解压完成.txt" (
  call :ok "maplibre-native-qt 源码已解压"
) else (
  call :warn "maplibre-native-qt 源码还没解压：运行 准备maplibre源码.bat"
)
if exist "%TP_MLN_VK%\qml\MapLibre\qmldir" (call :ok "maplibre Vulkan 版：%TP_MLN_VK%") else (call :warn "还没有 maplibre Vulkan 版：运行 编译maplibre.bat")
if exist "%TP_MLN_GL%\qml\MapLibre\qmldir" (call :ok "maplibre OpenGL 版：%TP_MLN_GL%") else (call :warn "还没有 maplibre OpenGL 版：运行 编译maplibre.bat")

rem ---------- 5. 显卡与磁盘 ----------
for /f "tokens=2 delims==" %%g in ('wmic path win32_VideoController get name /value 2^>nul ^| findstr "="') do (
  set "GPU=%%g"
  call :info "显卡：!GPU!"
)
for /f "tokens=3" %%f in ('dir /-c "%~d0\" 2^>nul ^| findstr /c:"bytes free" /c:"可用字节"') do set "FREE=%%f"
if defined FREE (
  set "FREEGB=!FREE:~0,-9!"
  if not defined FREEGB set "FREEGB=0"
  if !FREEGB! LSS 10 (call :warn "%~d0 盘剩余约 !FREEGB! GB，编译两套 maplibre 需要约 12 GB") else (call :ok "%~d0 盘剩余约 !FREEGB! GB")
)

rem ---------- 6. 打包工具（阶段 ⑧ 才需要） ----------
set "NSIS="
if exist "%ProgramFiles(x86)%\NSIS\makensis.exe" set "NSIS=%ProgramFiles(x86)%\NSIS\makensis.exe"
if exist "%ProgramFiles%\NSIS\makensis.exe" set "NSIS=%ProgramFiles%\NSIS\makensis.exe"
if defined NSIS (call :ok "NSIS：!NSIS!") else (call :info "NSIS 还没装（阶段 ⑧ 打包时才需要）")

echo.
echo ----------------------------------------------
echo   通过 %OK% 项，缺少 %BAD% 项，提醒 %WARN% 项
echo ----------------------------------------------
>>"%LOG%" echo.
>>"%LOG%" echo 通过 %OK% 项，缺少 %BAD% 项，提醒 %WARN% 项
echo 结果也存在：%LOG%
if %BAD% GTR 0 (
  echo 有缺少的项：按 claude\Windows开发环境安装.md 补装后再运行一次；
  echo 解决不了就把 检查环境-结果.txt 发给 Claude。
) else (
  echo 环境齐了。
)
echo.
pause
exit /b %BAD%

rem ================= 子程序 =================
:ok
echo   [√] %~1
>>"%LOG%" echo [OK]   %~1
set /a OK+=1
exit /b 0
:bad
echo   [×] %~1
>>"%LOG%" echo [缺少] %~1
set /a BAD+=1
exit /b 0
:warn
echo   [!] %~1
>>"%LOG%" echo [提醒] %~1
set /a WARN+=1
exit /b 0
:info
echo   [i] %~1
>>"%LOG%" echo [信息] %~1
exit /b 0
:need
if exist "%~1" (call :ok "%~2") else (call :bad "缺 %~2：%~1")
exit /b 0
