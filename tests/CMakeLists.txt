# 逻辑单元测试（Qt Test）。编译：cmake -DTP_BUILD_TESTS=ON …；运行：ctest 或直接运行 tst_data / tst_app。
# 真实数据往返测试：环境变量 TP_ROUNDTRIP_DIRS 指定要检查的文件夹（多个用 ; 分隔），只读，不写入。
find_package(Qt6 REQUIRED COMPONENTS Test Gui Quick Network)

# 数据层（0.3.0）
qt_add_executable(tst_data tst_data.cpp)
target_link_libraries(tst_data PRIVATE tp_data Qt6::Test)
target_compile_definitions(tst_data PRIVATE TP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(tst_data PROPERTIES WIN32_EXECUTABLE OFF AUTOMOC ON)
add_test(NAME tst_data COMMAND tst_data)

# 程序层（0.4.0）：设置、数据文件夹、窗口位置、翻译词典和缺英文检查、日志
qt_add_executable(tst_app tst_app.cpp ../src/app/windowstate.cpp ../src/app/windowstate.h)
target_link_libraries(tst_app PRIVATE tp_core Qt6::Test Qt6::Gui Qt6::Quick)
target_compile_definitions(tst_app PRIVATE TP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(tst_app PROPERTIES WIN32_EXECUTABLE OFF AUTOMOC ON)
add_test(NAME tst_app COMMAND tst_app)
qt_add_resources(tst_app "tst_app_templates" PREFIX "/templates" BASE "${CMAKE_SOURCE_DIR}/assets/templates"
    FILES ${CMAKE_SOURCE_DIR}/assets/templates/sample-plan.json ${CMAKE_SOURCE_DIR}/assets/templates/sample-plan-en.json ${CMAKE_SOURCE_DIR}/assets/templates/season-guide.json)

# 照片（0.7.0）：EXIF、缩略图、旋转、导入、清理。测试照片在 tests/photos/（tools/claude-kit/gen_photo_fixtures.py 生成）
qt_add_executable(tst_photo tst_photo.cpp)
target_link_libraries(tst_photo PRIVATE tp_photo Qt6::Test Qt6::Gui)
target_compile_definitions(tst_photo PRIVATE TP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(tst_photo PROPERTIES WIN32_EXECUTABLE OFF AUTOMOC ON)
add_test(NAME tst_photo COMMAND tst_photo)

# 地图的计算（0.8.0）：投影和相机、按范围缩放、最小缩放、飞行、昼夜阴影、"已解锁"上色（和 Electron 对照）、路线几何
qt_add_executable(tst_map tst_map.cpp)
target_link_libraries(tst_map PRIVATE tp_map Qt6::Test)
target_compile_definitions(tst_map PRIVATE TP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(tst_map PROPERTIES WIN32_EXECUTABLE OFF AUTOMOC ON)
add_test(NAME tst_map COMMAND tst_map)

# 联网（0.8.4）：统一请求通道（本机假服务器：User-Agent、白名单、超时、同一主机的最小间隔、离线开关）、批量算道路路线
qt_add_executable(tst_net tst_net.cpp)
target_link_libraries(tst_net PRIVATE tp_net Qt6::Test)
target_compile_definitions(tst_net PRIVATE TP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(tst_net PROPERTIES WIN32_EXECUTABLE OFF AUTOMOC ON)
add_test(NAME tst_net COMMAND tst_net)

# AI（0.13.0）：SSE 解析（两种接法）、用量和算钱、三档对应表、拼请求、钥匙校验、mdLite
qt_add_executable(tst_ai tst_ai.cpp)
target_link_libraries(tst_ai PRIVATE tp_ai Qt6::Test)
set_target_properties(tst_ai PROPERTIES WIN32_EXECUTABLE OFF AUTOMOC ON)
add_test(NAME tst_ai COMMAND tst_ai)

# 0.14.0 导出：.ics 和 Electron 对照、分页、长图上限、PDF
qt_add_executable(tst_export tst_export.cpp)
target_link_libraries(tst_export PRIVATE tp_export Qt6::Test Qt6::Gui)
target_compile_definitions(tst_export PRIVATE TP_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
set_target_properties(tst_export PROPERTIES WIN32_EXECUTABLE OFF AUTOMOC ON)
add_test(NAME tst_export COMMAND tst_export)
