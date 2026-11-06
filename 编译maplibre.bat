@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
echo ==============================================
echo   编译 maplibre-native-qt（Vulkan 版 + OpenGL 版）
echo ==============================================
echo 一次编译只能带一个图形后端，所以编两套，产物分别放：
echo   third_party\maplibre-native-qt\install-vulkan\
echo   third_party\maplibre-native-qt\install-opengl\
echo 临时文件在 D:\TravelPlannerNative-dev\（约 6-10 GB，编完可删）。
echo 窗口里会显示：当前第几套、进度、已用时间、预计剩余时间。中间不用操作。
echo 只想重编一套：编译maplibre.bat vulkan  或  编译maplibre.bat opengl
echo.
if not defined TP_QT set "TP_QT=D:\Qt\6.12.0\msvc2022_64"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\win\build-maplibre.ps1" -Root "%~dp0." -Qt "%TP_QT%" -Only "%~1"
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (
  echo 成功。结果写在 编译maplibre-结果.txt，详细日志在 编译maplibre-日志\。
) else (
  echo 出错了。请把 编译maplibre-结果.txt 和 编译maplibre-日志\ 里最新的 .log 发给 Claude。
)
pause
exit /b %RC%
