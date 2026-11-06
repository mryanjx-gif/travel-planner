@echo off
chcp 65001 >nul
cd /d "%~dp0"
echo 内存分步测试：约 17 分钟，中间不用操作，不要最小化或遮挡程序窗口。先要运行过 编译.bat。
pause
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\win\memtest.ps1" -Root "%~dp0." -Mode steps %*
set "RC=%ERRORLEVEL%"
pause
exit /b %RC%
