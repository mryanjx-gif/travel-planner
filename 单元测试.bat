@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
if not defined TP_QT set "TP_QT=D:\Qt\6.12.0\msvc2022_64"
echo 编译并运行数据层单元测试（只读你的数据文件夹，不写入）。
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\win\run-tests.ps1" -Root "%~dp0." -Qt "%TP_QT%"
set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" echo 有测试没通过或出错了。请把 单元测试-结果.txt 发给 Claude。
pause
exit /b %RC%
