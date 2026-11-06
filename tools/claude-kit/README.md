# claude-kit：Claude 的测试工具（不打包进 app）

这里放 Claude 在云端（Linux）测试原生版用的脚本。安装程序和 `编译.bat` 都不会用到这个文件夹。

## 云端环境（阶段 ⓪ 实测，2026-11-02）

| 项 | 结果 |
|---|---|
| 系统 | Ubuntu 24.04，x86_64，2 核，7 GB 内存 |
| apt 的 Qt | 只有 6.4.2，太旧（没有 `QQuickRhiItem`），不用 |
| Qt 官方下载站 / 镜像 / api.anaconda.org | 被代理拦截（403），不用 |
| conda-forge | 可用：`qt6-main=6.12.0`、`qt6-positioning=6.12.0`；**没有 Qt Location** |
| GitHub | 能 `git clone`（公开仓库）；release 附件和 API 不行 |
| 渲染 | Mesa llvmpipe（软件 OpenGL 4.6），Vulkan 用 lavapipe（未测） |

## 怎么搭（每个新对话的容器是空的，要重新装）

```bash
# 1. micromamba（从 conda-forge 取单文件版本）
curl -sSo mm.tar.bz2 https://conda.anaconda.org/conda-forge/linux-64/micromamba-2.10.0-0.tar.bz2
tar -xjf mm.tar.bz2 bin/micromamba
export MAMBA_ROOT_PREFIX=/opt/mamba
./bin/micromamba create -y -p /opt/qtenv -c conda-forge \
    "qt6-main=6.12.0" "qt6-positioning=6.12.0" cxx-compiler cmake ninja \
    mesalib libglvnd-devel xorg-xorgproto pkg-config zlib
# 2. 虚拟显示（apt 自带 Xvfb）
Xvfb :99 -screen 0 1600x1000x24 &
# 3. 编译
./bin/micromamba run -p /opt/qtenv cmake -S <源码> -B <build> -G Ninja -DCMAKE_PREFIX_PATH=/opt/qtenv
```

## 截图怎么跑

- **不要用** `QT_QPA_PLATFORM=offscreen`：conda-forge 这个 Qt 的 offscreen 插件没有 OpenGL，会退回软件渲染，`QQuickRhiItem` 和地图都画不出来。
- 用 `DISPLAY=:99 QT_QPA_PLATFORM=xcb QSG_RHI_BACKEND=opengl`，窗口画在 Xvfb 里，效果等同无窗口运行。
- app 的 `--screenshot <页面> <输出路径>` 开关在阶段 ⓪ 第 2 个对话加入；截图前先用像素颜色做数值判断。

## maplibre-native-qt

源码从 GitHub 克隆（带子模块约 1.3 GB），2 核编译约 1 小时。conda 的 GCC 15 会触发 maplibre-native 的 `-Werror` 误报，要加 `-DMLN_WITH_WERROR=OFF`；还需要 conda 的 `zlib`（头文件）。编译参数和结果见 `claude/v7.0.0-a记录.md`。

## 7.0.0-b 补充（2026-11-02）

- Vulkan：`apt-get install mesa-vulkan-drivers vulkan-tools`（lavapipe 软件 Vulkan 1.4）；查错用 `apt-get install vulkan-validationlayers` 后加环境变量 `QSG_RHI_DEBUG_LAYER=1`。
- maplibre 两套都在云端编：OpenGL 577 秒、Vulkan 783 秒（2 核）。参数：`-DMLN_WITH_OPENGL=ON`（或 `VULKAN`）`-DMLN_QT_WITH_LOCATION=OFF -DMLN_QT_WITH_WIDGETS=OFF -DMLN_WITH_WERROR=OFF -DMLN_QT_WITH_INTERNAL_ICU=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.19`。
- 程序目录里要有 `maplibre/opengl/{bin,qml}`、`maplibre/vulkan/{bin,qml}`（云端用符号链接指到两套 install 的 `lib`、`qml`）。
- PowerShell：`micromamba create -p /opt/pwsh -c conda-forge powershell`，只用来检查 `.ps1` 的语法（是 PowerShell 7，你电脑上是 5.1，写脚本时避开 7 才有的语法）。
- Esri 瓦片在云端被代理拦截，云端截图只有底色；地球显示离线简图（Natural Earth 省界拼成的陆地）。
- 截图用合成测试数据（不是 PO 的真实数据）；真实副本在 PO 电脑的 `testdata\data.js`。

## 0.3.0 补充（2026-11-02）

- 只跑数据层测试时不需要 maplibre：`cmake -S <源码> -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/qtenv -DTP_BUILD_TESTS=ON`，然后 `cmake --build build --target tst_data`，运行 `build/tests/tst_data`。
- 真实数据往返：`TP_ROUNDTRIP_DIRS=<暂存的数据文件夹> build/tests/tst_data realRoundTrip`。
- 编整个程序但没有 maplibre 源码时，Vulkan 头文件要单独给：conda 的 GCC 不搜 `/usr/include`，所以 `apt-get install libvulkan-dev` 后把 `/usr/include/vulkan`、`vk_video` 复制到 `/opt/vkh/include/`，配置时加 `-DTP_VULKAN_HEADERS=/opt/vkh/include -DVulkan_INCLUDE_DIR=/opt/vkh/include`。
- Windows 专属代码的语法检查：`apt-get install g++-mingw-w64-x86-64-posix`，`x86_64-w64-mingw32-g++-posix -std=c++20 -fsyntax-only -DUNICODE -D_UNICODE -DNOMINMAX -I/opt/qtenv/include/qt6 -I/opt/qtenv/include/qt6/QtCore <文件>`（只检查语法，不能链接）。
- `json-bench/gen.js`：重新生成 `tests/golden/`（`node gen.js tests/golden`，固定种子，结果不变）。

## 0.4.0 补充（2026-11-02）

- 这次容器的 apt 要先 `apt-get update` 才装得上 `libvulkan-dev`、`mesa-vulkan-drivers`、`xvfb`、`fonts-noto-cjk`（中文字体，截图用）。
- conda 环境只需要 `qt6-main=6.12.0 cxx-compiler cmake ninja mesalib libglvnd-devel xorg-xorgproto pkg-config zlib`（Qt Quick Dialogs、Effects 都在 qt6-main 里）。
- 没有 maplibre 也能编整个程序、截界面：地图层（`qml/MapLayer.qml`）加载失败时显示底色，其他界面照常。`assets/admin` 没暂存时先建个空目录，否则编译后的复制步骤报错。
- 界面截图：`--screenshot ui-<名字> <输出.png>`，名字见 `qml/Main.qml` 末尾（ui-conflict、ui-ext、ui-ext-dirty、ui-closefail、ui-closefail-error、ui-settings、ui-firstrun、ui-missing、ui-nodata、ui-restore、ui-title、ui-banner）；加 `--lang en` 看英文；只读 / 更新版本横幅用 `--data` 指向一个坏的 / version 5 的 data.js。
- 假装电脑上有 Electron 版：环境变量 `TP_ELECTRON_SETTINGS=<一个 settings.json>`（里面写 `dataDir`）。
- 测试：`cmake --build build --target tst_data tst_app`，运行 `build/tests/tst_app`（含缺英文检查）。
- Windows 专属代码语法检查（MinGW）要加 `-I/opt/qtenv/include/qt6/QtNetwork`（单实例）。

## 文件

| 文件 | 用途 |
|---|---|
| `run.sh` | 云端运行原型：`run.sh opengl|vulkan --screenshot street out.png` |
| `mkzip.py` | 把 maplibre-native-qt 工作副本打成可复现的 zip，并统计 Windows 下路径超长的文件 |
| `towin.py` | 把 .bat 转成 CRLF、.ps1 转成 CRLF + UTF-8 BOM（PowerShell 5.1 读中文要 BOM） |
| `proto-maptest/` | 7.0.0-a 的最小测试（地图 + RhiItem 三角形） |
| `json-bench/gen.js` | 生成 JSON 读写的 Node 基准文件（0.3.0） |

## 0.5.0 补充（2026-11-03）

- 这次容器里 `apt-get install libvulkan-dev` 找不到包（没先 `apt-get update`）；改用 conda：`micromamba install -p /opt/qtenv -c conda-forge libvulkan-headers`，配置时 `-DTP_VULKAN_HEADERS=/opt/qtenv/include -DVulkan_INCLUDE_DIR=/opt/qtenv/include`。Xvfb 要先 `apt-get update`。
- 截图页 `ui-backup`、`ui-backup-running`（设置面板备份一节展开；后者演示进度 312 / 840）；截图前在编译目录放一份 `settings.json`（`backupDir`、`autoBackup`、`lastBackup`）就能看到"几天前备份过"。
- `ui-devtest`：用 `--data <文件夹名带 test 的副本>/data.js` 跑，自动做"改两下 → 改一下 → 撤销 → 重做 → 撤销 → 备份"，看 app.log 和副本里的 data.js / data.backup.js / .history。

## 0.5.1 补充（2026-11-03）

- conda 环境这次一起装了 `libvulkan-headers nodejs`（Node 用来跑 Electron 代码当基准，例如错误码哈希）；apt 先 `apt-get update` 再装 `xvfb fonts-noto-cjk g++-mingw-w64-x86-64-posix`。
- 截图页 `ui-error`（两张出错小框叠在外部修改卡片上，上面那张展开）、`ui-crash`（"上次意外退出了"）。
- `--test-error q|m|hang`：启动 3 秒后故意制造 QML 错误 / C++ 异常 / 卡住 12 秒（开发版才有）。q、m 用 `--screenshot ui-banner <png>` 跑（截图时小框已出现）；hang 用 `--memtest 1` 跑（截图模式 5 秒就退出了）。结果看 `~/.local/share/TravelPlannerNative/logs/errors.log`、`app.log`（Linux 上日志在这里）。截图 / 内存测试运行时看门狗只写日志、不弹框。
- 崩溃处理、系统对话框只有 Windows 实现，云端只能 MinGW 语法检查（`-Wall -Wextra`）：`x86_64-w64-mingw32-g++-posix -std=c++20 -fsyntax-only -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 -I/opt/qtenv/include/qt6 -I/opt/qtenv/include/qt6/QtCore -Isrc/app -Isrc/platform src/platform/crashhandler.cpp`。
- 检查配置时 qmlimportscanner 有没有扫进 third_party：放一个故意写错的 `third_party/maplibre-native-qt/src/docs/examples/snippets_Map.qml` 再配置，不应出现 "Expected token" / "Syntax error"（这个假文件不要写回 PO 电脑）。
- `.ps1` 语法检查：`/opt/pwsh/bin/pwsh -NoProfile -Command '[System.Management.Automation.Language.Parser]::ParseFile(<路径>,[ref]$t,[ref]$e)'`，`$e` 为空就是没有语法错误。

## 0.5.2 补充（2026-11-03）

- 截图页 `ui-devtools`：设置面板滚到最底下的"开发版测试"（看"制造错误"那几个按钮）。
- MinGW 检查 `src/app/tpapplication.cpp` 时，Linux 版 Qt 没有 `QtGui/qwindowdefs_win.h`：建一个只有 `#pragma once` 的同名文件，加 `-I<那个目录>`。
- **MSVC 专属的 `__try / __except`**（MinGW 的 GCC 不支持）用 clang 做语法检查：`apt-get install clang`，把 `#ifdef _MSC_VER` 临时换成 `#if 1` 写到 /tmp 的副本里，再
  `clang++ --target=x86_64-w64-windows-gnu -std=c++20 -fsyntax-only -fms-extensions -Wall -Wextra -Wno-ignored-attributes -DUNICODE -D_UNICODE -DNOMINMAX -D_WIN32_WINNT=0x0A00 -nostdinc++ -isystem /usr/lib/gcc/x86_64-w64-mingw32/13-posix/include/c++ -isystem /usr/lib/gcc/x86_64-w64-mingw32/13-posix/include/c++/x86_64-w64-mingw32 -isystem /usr/lib/gcc/x86_64-w64-mingw32/13-posix/include/c++/backward -isystem /usr/x86_64-w64-mingw32/include -I<stub> -I/opt/qtenv/include/qt6 -I/opt/qtenv/include/qt6/QtCore -I/opt/qtenv/include/qt6/QtGui -Isrc/app -Isrc/platform <副本>`。
  故意写错一个名字（如 `GetExceptionInformationX`）确认它真的在检查 SEH 那几行。只是语法检查，MSVC 的 C2712 等规则还要 PO 编译时确认。
- `tst_data` 有 3 项（realRoundTrip、storeSaveUnchanged、undoSharesUnchanged）要 `testdata/data.js`，没暂存时会失败；暂存后 90 项全过。

## 0.6.0 补充（2026-11-03）

- 0.6.0 起数据是 travel.json：测试和截图默认用 `testdata/travel.json`（由 `testdata/data.js` 转换：`TP_WRITE_TESTDATA=1 build/tests/tst_data importRealData`）。`tst_data` 的 storeSaveUnchanged 要它，importRealData / importRealDataRun / realRoundTrip 要 `testdata/data.js`。
- 截图页（0.6.0）：`ui-import`（选方式）、`ui-import-running`、`ui-import-report`、`ui-import-failed`、`ui-electron-folder`、`ui-locked`、`ui-newer`、`ui-unreadable`、`ui-takeover`；`ui-firstrun` 要有 Electron 选项时设 `TP_ELECTRON_SETTINGS`。
- 端到端导入：建一个假的 Electron 文件夹（真实 data.js + 按引用路径造的小文件），`TP_ELECTRON_SETTINGS=<settings.json> TravelPlanner --data <空文件夹> --screenshot ui-import-e2e <png>`，看 app.log 的"导入完成"一行和空文件夹里生成的 travel.json、photos。
- 这次 conda 环境：`qt6-main=6.12.0 cxx-compiler cmake ninja mesalib libglvnd-devel xorg-xorgproto pkg-config zlib libvulkan-headers nodejs`；apt 先 `apt-get update` 再装 `xvfb fonts-noto-cjk g++-mingw-w64-x86-64-posix`。没有 maplibre 时地图层空白，`assets/admin` 暂存过来左下角才显示数据统计。

## 0.7.0 补充（2026-11-03）

- 照片测试要 exiftool 和 pillow-heif：`apt-get install exiftool`（apt 先 `apt-get update`）；`pip install --break-system-packages pillow-heif`（Pillow 系统自带，含 WebP）。都只在云端用，不进项目。
- 测试照片：`python3 tools/claude-kit/gen_photo_fixtures.py /tmp/photos tests/photos.txt`（结果固定，期望值写在 tst_photo 里）。**图片不要直接写回 PO 电脑**：写回时会被加上 C2PA 内容凭证（0.7.0 实测，每张多约 5.8 KB，MD5 对不上），所以打包成 base64 文本。
- 真实照片对照：`git clone --depth 1 https://github.com/ianare/exif-samples`，然后
  `exiftool -j -n -File:ImageWidth -File:ImageHeight -Rotation -EXIF:Orientation -EXIF:Make -EXIF:Model -EXIF:DateTimeOriginal -EXIF:GPSLatitude -EXIF:GPSLongitude -EXIF:GPSLatitudeRef -EXIF:GPSLongitudeRef -@ <文件列表> > samples.json`，
  把 SourceFile 改成绝对路径，`TP_EXIF_REAL=samples.json build/tests/tst_photo`（exifReal 对照，exifFuzz 也会拿这些照片随机损坏）。
- AddressSanitizer：另建编译目录，`-DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"`，只编 tst_photo，运行时 `ASAN_OPTIONS=detect_leaks=0`。
- 自检（0.7.2）：`TravelPlanner --selftest <报告.txt> --data <数据副本>`（只在开发版，会改数据）。云端用"截图页"那份数据的副本跑，报告里每项 ✓ / ✗，退出码 = 失败个数。PO 那边由 `验证.bat` 调用。每个版本把新功能的检查加进 `src/app/selftest.cpp`，并改 `tools/win/verify.ps1` 的 `$Pages`。
- .ps1 语法检查用 PowerShell 7（`micromamba create -p /opt/pwsh -c conda-forge powershell`），写法保持 5.1 能跑。
- 截图页（0.7.0）：`ui-photos`（照片测试页，选中第一张）、`ui-photo-rotate`（转第一张能转的）、`ui-photo-import`（把 tests/photos.txt 里的照片当成选中的文件，停在确认表）、`ui-photo-clean`（清理框）、`ui-settings-photos`、`ui-photo-e2e`（导入 → 撤销 → 重做 → 旋转 → 清理，看 app.log 的 "e2e" 行）。要一个有照片的数据文件夹：拿 testdata/travel.json，只留文件存在的照片记录，再加几条测试照片的记录；放一个旧的多余文件和一个 .originals 里的孤儿原图测清理。`--data` 可以用中文路径。

## 0.8.0 补充（2026-11-03）

- maplibre-native-qt 直接 `git clone https://github.com/maplibre/maplibre-native-qt.git`，`git checkout 399311ee…`，`git submodule update --init --recursive`（约 6 GB，十几分钟）。和 `third_party\maplibre-native-qt\src` 逐字相同（抽查 map_quick_item.cpp、map.hpp）。
- 编译参数同 7.0.0-b，加 `-DCMAKE_BUILD_TYPE=RelWithDebInfo -DCMAKE_INSTALL_PREFIX=…`，`cmake --install`。这次 2 核：OpenGL 约 35 分钟（和程序一起编时更慢）。程序目录放 `maplibre/opengl/bin` → `<install>/lib`、`maplibre/opengl/qml` → `<install>/qml` 两个符号链接；运行时 `LD_LIBRARY_PATH=<install>/lib`。
- `assets/app`（图标）没暂存时先建个空目录，否则编译后的复制步骤报错。
- 自检：`TravelPlanner --backend=opengl --data <travel.json 副本所在文件夹> --selftest <报告>`。地图部分要 maplibre；照片部分 120 张照片文件不存在也能跑（只做测试照片那部分）。
- 退出测试：`--hide-before-exit`（配合 `--screenshot`）= 先藏起窗口再退出（托盘）。不先释放地图时退出码 139，正常是 0；`gdb -batch -ex run -ex bt --args …` 看栈。
- 交互：`apt-get install xdotool imagemagick`，普通方式运行（不加 --screenshot），`xdotool mousemove … mousedown 1 … mouseup 1`、`click 4`（滚轮）、`key ctrl+q`，`import -window root` 截屏。
- "已解锁"对照：`node tools/claude-kit/unlock-golden/gen.js <Electron map-app.js> testdata/data.js assets/admin/adm1.geojson tests/golden/unlock-colors.json`（把 Electron 里 inRing … computeUnlocked 原样截出来跑）。Electron 改了这段代码时重新生成。

## 0.8.2 补充（2026-11-03）

- 这次 `git submodule update` 中途断过一次（`curl 56 Connection reset`），git 自己重试后成功，不用管。
- 合并对照：`node tools/claude-kit/marker-golden/gen.js <Electron map-app.js> testdata/data.js tests/golden/marker-clusters.json`（Node 用 conda 环境里的 `/opt/qtenv/bin/node`）。
- 标记图片单独看：写个小程序调 `MarkerImages::paintStop / paintCluster / paintGlow` 拼成一张图（`QT_QPA_PLATFORM=offscreen` 就行，QPainter 不要 OpenGL）。图标字体 `assets/fonts/MaterialSymbolsRounded.ttf`，数字用 `fonts-noto-cjk` 的 Noto Serif CJK SC。
- 截图页（0.8.2）：`markers`（选中一条 ≥4 站的路线、聚焦第 3 站、显示提示框，7 级）、`spread`（20 m 内重叠的站点，15 级）、`pins`（"想去"的单一目的地，8 级）。
- 图标字体从 woff2 转 TTF：`pip install --break-system-packages fonttools brotli`，`TTFont(woff2).flavor=None; save(ttf)`；码位表 `src/map/msicons.h` 从字体的 GSUB 连字表读出（见文件头）。

## 0.8.3 补充（2026-11-04）

- **写回的坑**：同一个输出文件名第二次写回时，写回工具可能写进去的是**上一次**的内容（修改时间变了、大小和内容还是旧的；这次 `v0.8.3记录.md`、`项目状态.md` 就是这样）。每次写回用没用过的输出文件名（例如加 `-r2`），写回后一定再暂存比对 MD5。

## 0.8.4 补充（2026-11-04）

- 这次 PO 电脑上的 shell 又没起来，照旧暂存 → 云端改 → 写回 → 比对 MD5；写回用这次没用过的输出文件名（0.8.3 的坑）。
- maplibre 只编了 OpenGL 版（2 核约 40 分钟，和程序同时编）；Vulkan 版这次没编（改动在 QML / C++，和图形后端无关，PO 电脑上验证.bat 用的是 Vulkan）。
- 联网单元测试 `tst_net`：本机 127.0.0.1 上临时开 QTcpServer 当假服务器，不连外网；`QT_QPA_PLATFORM=offscreen build/tests/tst_net`。
- routing.openstreetmap.de 在云端被代理拦（CONNECT 403），自检"真的请求一次路线服务"在云端必然失败（还有 0.8.3 的"底图瓦片"一项），其余都应该过。
- 道路路线对照：`node tools/claude-kit/road-golden/gen.js <Electron map-app.js> testdata/travel.json tests/golden/roads.json`（Electron 改了编解码 / 抽稀 / legKeyOf 时重新生成）。
- 截图页（0.8.4）：`lines`（站点最多的路线，鼠标"碰到"它中间一段：高亮、其余变淡、提示框；5 级）、`roadbar`（假装有 7 段还没算，看提示条；不动数据）。
- 截图 / 自检 / 内存测试时 QML 里 `testRun` 为真：地图的 HoverHandler / 点线不跟真的鼠标（Xvfb 里鼠标停在窗口上时会把截图里的悬停打掉；PO 电脑上自检时鼠标在窗口上也会干扰）。
- 交互测试（xdotool）：普通方式运行，`import -window root` 截屏找到路线颜色的像素，`xdotool mousemove` 过去 → 提示框和高亮；`click 1` 选中；`key Escape`、`key ctrl+q`。


## 0.8.5 补充（2026-11-04）

- 这次 PO 电脑上的 shell 还是没起来（"Workspace unavailable"），照旧暂存 → 云端改 → 写回 → 比对 MD5。连接文件夹时一次申请多个被拦了（"safety check"），改成一个一个申请（`D:\旅行计划Native`、`D:\旅行计划App` 各一次）就行。
- 本机瓦片服务的单元测试（tst_net 的 tile*）：假的上游也是 127.0.0.1 上的 QTcpServer，`TileServer::setUpstreamRewrite` 把 Esri 地址换成它；不连外网。
- 预取对照：`node tools/claude-kit/tile-golden/gen.js <Electron map-app.js> tests/golden/prefetch.json`（Electron 改了 tileXY / prefetchTiles 时重新生成）。
- 云端瓦片被代理拦：本机瓦片服务照样起来，地图要的瓦片都是 502。要看真的有瓦片的样子，开一个假上游：`python3 tools/claude-kit/tile-golden/fake-upstream.py 8765`（按 `/<服务>/MapServer/tile/<z>/<y>/<x>` 用 PIL 画一张带编号的格子图），运行时加环境变量 `TP_TILE_UPSTREAM=http://127.0.0.1:8765`（只在开发版认），截图、自检里就有底图了，硬盘缓存也会真的存进 `<编译目录>/cache/tiles`。
- 不接假上游时，自检的"地图的瓦片存进硬盘缓存""缓存里有的直接给"两项在云端必然失败（和"底图瓦片画出来了""真的请求一次路线服务"一样）；接了假上游这几项都过。接了假上游、有瓦片时，0.8.4 的"线条透明度立刻生效"在 llvmpipe 上会失败（0.12 秒时还没画出高亮层，0.22 秒时已经画了；软件渲染太慢），没有瓦片时过。
- 截图页（0.8.5）：`layers` / `layers-sat`（图层泡泡打开，街道图 / 卫星图）、`tilefail`（假装连续失败 8 次的横幅）、`unlock-edit`（手动标记面板 + 鼠标"碰到"一个手动标记过的省）、`unlock-search`（搜"四川"）。
- 原型顶栏里的底图按钮去掉了：截图 / 自检要换底图直接 `mapc.base = …`（QML）或 `mapc->setBase(…)`（C++）。

## 0.8.6 补充（2026-11-04）
- **后台进程活不过一轮回复**：回复结束几分钟后容器里后台跑的程序会被停掉（这次 maplibre 编到一半、Xvfb、假上游都被停了）。长时间编译要在同一轮里一边跑一边隔一两分钟看一次进度；ninja 中断后重跑会接着编。下一轮开头先 `pgrep Xvfb`，没有就重开（注意 `pgrep -f <名字>` 会匹配到自己这条命令）。
- 这次 maplibre 只编了 OpenGL 版（2 核，前后约 1 小时）。
- 分步内存测试（云端）：`TravelPlanner --backend=opengl --memtest 4 --memsteps <csv> [--memtest-skip globe,unlock,sat]`，Linux 上私有内存读的是 RssAnon（glibc 释放后不一定还给系统，只能看趋势），显存那两列是空的。看堆用 heaptrack：`heaptrack -o <输出> ./TravelPlanner …`，`heaptrack_print -f <输出>.gz -p 1 -n 25 -s 3` 看峰值来源。0.8.6：堆峰值 594 MB（0.8.5 是 795 MB），峰值出在地球拉近时（z4 拼图 + llvmpipe 的纹理）。
- 模拟屏幕缩放比例：`QT_SCALE_FACTOR=1.25`（卫星瓦片 205，看 `<编译目录>/cache/map/style-v5-sat-205.json`）。
- 有瓦片（接假上游或缓存里有）时，llvmpipe 上除了 0.8.5 说的"线条透明度立刻生效"，0.8.4 的"点线选中；碰到选中的计划的线不再高亮"也会失败（选中后 0.3 秒变淡程度还没到 1）；清空 `<编译目录>/cache/tiles`、不接上游时两项都过。

## 0.8.7 补充（2026-11-05）
- PO 电脑上的 shell 还是没起来，照旧暂存 → 云端改 → 写回。这次连接文件夹时一次申请三个（源码、Electron 源码、日志）没被拦。
- 编译程序要 Vulkan 头文件：maplibre 源码还没克隆完时用 conda 的 `vulkan-headers`（`micromamba install -p /opt/qtenv -c conda-forge vulkan-headers`），cmake 加 `-DVulkan_INCLUDE_DIR=/opt/qtenv/include`。
- 程序本身不用 maplibre 也能编；没有 maplibre 时 QML 里的地图层加载失败，`--screenshot ui-*` 照样能跑（顺便看有没有 QML 报错）。
- Bash 工具单次最多等 2 分钟（不加 timeout 参数时）：长编译一律 `nohup … &` 放后台，隔一两分钟看一次；不要在同一个编译目录同时跑两个 ninja。
- 计划对照：`/opt/qtenv/bin/node tools/claude-kit/plans-golden/gen.js <Electron map-app.js> <Electron mapfirst.js> testdata/data.js tests/golden/plans.json`。
- 截图页（0.8.7）：`now`（一条带日期的路线设为下次出发、假装今天是第 3 天、打开此刻卡片）、`sheet`（≥ 4 站的路线、聚焦第 2 站）、`filter`（关掉"已完成"、打开筛选下拉）。截图会改数据副本（设为下次出发），用副本跑。
- QML 里 `Item` 自带 `focus` 属性，自定义组件不要再声明叫 `focus` 的属性（0.8.7 简易面板踩过）；中文字符串不能放进 `QLatin1String`（会按 Latin-1 解，比较永远不相等；0.8.7 区域旧名踩过）。
- QML 里的属性名不要以 on + 大写字母开头（如 `onMap`）：会被当成信号处理器的写法，编译不报错，但用到它的绑定全都不对（0.8.7 踩过，改名 `mapShown`）。

## 0.8.8 补充（2026-11-05）
- PO 电脑上的 shell 还是没起来，照旧暂存 → 云端改 → 写回。两个文件夹（源码、Electron 源码）一次申请没被拦。
- 这次 conda 一起装了 `libvulkan-headers vulkan-headers nodejs`，配置时 `-DTP_VULKAN_HEADERS=/opt/qtenv/include -DVulkan_INCLUDE_DIR=/opt/qtenv/include`；maplibre 只编 OpenGL 版（克隆约 15 分钟、编译约 35 分钟，和程序同时编）。
- 假上游（`fake-upstream.py 8765`）要在程序启动前开好，否则第一批瓦片 502、会出"底图加载失败"横幅。
- 截图页（0.8.8）：`bubbles`（指定下次出发后打开 ⋯ 泡泡）、`list`（我的旅程 + 选中一条路线 + 隐藏一个已完成的计划）、`palette`（两个计划算最近打开过，Ctrl K 没输入）、`menu`（头像菜单）。这几页会改数据副本（设为下次出发）。
- 自检里点界面、按键：用 `QWindowSystemInterface::handleMouseEvent / handleKeyEvent<SynchronousDelivery>`（和 QTest 一样）。`QCoreApplication::sendEvent(window, QMouseEvent)` 在这里点不动按钮；按键的 `Shortcut` 要窗口有焦点（先 `requestActivate()`，Windows差异清单 #22）。
- 自检按 objectName 找 Repeater / ListView 建的行：`findChild` 找不到（它们不是 QObject 子对象），要沿 `contentItem()->childItems()` 找（`selftest_shell.cpp` 的 `findItem`）。
- QML 里函数不要叫 `escape`（JS 的全局函数，qmlcachegen 报 "Illegal method name"）。
- 交互看一眼：`TravelPlanner --backend=opengl --data <副本>` 普通方式运行，`xdotool mousemove 36 32 click 1`（⋯）、`xdotool key ctrl+k`，`import -window root` 截屏。

## 0.8.9 补充（2026-11-06）
- PO 电脑上的 shell 还是没起来，照旧暂存 → 云端改 → 写回。
- maplibre 照 0.8.8 只编 OpenGL 版，装到 `/opt/mln-gl-install`（2 核约 1 小时，和程序同时编更慢）；之后 `mkdir -p <编译目录>/maplibre/opengl`，两个符号链接 `bin → <install>/lib`、`qml → <install>/qml`，运行时 `LD_LIBRARY_PATH=<install>/lib`。没有 maplibre 时编辑器出不来（`mapShown` 要地图），只能看 C++ 和单元测试。
- 区域建议对照：`/opt/qtenv/bin/node tools/claude-kit/region-golden/gen.js <Electron map-app.js> tests/golden/regions.json`（Electron 的 `suggestRegion` 原样拿来跑一组坐标；tst_map planEditRegions 比对）。
- 截图页（0.8.9）：`editor`（编辑 ≥ 4 站的路线，展开第 2 站）、`editor-new`（新建，信息卡片展开）、`editor-search`（编辑 + 三条示例结果，镜头自己移过去）、`editor-style`（计划的样式浮层）、`controls`（街道图上 ± / 比例尺 / 图例 / 我的位置）、`locate`（假装拿到位置）、`narrow`（700 宽 + 简易面板抽屉）、`narrow-editor`（700 宽 + 编辑器抽屉）、`narrow-menu`（1000 宽 + 头像菜单的页面入口）。这几页会改数据副本。
- 编辑器的自动保存 / 算单段是定时器（400 ms / 700 ms）：自检里用 `editor.setDelays(…)` 改短，联网用 `editor.setFetchForTest(…)`、`geocoder.setFetchForTest(…)` 模拟（不走网络）；自检"真的问系统定位"在云端拿不到（geoclue2 没有位置），只当信息。
- 自检报告里"真的请求一次路线服务"在云端必然 ✗（代理拦了），其余应全过。
- QML：`Repeater` 的 model 给整份数组时，数组每变一次（每打一个字）所有行重建、输入框丢焦点；编辑器的站点列表改成 `model: 个数`，行里自己按 index 取数据。`Row` 里放 `width: parent.width` 的子项、`Flow` 里的子项用 anchors 都会出"polish loop"警告。`RotationAnimator on rotation` 的 `onStopped` 里要写 `id.rotation`（直接写 `rotation` 是全局属性，报 Invalid write）。`Text` 的 `height: visible ? implicitHeight : 0` 会出 binding loop，改在用它的地方判断 `visible`。

## 0.8.10 补充（2026-11-06）
- PO 电脑上的 shell 还是没起来，照旧暂存 → 云端改 → 写回。连接文件夹一次申请多个被拦，`D:\旅行计划Native` 单独申请通过；日志文件夹单独申请也被拦了。
- **后台进程被停会弄坏克隆**：`git submodule update --init --recursive` 中途被停（回复结束几分钟后后台程序会被停，0.8.6 补充）以后，有些子模块目录里只剩 `.git` 文件、没有内容，再跑 `submodule update` 也不会补（状态显示正常），maplibre 配置时报 "Dependency with include path … not found"。补法：`git submodule foreach --quiet --recursive 'n=$(ls -A | grep -v "^.git$" | wc -l); [ "$n" = 0 ] && git checkout -f -q HEAD'`。另外断掉重跑时代理端口可能变了，旧的克隆进程会连不上（`Failed to connect to 127.0.0.1 port …`），要重开。
- 这次 maplibre 只编 OpenGL 版，和程序同时编约 55 分钟。
- 截图页（0.8.10）：`modes`（打开 ⋯，8 颗泡泡）、`modes-narrow`（640 宽，⋯ 排两行）、`mode-foot` / `mode-wish` / `mode-photo`（切到那种模式、打开列表）、`ledger`（账本占位页）。`narrow`、`narrow-editor` 改成 500 宽（抽屉断点 520）。这几页会改数据副本（设为下次出发）。
- 截图 / 自检时窗口摆在屏幕可用区域正中；`TP_TEST_WINDOW_AT=x,y` 故意摆到某处（复现窗口一部分在屏幕外；X11 上合成的鼠标事件照样能点到屏幕外的部分，复现不了 PO 电脑上的点不中）。
- 截图 / 自检固定从"计划"模式开始，"也显示"叠上想去、已完成（和 0.8.9 一样画全部计划），不读也不写 settings.json 里的 `mapMode` / `modeAlso` / `modeBase`。

## 0.9.0 补充（2026-11-06）
- PO 电脑上的 shell 还是没起来，照旧暂存 → 云端改 → 写回。连接文件夹一次申请三个被拦，`D:\旅行计划Native`、`D:\旅行计划App` 分开申请通过。
- conda 这次要单独补 `qt6-positioning=6.12.0`（0.8.9 起程序用 Qt Positioning，只装 qt6-main 配置时报找不到 Positioning）。完整的：`qt6-main=6.12.0 qt6-positioning=6.12.0 cxx-compiler cmake ninja mesalib libglvnd-devel xorg-xorgproto pkg-config zlib libvulkan-headers vulkan-headers nodejs`。
- maplibre OpenGL 版这次 2 核约 45 分钟（克隆约 15 分钟，和程序同时编）。
- **假上游先确认真的起来了**：`pgrep -f "fake-upstream.py 8765"` 会匹配到这条命令自己，以为已经在跑、其实没开（这次头一轮地球截图全是"Connection refused"）。用 `curl --noproxy '*' http://127.0.0.1:8765/World_Imagery/MapServer/tile/1/0/0` 看是不是 200。
- 截图页（0.9.0）：`globe`（卫星图，对准晨昏线）、`globe-unlock`（已解锁，对准东亚）、`globe-near`（上海附近 6 级，看细一级的瓦片）。没有 maplibre 也能截（地图层加载失败，地球照常），`--backend=vulkan` 时用 lavapipe 看地球在 Vulkan 上对不对。
- 地球的状态在左下状态栏最后一段（几块、最细几级、显卡几层）；QML 里 `globe.tileStats()` 有详细的数。
- **MinGW 语法检查要 windows.h**：带 Qt Gui 的文件要一个 `QtGui/qwindowdefs_win.h` 桩，内容写 `#pragma once` + `#include <windows.h>`（0.5.2 的空桩不够：Qt 头文件要 HRGN、HBITMAP 这些类型）。这样也能查出 `far`、`near` 这种 Windows 宏（Windows差异清单 #27）。命令：`x86_64-w64-mingw32-g++-posix -std=c++20 -fsyntax-only -Wall -Wextra -DUNICODE -D_UNICODE -DNOMINMAX -D_USE_MATH_DEFINES -I<桩> -I$Q -I$Q/QtCore -I$Q/QtGui -I$Q/QtQuick -I$Q/QtQml -I$Q/QtNetwork -I$Q/QtGui/6.12.0 -I$Q/QtGui/6.12.0/QtGui -I$Q/QtCore/6.12.0 -I<编译目录>/generated -Isrc/map -Isrc/data -Isrc/services -Isrc/app -Isrc/platform <文件>`（`Q=/opt/qtenv/include/qt6`）。
- 自检里模拟"每 16 毫秒拖一下"不要用 `pause(16)`：软件渲染时处理一次事件要上百毫秒，用 `QThread::msleep(16)` 干等。

## 0.9.1 补充（2026-11-06）
- PO 电脑上的 shell 还是没起来，照旧暂存 → 云端改 → 写回。`D:\旅行计划Native` 和日志文件夹一次申请两个通过。
- 回复一结束，后台跑的自检、Xvfb、假上游都会被停（0.8.6 补充说过）：自检要在同一轮里等它跑完（用时约 2.5 分钟，`timeout … bash -c 'until grep -q "RC=" log; do sleep 5; done'` 一次最多等 2 分钟，分两次等）。
- 计时：app.log 里 "[启动]"、"[计时]" 开头的行；自检"性能（0.9.1）"一节在最前面（约 40 秒）。云端 llvmpipe 的数只看相对大小。
- 中文字符串比较：`e.what == QLatin1String("数据重建")` 永远不相等（0.8.7 补充说过），用 `QStringLiteral`；这次又踩了一次。

## 0.9.3 补充（2026-11-06）
- 连接文件夹：一次申请四个被自动拦了一次，PO 说同意后同一组再申请就通过。这次 **PO 电脑上的 shell 能用**（device_bash）：在源码文件夹里 `tar -czf` 整份源码（CMakeLists、CHANGELOG、src、qml、shaders、i18n、tests、tools、assets、testdata、installer、claude）放进【删除】再暂存到云端，一次就到（约 8 MB），比一个个暂存快；同时记一份各文件的修改时间（`find … -printf '%T@ %s %p'`），写回前对照。shell 的 /tmp 不能暂存，要放进连接的文件夹。shell 里对整个源码文件夹（含 third_party）跑 `du` 会超时，按子文件夹跑。
- 没有 maplibre 也能测地球：`TP_SELFTEST_ONLY=globe`（逗号分隔，perf、map、markers、routes、tiles、unlock、memory、globe、plans、shell、pages、editor、photos）只跑这几节；这次 OpenGL / Vulkan 各约 20 秒。
- 截图页（0.9.3）：`globe-night`（此刻天最黑的大城市附近，看城市灯光）；`globe` 照旧（左边白天、右边夜里，看光晕）。
- Python 改 .ps1 时 `open(…, encoding='utf-8')` 读写会把 CRLF 变成 LF：改完用 `towin.py` 转回 CRLF + BOM 再写回。
- 云端访问 assets.science.nasa.gov 被拦（403）；NASA 的图让 PO 下载。

## 0.9.5 补充（2026-11-06）

- 自检要开发版：`-DCMAKE_BUILD_TYPE=Release` 时没有 `TP_DEV_TOOLS`，`--selftest` 直接退出（码 6）。用 `RelWithDebInfo`（可以 `-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2` 省掉调试信息加快编译）。数据副本的文件夹名要带 test（如 `/tmp/data-test/travel.json`）。
- 截图页（0.9.5）：`globe-markers`（地球上框住所有站点：标记、路线、飞行弧线）、`globe-select`（选中最长一段飞行的路线、交通小图标）。`globe-night` 改成拉到最远（城市灯光只在最远时显示）。
- 没有 maplibre 时 `plans,shell,pages` 三节有 16 项本来就过不了（简易面板、列表等要平面地图在）；0.9.5 和 0.9.4 编出来对比过，失败的是同样 16 项。地球那一节不受影响（`TP_SELFTEST_ONLY=globe`，OpenGL / Vulkan 各 32 项）。
- 没接假上游时卫星瓦片全失败，"连续失败 8 次"的横幅会先出来；0.9.5 起自检那一项开始前先清零。

## 0.9.7 补充（2026-11-06）
- PO 电脑上的 shell 能用：照 0.9.3 打包整份源码暂存（约 9 MB，一次到）；文档在 PO 电脑上直接精确替换，源码改完打包写回、在那边解开、比对 MD5。
- **maplibre 要带 `-DNDEBUG`**：只写 `-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2` 会把 RelWithDebInfo 默认的 `-DNDEBUG` 一起去掉，maplibre 里的 assert 生效，一开地图就在 `renderer_backend.cpp:53 assumeViewport` 断言失败退出（码 134）。要写 `"-DCMAKE_CXX_FLAGS_RELWITHDEBINFO=-O2 -DNDEBUG" "-DCMAKE_C_FLAGS_RELWITHDEBINFO=-O2 -DNDEBUG"`（程序本身用 `-O2` 没关系）。
- 子模块补内容的命令要以 `true` 结尾，不然 `foreach` 碰到第一个不用补的子模块就停（返回值非 0）：`git submodule foreach --quiet --recursive 'n=$(ls -A | grep -v "^.git$" | wc -l); if [ "$n" = 0 ]; then git checkout -f -q HEAD; fi; true'`。这次所有子模块都完整，不用补。
- 计时（2 核）：conda + apt 59 秒；maplibre 克隆 11.3 分钟、OpenGL 版编译 14.4 分钟；程序完整编译约 8 分钟；单元测试约 20 秒；全套自检约 2.5 分钟（有 maplibre、接假上游）。详见 `claude/v0.9.7记录.md` §5。
- 截图页（0.9.7）：`morph-half` / `morph-flat`（上海附近 5.5 级，地球摊到一半 / 摊平）、`morph-ref`（同一镜头的平面卫星图，和 morph-flat 对照）、`filter`（计划模式只画计划中、打开筛选下拉）、`filter-overlay`（再打开"叠上其他旅程"）。截图 / 自检时四种状态都按正常样子画（`PlanController::setTestAllStatuses(true)`），`filter*` 两页会关掉它。
- 自检等待（0.9.7 起，`selftest_util.h`）：`waitFrames`（再画 n 帧）、`waitMapIdle`（平面停下来）、`waitRendered`（平面停下来、地图库要的瓦片都回来了）、`waitGlobeIdle`（地球停下来、瓦片到齐）、`waitStill`（界面元素停稳）。新写自检照这些用，固定时长的 `pause` 只留给量时间、动画时长、点击之间的间隔。
- 自检里测自动摊平 / 卷回要 `win.morphTest = true`（截图 / 自检平时不自动）；没有 maplibre 时没有平面地图，变形不播（直接淡入淡出），"平面上缩到 5 级以下卷回地球"那项过不了。
