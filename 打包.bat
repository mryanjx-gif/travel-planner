@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
echo ==============================================
echo   打包：NSIS 安装程序 + 便携版 zip，量大小
echo ==============================================
echo 会做：编 Release 版 → 收集运行库（两套地图库都带）→ dist\ 里的安装包和 zip
echo 先要运行过 编译maplibre.bat、装好 NSIS 3。约 3-5 分钟。
echo.
if not defined TP_QT set "TP_QT=D:\Qt\6.12.0\msvc2022_64"
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\win\package.ps1" -Root "%~dp0." -Qt "%TP_QT%"
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (echo 成功。结果写在 打包-结果.txt。) else (echo 出错了。请把 打包-结果.txt 发给 Claude。)
pause
exit /b %RC%
