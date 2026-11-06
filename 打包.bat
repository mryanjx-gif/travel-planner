@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
echo ==============================================
echo   打包草稿：出一个 zip，量安装包大小，和 Electron v6.3.0 比
echo ==============================================
echo 会做：编 Release 版 → 收集运行库（两套地图库都带）→ dist\TravelPlannerNative-版本号\ → 打 zip
echo 先要运行过 编译maplibre.bat。约 3-5 分钟。正式安装程序（NSIS）在阶段 ⑧ 做。
echo.
if not defined TP_QT set "TP_QT=D:\Qt\6.12.0\msvc2022_64"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\win\package.ps1" -Root "%~dp0." -Qt "%TP_QT%"
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (echo 成功。结果写在 打包-结果.txt。) else (echo 出错了。请把 打包-结果.txt 发给 Claude。)
pause
exit /b %RC%
