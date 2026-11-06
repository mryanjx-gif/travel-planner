@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"
echo ==============================================
echo   准备 maplibre-native-qt 源码（提交 399311e）
echo ==============================================
echo 会做：校验 20 个分块 → 合并成整份 zip → 解压到 third_party\maplibre-native-qt\src\
echo       （跳过 Windows 路径超长的测试文件）→ 分块移进【删除】
echo       另外：testdata\data.js 不存在时，从 D:\旅行计划数据\data.js 只读复制一份
echo 大约需要 5-15 分钟，中间不用操作。
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\win\prepare-maplibre.ps1" -Root "%~dp0."
set "RC=%ERRORLEVEL%"
echo.
if "%RC%"=="0" (
  echo 成功。结果也写在 准备maplibre源码-结果.txt。
) else (
  echo 出错了。请把 准备maplibre源码-结果.txt 发给 Claude（或截图）。
)
pause
exit /b %RC%
