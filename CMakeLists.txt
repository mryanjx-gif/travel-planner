# 旅行计划 原生版（Qt）
# 版本号的唯一来源是下面的 TP_VERSION；窗口标题、关于页、安装程序都从这里取。
# 规则（PO 2026-11-02）：大更新改第一位，中等更新改第二位，小修改改第三位；正式版之前是 0.x.y，正式版 1.0.0。
cmake_minimum_required(VERSION 3.24)

set(TP_VERSION "1.2.0")
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" TP_VERSION_NUMERIC "${TP_VERSION}")

project(TravelPlannerNative
    VERSION ${TP_VERSION_NUMERIC}
    DESCRIPTION "旅行计划 原生版"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    # 源码统一 UTF-8；Windows 10 1809 起
    add_compile_options(/utf-8 /permissive- /Zc:__cplusplus /W4)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00 NOMINMAX UNICODE _UNICODE _USE_MATH_DEFINES)
endif()

# Vulkan 头文件：用 maplibre-native 自带的一份（Qt 的 QVulkanInstance 头文件需要它；你电脑上没装 Vulkan SDK）。
# 在 find_package(Qt6) 之前告诉 Qt 去哪找，免得出现 "Could NOT find WrapVulkanHeaders"。
set(TP_MAPLIBRE_SRC "${CMAKE_SOURCE_DIR}/third_party/maplibre-native-qt/src" CACHE PATH "maplibre-native-qt 源码目录")
set(TP_VULKAN_HEADERS "${TP_MAPLIBRE_SRC}/vendor/maplibre-native/vendor/Vulkan-Headers/include" CACHE PATH "Vulkan-Headers/include")
if(EXISTS "${TP_VULKAN_HEADERS}/vulkan/vulkan.h" AND NOT Vulkan_INCLUDE_DIR)
    set(Vulkan_INCLUDE_DIR "${TP_VULKAN_HEADERS}" CACHE PATH "Vulkan 头文件（maplibre 自带）")
endif()
# 用 QRhi 必须带 GuiPrivate；它的"绑定 Qt 小版本"警告我们知道（Qt 版本固定在 6.12），不再显示
set(QT_NO_PRIVATE_MODULE_WARNING ON)

# Qt 只按 LGPL-3 使用：只动态链接，只用 LGPL-3 模块（见 claude/技术选型.md §4）。
# ShaderTools 只在编译时提供 qt_add_shaders（调用 qsb），运行时不链接。
find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui GuiPrivate Quick Network Positioning ShaderTools)
qt_standard_project_setup(REQUIRES 6.12)

# maplibre-native-qt：程序不链接它，只在运行时通过 QML 插件加载（两套：maplibre/vulkan、maplibre/opengl，见 src/platform/graphicsbackend.h）。

configure_file(src/app/version.h.in "${CMAKE_BINARY_DIR}/generated/version.h" @ONLY)

# 数据层（阶段 ①）：数据读写、保存流程、外部修改检测；0.5.0 加自动保存节拍、撤销栈、备份；0.6.0 改成原生版自己的格式（travel.json）、锁文件、从 Electron 版导入。单独成静态库，程序和单元测试共用；只依赖 Qt Core。
add_library(tp_data STATIC
    src/data/jsonvalue.cpp src/data/jsonvalue.h
    src/data/jsonio.cpp src/data/jsonio.h
    src/data/datafile.cpp src/data/datafile.h
    src/data/datastore.cpp src/data/datastore.h
    src/data/savescheduler.cpp src/data/savescheduler.h
    src/data/undostack.cpp src/data/undostack.h
    src/data/backup.cpp src/data/backup.h
    src/data/nativeformat.cpp src/data/nativeformat.h
    src/data/lockfile.cpp src/data/lockfile.h
    src/data/electronimport.cpp src/data/electronimport.h
    src/data/planmerge.cpp src/data/planmerge.h
    src/data/exif.cpp src/data/exif.h
    src/data/photolib.cpp src/data/photolib.h
    src/data/trdata.h
    src/platform/atomicfile.cpp src/platform/atomicfile.h
)
target_include_directories(tp_data PUBLIC src/data src/platform)
target_link_libraries(tp_data PUBLIC Qt6::Core)
set_target_properties(tp_data PROPERTIES AUTOMOC ON)

# 照片的图片处理（0.7.0）：缩略图缓存、旋转。要读写图片，所以单独一个库链接 Qt Gui；程序和单元测试共用。
# WebP 用 Qt Image Formats 的 qwebp 插件（运行时加载；内含 libwebp，BSD-3，授权在阶段 ⑧ 列进"关于"）。
add_library(tp_photo STATIC
    src/data/thumbcache.cpp src/data/thumbcache.h
    src/data/photorotate.cpp src/data/photorotate.h
    src/platform/wicimage.cpp src/platform/wicimage.h
)
target_include_directories(tp_photo PUBLIC src/data src/platform)
target_link_libraries(tp_photo PUBLIC tp_data Qt6::Core Qt6::Gui)
if(WIN32)
    target_link_libraries(tp_photo PUBLIC windowscodecs ole32)   # 0.11.1 HEIC 用 Windows 自带的解码（WIC，系统库）
endif()

# 地图的计算（0.8.0）：投影和相机、昼夜阴影多边形、"已解锁"上色、路线几何。只依赖 Qt Core 和数据层，单元测试 tst_map 共用。
add_library(tp_map STATIC
    src/map/geo.cpp src/map/geo.h
    src/map/night.cpp src/map/night.h
    src/map/unlock.cpp src/map/unlock.h
    src/map/tripdata.cpp src/map/tripdata.h
    src/map/markers.cpp src/map/markers.h
    src/map/roads.cpp src/map/roads.h
    src/map/plans.cpp src/map/plans.h
    src/map/planpage.cpp src/map/planpage.h
    src/map/planmoney.cpp src/map/planmoney.h
    src/map/planweather.cpp src/map/planweather.h
    src/map/plankit.cpp src/map/plankit.h
    src/map/plancal.cpp src/map/plancal.h
    src/map/photomap.cpp src/map/photomap.h
    src/map/suncalc.cpp src/map/suncalc.h
    src/map/planedit.cpp src/map/planedit.h
    src/map/globegeom.cpp src/map/globegeom.h
    src/map/globeroutes.cpp src/map/globeroutes.h
)
target_include_directories(tp_map PUBLIC src/map src/data)
target_link_libraries(tp_map PUBLIC tp_data Qt6::Core)

# 联网（0.8.4）：统一请求通道（白名单、User-Agent、超时、同一主机的最小间隔、离线开关）和批量算道路路线。
# Qt Network（LGPL-3，技术选型.md §4）；单元测试 tst_net 用本机的假服务器测。
add_library(tp_net STATIC
    src/services/netclient.cpp src/services/netclient.h
    src/services/roadrouter.cpp src/services/roadrouter.h
    src/services/tilecache.cpp src/services/tilecache.h
    src/services/tileserver.cpp src/services/tileserver.h
    src/services/geocoder.cpp src/services/geocoder.h
)
target_include_directories(tp_net PUBLIC src/services src/map src/data src/platform)
target_link_libraries(tp_net PUBLIC tp_map tp_data Qt6::Core Qt6::Network)
set_target_properties(tp_net PROPERTIES AUTOMOC ON)

# 程序的非界面部分（0.4.0）：设置、数据文件夹、翻译词典、日志、路径；0.5.1 加出错报告、上次崩溃的记号、卡住检测的计数；0.5.2 加崩溃异常码的判断。
# 只依赖 Qt Core，单元测试共用。
add_library(tp_core STATIC
    src/app/settings.cpp src/app/settings.h
    src/app/errorreporter.cpp src/app/errorreporter.h src/app/errhash.h src/app/crashcodes.h
    src/app/crashinfo.cpp src/app/crashinfo.h
    src/app/hangdetector.h
    src/app/datafolder.cpp src/app/datafolder.h
    src/app/translator.cpp src/app/translator.h
    src/app/logger.cpp src/app/logger.h
    src/app/commands.cpp src/app/commands.h
    src/app/navhistory.cpp src/app/navhistory.h
    src/app/perf.cpp src/app/perf.h
    src/platform/paths.cpp src/platform/paths.h
)
target_include_directories(tp_core PUBLIC src/app src/platform)
target_link_libraries(tp_core PUBLIC tp_data Qt6::Core)
set_target_properties(tp_core PROPERTIES AUTOMOC ON)

# AI 助手（0.13.0）：服务商表、拼请求、SSE 解析、算钱、凭据管理器、流式请求。Qt Network，不加新库；单元测试 tst_ai
add_library(tp_ai STATIC
    src/ai/aiproviders.cpp src/ai/aiproviders.h
    src/ai/aistream.cpp src/ai/aistream.h
    src/ai/aiclient.cpp src/ai/aiclient.h
    src/ai/aiprompt.cpp src/ai/aiprompt.h
    src/ai/aitools.cpp src/ai/aitools.h
    src/platform/credentials.cpp src/platform/credentials.h
)
target_include_directories(tp_ai PUBLIC src/ai src/platform)
target_link_libraries(tp_ai PUBLIC tp_core tp_map Qt6::Core Qt6::Network)   # 0.13.1 aitools 改计划用 tp_map 的 planedit
if(WIN32)
    target_link_libraries(tp_ai PUBLIC advapi32)   # CredWriteW / CredReadW / CredDeleteW（系统库）
endif()
set_target_properties(tp_ai PROPERTIES AUTOMOC ON)

# 0.14.1 miniz 3.1.2（MIT，原样放在 src/third_party/miniz，来源见那里的 说明.md）：.trip 的 zip 打包 / 解包
# 按 C++ 编（miniz 支持；项目只开了 CXX，中途 enable_language(C) 在开发目录重新配置时失败）
set_source_files_properties(src/third_party/miniz/miniz.c PROPERTIES LANGUAGE CXX)
add_library(tp_miniz STATIC src/third_party/miniz/miniz.c src/third_party/miniz/miniz.h)
target_include_directories(tp_miniz PUBLIC src/third_party/miniz)
target_compile_definitions(tp_miniz PUBLIC MINIZ_NO_ZLIB_COMPATIBLE_NAMES)
if(MSVC)
    target_compile_options(tp_miniz PRIVATE /W0)   # 第三方原样代码，不看它的警告
endif()

# 导出（0.14.0）：行程单 / 紧急信息卡（QPdfWriter + QPainter，都在 Qt Gui 里，不加新模块）、.ics。单元测试 tst_export 共用
# 0.14.1：.trip 计划文件、分享版文件夹（trippack）
add_library(tp_export STATIC
    src/export/itinerary.cpp src/export/itinerary.h
    src/export/ics.cpp src/export/ics.h
    src/export/trippack.cpp src/export/trippack.h
)
target_include_directories(tp_export PUBLIC src/export)
target_link_libraries(tp_export PUBLIC tp_map tp_core tp_miniz Qt6::Core Qt6::Gui)

qt_add_executable(TravelPlanner WIN32
    src/app/main.cpp
    src/app/devtools.cpp src/app/devtools.h
    src/app/appcontroller.cpp src/app/appcontroller.h
    src/app/windowstate.cpp src/app/windowstate.h
    src/app/tpapplication.cpp src/app/tpapplication.h
    src/app/photocontroller.cpp src/app/photocontroller.h
    src/app/albummodel.cpp src/app/albummodel.h
    src/app/plancontroller.cpp src/app/plancontroller.h
    src/app/planpagecontroller.cpp src/app/planpagecontroller.h
    src/app/calendarcontroller.cpp src/app/calendarcontroller.h
    src/app/journalcontroller.cpp src/app/journalcontroller.h
    src/app/ratescontroller.cpp src/app/ratescontroller.h
    src/app/netcontroller.cpp src/app/netcontroller.h
    src/app/aicontroller.cpp src/app/aicontroller.h
    src/app/exportcontroller.cpp src/app/exportcontroller.h
    src/app/tripcontroller.cpp src/app/tripcontroller.h
    src/app/weathercontroller.cpp src/app/weathercontroller.h
    src/app/planeditor.cpp src/app/planeditor.h
    src/app/locator.cpp src/app/locator.h
    src/app/thumbprovider.cpp src/app/thumbprovider.h
    src/app/framestats.cpp src/app/framestats.h
    src/app/selftest.cpp src/app/selftest.h src/app/selftest_util.cpp src/app/selftest_util.h
    src/app/selftest_map.cpp src/app/selftest_routes.cpp src/app/selftest_unlock.cpp src/app/selftest_plans.cpp src/app/selftest_shell.cpp src/app/selftest_pages.cpp src/app/selftest_planpage.cpp src/app/selftest_money.cpp src/app/selftest_weather.cpp src/app/selftest_kit.cpp src/app/selftest_photos_tab.cpp src/app/selftest_library.cpp src/app/selftest_audit.cpp src/app/selftest_photomode.cpp src/app/selftest_calendar.cpp src/app/selftest_editor.cpp src/app/selftest_globe.cpp src/app/selftest_perf.cpp src/app/selftest_welcome.cpp src/app/selftest_ai.cpp src/app/selftest_export.cpp src/app/selftest_trip.cpp src/app/selftest_v11.cpp src/app/selftest_ailive.cpp src/app/selftest_v12.cpp
    src/platform/crashhandler.cpp src/platform/crashhandler.h
    src/platform/nativedialog.cpp src/platform/nativedialog.h
    src/platform/watchdog.cpp src/platform/watchdog.h
    src/platform/graphicsbackend.cpp src/platform/graphicsbackend.h
    src/platform/procmem.cpp src/platform/procmem.h
    src/platform/singleinstance.cpp src/platform/singleinstance.h
    src/platform/tray.cpp src/platform/tray.h
    src/platform/fullscreen.cpp src/platform/fullscreen.h
    src/map/mapcontroller.cpp src/map/mapcontroller.h src/map/mapcontroller_p.h
    src/map/mapcontroller_camera.cpp src/map/mapcontroller_lines.cpp src/map/mapcontroller_unlock.cpp src/map/mapcontroller_edit.cpp src/map/mapcontroller_globe.cpp
    src/map/markerlayer.cpp src/map/markerlayer.h
    src/map/photolayer.cpp src/map/photolayer.h
    src/map/markerimages.cpp src/map/markerimages.h
    src/map/msicons.h
    src/map/sun.h
    src/map/globetiles.cpp src/map/globetiles.h
    src/map/globeraster.cpp src/map/globeraster.h
    src/map/globeitem.cpp src/map/globeitem.h src/map/globeops.h
    src/map/flatvehicle.cpp src/map/flatvehicle.h
    src/map/glasssurface.cpp src/map/glasssurface.h
    src/map/skyitem.cpp src/map/skyitem.h
)
target_include_directories(TravelPlanner PRIVATE
    "${CMAKE_BINARY_DIR}/generated" src/app src/platform src/map src/services src/ai src/export)
if(EXISTS "${TP_VULKAN_HEADERS}/vulkan/vulkan.h")
    target_include_directories(TravelPlanner SYSTEM PRIVATE "${TP_VULKAN_HEADERS}")
endif()
# 截图 / 内存测试默认读 <源码>/testdata/data.js；开发版（非 Release）带开发用快捷键（原样保存一次）
target_compile_definitions(TravelPlanner PRIVATE TP_SOURCE_DIR="${CMAKE_SOURCE_DIR}" $<$<NOT:$<CONFIG:Release>>:TP_DEV_TOOLS>)

set_source_files_properties(qml/theme/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE QT_RESOURCE_ALIAS Theme.qml)
set_source_files_properties(qml/theme/Fmt.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE QT_RESOURCE_ALIAS Fmt.qml)
foreach(f TpAnim TpMove TpSpring FocusRing TpCard TpDone GlassPanel TpButton ModalLayer ChoiceCard ChoiceDialog MessageDialog TextInputDialog ExtChangedCard ErrorCard Banner Toast Segmented
          TpSelect TpCheck PhotoImportReview PhotoCleanupDialog MapMarkers PhotoMarkers AlbumView PhotoAssignDialog MapTip RoadBar LayerBubbles UnlockPanel
          LinkText StatusFilterPill FilterMenu NowCard PlanSheet
          SplitBud TopBar AvatarMenu PlanListPanel LibraryView WishMonthBar WishDialog CommandPalette RoundButton TpIcon
          PlanEditor DateField TpField StylePopover MarkerShape ColorWheel EditOverlay DrawerGrip
          AiPanel ExportDialog SchedList RouteSketch RoundIcon MenuRow Lightbox PhotosTab MdEditor CalPlanCard CalPicker EmptyArt EmptyState Tour TpTip PeoplePane RowAct RowMenu BarHead)
    set_source_files_properties(qml/components/${f}.qml PROPERTIES QT_RESOURCE_ALIAS ${f}.qml)
endforeach()
foreach(f SettingsPanel SettingsCard SettingsRow BackupSection AboutSection CalendarPage JournalPage LedgerPage PlanPage WelcomePage)
    set_source_files_properties(qml/pages/${f}.qml PROPERTIES QT_RESOURCE_ALIAS ${f}.qml)
endforeach()
qt_add_qml_module(TravelPlanner
    URI TPApp
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/FlatMap.qml
        qml/Pill.qml
        qml/theme/Theme.qml
        qml/theme/Fmt.qml
        qml/components/TpAnim.qml
        qml/components/TpMove.qml
        qml/components/TpSpring.qml
        qml/components/FocusRing.qml
        qml/components/TpCard.qml
        qml/components/TpDone.qml
        qml/components/GlassPanel.qml
        qml/components/TpButton.qml
        qml/components/ModalLayer.qml
        qml/components/ChoiceCard.qml
        qml/components/ChoiceDialog.qml
        qml/components/MessageDialog.qml
        qml/components/TextInputDialog.qml
        qml/components/ExtChangedCard.qml
        qml/components/ErrorCard.qml
        qml/components/Banner.qml
        qml/components/Toast.qml
        qml/components/Tour.qml
        qml/components/TpTip.qml
        qml/components/BarHead.qml
        qml/components/PeoplePane.qml
        qml/components/Segmented.qml
        qml/components/TpSelect.qml
        qml/components/TpCheck.qml
        qml/components/PhotoImportReview.qml
        qml/components/PhotoCleanupDialog.qml
        qml/components/MapMarkers.qml
        qml/components/MapTip.qml
        qml/components/RoadBar.qml
        qml/components/LayerBubbles.qml
        qml/components/UnlockPanel.qml
        qml/components/LinkText.qml
        qml/components/StatusFilterPill.qml
        qml/components/FilterMenu.qml
        qml/components/NowCard.qml
        qml/components/PlanSheet.qml
        qml/components/SchedList.qml
        qml/components/BookingsTab.qml
        qml/components/BudgetTab.qml
        qml/components/RowAct.qml
        qml/components/RowMenu.qml
        qml/components/BookingDialog.qml
        qml/components/ExpenseDialog.qml
        qml/components/PeopleDialog.qml
        qml/components/TpChip.qml
        qml/components/CheckCard.qml
        qml/components/PackingTab.qml
        qml/components/EmergencyDialog.qml
        qml/components/DocDialog.qml
        qml/components/JournalTab.qml
        qml/components/GuideTab.qml
        qml/components/GuideBlocks.qml
        qml/components/GuideLinkDialog.qml
        qml/components/PhotoPickDialog.qml
        qml/components/Lightbox.qml
        qml/components/PhotosTab.qml
        qml/components/CatBar.qml
        qml/components/RouteSketch.qml
        qml/components/RoundIcon.qml
        qml/components/MenuRow.qml
        qml/components/SplitBud.qml
        qml/components/TopBar.qml
        qml/components/AvatarMenu.qml
        qml/components/PlanListPanel.qml
        qml/components/LibraryView.qml
        qml/components/PhotoMarkers.qml
        qml/components/AlbumView.qml
        qml/components/PhotoAssignDialog.qml
        qml/components/WishMonthBar.qml
        qml/components/WishDialog.qml
        qml/components/CommandPalette.qml
        qml/components/RoundButton.qml
        qml/components/AiPanel.qml
        qml/components/AiKeyGuide.qml
        qml/components/ExportDialog.qml
        qml/components/TripFlow.qml
        qml/components/AiProposal.qml
        qml/components/TpIcon.qml
        qml/components/PlanEditor.qml
        qml/components/DateField.qml
        qml/components/TpField.qml
        qml/components/StylePopover.qml
        qml/components/MarkerShape.qml
        qml/components/ColorWheel.qml
        qml/components/EditOverlay.qml
        qml/components/DrawerGrip.qml
        qml/components/MdEditor.qml
        qml/components/CalPlanCard.qml
        qml/components/CalPicker.qml
        qml/components/EmptyArt.qml
        qml/components/EmptyState.qml
        qml/components/CalSeasonView.qml
        qml/pages/SettingsPanel.qml
        qml/pages/SettingsCard.qml
        qml/pages/SettingsRow.qml
        qml/pages/BackupSection.qml
        qml/pages/AboutSection.qml
        qml/pages/CalendarPage.qml
        qml/pages/JournalPage.qml
        qml/pages/LedgerPage.qml
        qml/pages/AiPlanPage.qml
        qml/pages/WelcomePage.qml
        qml/pages/PlanPage.qml
)

# 界面翻译词典（0.4.0）：i18n/tr/*.txt 原样打包进资源 :/i18n/tr/…，运行时由 DictTranslator 读取（不生成 .ts / .qm）
file(GLOB_RECURSE TP_TR_FILES RELATIVE "${CMAKE_SOURCE_DIR}" CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/i18n/tr/*.txt")
qt_add_resources(TravelPlanner "tp_i18n" PREFIX "/" FILES ${TP_TR_FILES})
# 新数据的模板（0.12.7，D-19）：Electron 版的示例计划和季节表，PO 自己的项目
set(TP_TEMPLATE_FILES assets/templates/sample-plan.json assets/templates/sample-plan-en.json assets/templates/season-guide.json)
qt_add_resources(TravelPlanner "tp_templates" PREFIX "/templates" BASE "${CMAKE_SOURCE_DIR}/assets/templates" FILES ${TP_TEMPLATE_FILES})
# 更新日志（0.12.5，设置 → 关于）
qt_add_resources(TravelPlanner "tp_changelog" PREFIX "/" FILES CHANGELOG.md)
# 地图标记的图标字体（0.8.2，Material Symbols Rounded 子集，Apache 2.0；授权文本在同一个文件夹）
qt_add_resources(TravelPlanner "tp_fonts" PREFIX "/fonts" BASE "${CMAKE_SOURCE_DIR}/assets/fonts" FILES assets/fonts/MaterialSymbolsRounded.ttf)

# 着色器：编译时用 qsb 生成 .qsb（GLSL / HLSL / SPIR-V 各一份），放进资源 :/shaders/
qt_add_shaders(TravelPlanner "tp_shaders"
    PREFIX "/"
    FILES
        shaders/globe.vert
        shaders/globe.frag
)
# 地球的瓦片（0.9.0）用纹理数组（sampler2DArray）：GLSL 只出 300 es / 330（老的 100 es / 120 没有纹理数组）。
# 0.9.3：天空（sky.frag）用 uint 运算算星星、城市灯光用 textureGrad，也放这一组。
# OpenGL 后端要 3.3 以上（maplibre 本来就要；多出几个版本让 QRhi 按驱动的版本挑）；HLSL / MSL 照默认
qt_add_shaders(TravelPlanner "tp_shaders_globe"
    PREFIX "/"
    GLSL "300es,330,410,440"
    FILES
        shaders/glass.vert
        shaders/glass.frag
        shaders/globetile.vert
        shaders/globetile.frag
        shaders/sky.vert
        shaders/sky.frag
        shaders/globeline.vert
        shaders/globeline.frag
)

# QRhi（rhi/qrhi.h）属于 Qt Gui 的半公开 API，要链接 GuiPrivate（仍是 Qt Gui 模块本身，LGPL-3）
# 0.8.9：Qt Positioning（我的位置，LGPL-3，技术选型.md §4）；Windows 上用 Windows 定位服务的插件（windeployqt 自动带上）
target_link_libraries(TravelPlanner PRIVATE tp_core tp_ai tp_export tp_data tp_photo tp_map tp_net Qt6::Core Qt6::Gui Qt6::GuiPrivate Qt6::Quick Qt6::Network Qt6::Positioning)
if(WIN32)
    target_link_libraries(TravelPlanner PRIVATE psapi shell32 user32)
    # 0.8.0：进程代码页 UTF-8（maplibre 读本地样式文件用窄字符路径），见 src/app/app.manifest；和 Qt 生成的 manifest 合并
    target_sources(TravelPlanner PRIVATE src/app/app.manifest src/app/app.rc)   # 0.14.2：exe 图标
endif()

set_target_properties(TravelPlanner PROPERTIES
    OUTPUT_NAME "TravelPlanner"
    WIN32_EXECUTABLE ON
    QT_QML_ROOT_PATH "${CMAKE_SOURCE_DIR}/qml"
    # 配置时 Qt 的 qmlimportscanner 从目标所在目录（项目根目录）往下扫所有 .qml，QT_QML_ROOT_PATH 管不到这一步
    # （Qt6QmlMacros.cmake _qt_internal_scan_qml_imports 固定传 -rootPath <目标源码目录>）。
    # 它的 -exclude 只按文件夹名比较、只跳过那一层（qtdeclarative 6.12 tools/qmlimportscanner/main.cpp isExcluded），
    # 而且每个文件夹会以 "名字" 和 "名字/." 各遍历一次，所以要同时排除 examples 和 "."：
    # 跳过 third_party\maplibre-native-qt\src\docs\examples 里的 snippets_*.qml（文档片段，不是完整的 QML，
    # 会报 "Syntax error"；不影响结果，0.5.1 去掉）。排除 "." 只少扫项目根目录本身（那里没有 .qml）；
    # 云端实测扫出来的导入列表和不排除时完全一样。我们自己的 qml\ 里没有叫 examples 的文件夹。
    QT_QML_IMPORT_SCANNER_EXTRA_ARGS "-exclude;examples;-exclude;."
)

# 运行需要的数据文件：省界（Natural Earth，公有领域）放到 exe 旁边的 assets/
# 程序图标（托盘、窗口）放到 assets/app/
# 0.9.3：地球的夜晚城市灯光（NASA Black Marble 2016 灰度，8000×4000 JPEG，约 1.3 MB）放到 assets/nightlights/
add_custom_command(TARGET TravelPlanner POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/admin" "$<TARGET_FILE_DIR:TravelPlanner>/assets/admin"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/app" "$<TARGET_FILE_DIR:TravelPlanner>/assets/app"
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/nightlights" "$<TARGET_FILE_DIR:TravelPlanner>/assets/nightlights"
    # 0.12.3：字体目录整个复制：思源宋体两个文件（NotoSerifCJKsc-Regular.otf、-Bold.otf）放进去就带上，没放也不会让编译失败
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/assets/fonts" "$<TARGET_FILE_DIR:TravelPlanner>/assets/fonts"
    # 0.12.5：授权全文放进程序目录的 LICENSES（设置 → 关于能打开）。Qt 自带的那份在打包时加（package.ps1）
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_SOURCE_DIR}/third_party/maplibre-native-qt/LICENSES" "$<TARGET_FILE_DIR:TravelPlanner>/LICENSES/maplibre-native-qt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/assets/fonts/LICENSE-Material-Symbols.txt" "${CMAKE_SOURCE_DIR}/assets/fonts/LICENSE-NotoSerifCJK.txt" "${CMAKE_SOURCE_DIR}/assets/licenses/LICENSE-suncalc.txt" "${CMAKE_SOURCE_DIR}/assets/licenses/LICENSE-miniz.txt" "${CMAKE_SOURCE_DIR}/assets/licenses/README.txt" "$<TARGET_FILE_DIR:TravelPlanner>/LICENSES"
    VERBATIM)
# 安装目录也要带上（OPTIONAL：文件不存在时跳过，不报错）
install(FILES "${CMAKE_SOURCE_DIR}/assets/fonts/NotoSerifCJKsc-Regular.otf" "${CMAKE_SOURCE_DIR}/assets/fonts/NotoSerifCJKsc-Bold.otf" OPTIONAL DESTINATION assets/fonts)
# 0.12.11：字体的授权跟着装（原来只装了字体文件）。正式打包走 package.ps1，那边一直带着
install(FILES "${CMAKE_SOURCE_DIR}/assets/fonts/LICENSE-NotoSerifCJK.txt" "${CMAKE_SOURCE_DIR}/assets/fonts/LICENSE-Material-Symbols.txt" DESTINATION LICENSES)

# 以后各模块（src/services …）按阶段加进来；
# 逻辑单元测试放在 tests/，用 Qt Test（qtbase 自带，LGPL-3；只用于测试，不打包），打开 TP_BUILD_TESTS 时编译。
option(TP_BUILD_TESTS "编译 tests/ 下的单元测试" OFF)
if(TP_BUILD_TESTS AND EXISTS "${CMAKE_SOURCE_DIR}/tests/CMakeLists.txt")
    enable_testing()
    add_subdirectory(tests)
endif()
