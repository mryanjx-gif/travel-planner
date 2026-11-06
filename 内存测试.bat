@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
echo ==============================================
echo   内存测试：原生版 Vulkan 1 小时 + OpenGL 1 小时
echo ==============================================
echo 先要运行过 编译.bat。全程约 2 小时（加 Electron 参照再多 10 分钟），中间不用操作：
echo   程序会自己循环换底图、平移缩放、切地球、开关玻璃面板，跑完自动退出、自动换下一项。
echo   测试期间电脑不会睡眠；请不要最小化或遮挡程序窗口。
echo.
echo 想要 Electron 版作参照：现在打开 Electron 版「旅行计划」，停在地图页，再按任意键。
echo 不需要参照：确认 Electron 版没开着，直接按任意键。
pause >nul
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\win\memtest.ps1" -Root "%~dp0." %*
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (echo 完成。结果在 内存测试-结果\ 文件夹。) else (echo 出错了。请把 内存测试-结果\ 里最新的文件发给 Claude。)
pause
exit /b %RC%
