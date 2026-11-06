@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
if not defined TP_QT set "TP_QT=D:\Qt\6.12.0\msvc2022_64"
echo 编译程序（优化 + 调试信息），完成后打开程序所在文件夹。
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\win\build-app.ps1" -Root "%~dp0." -Qt "%TP_QT%"
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="1" echo 出错了。请把 编译-结果.txt 发给 Claude。
pause
exit /b %RC%
