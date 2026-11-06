@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
if not defined TP_QT set "TP_QT=D:\Qt\6.12.0\msvc2022_64"
echo 一键验证：编译、单元测试、自检、截图。期间不要动开发版程序。
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\win\verify.ps1" -Root "%~dp0." -Qt "%TP_QT%"
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%
