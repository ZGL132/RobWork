# =====================================================================
# testkit 单元（测试支撑单元）构建脚本
#
# 设计依据：
#   - units/testkit.md §3.3（命名空间/目标/CMake 集成）、§3.2（依赖：
#     仅 core＋C++17 标准库，零 Qt〔T-2 纪律〕、零产品单元、零 gtest
#     库级链接）、§3.6（数据根定位）、§4.6（TestPaths 三态解析）
#   - development-task-breakdown.md §5.1（双模式/目标命名/C++17 约定）、
#     §5.5（gtest 接入定稿：vcpkg 安装＋find_package(GTest CONFIG REQUIRED)，
#     失败即停不回落——CR-07；版本首装 gtest:x64-windows@1.18.0 已随
#     WP-03-T01/CORE-T01 执行并登记于本文 §4.4，本任务为 testkit 侧接入）
#   - 需求 NFR-MNT-01（零 Qt）、NFR-COR-01（黄金数据载体）；红线 T-1/T-2
#     （testkit.md §2.4，DTB §4.5 例外登记册"无例外"）；O-21（P-TK-4：
#     全仓依赖门禁建成前的 testkit 侧自律守卫）
#
# 本文件随 TK-T01（≙WP-02-T01，任务契约 tasks/foundation/TK-T01.json）落地：
#   1. 骨架期的 INTERFACE 占位升级为 STATIC 库（目标名 sdurws_ird_testkit
#      不变，别名 RWS::ird::testkit 不变，公共 include 根不变）；
#   2. 注册 gtest 测试目标 sdurws_ird_testkit_test（数据根三态用例＋
#      TK-BUILD 前两用例）；
#   3. 落位数据根变量 SDURWS_IRD_TESTDATA_DIR（编译默认 industrialrobot/
#      testdata；实体目录随首个数据集任务 TK-T03 建立——allowedFiles 不含
#      testdata/**，偏差按 DTB §5.4 登记于单元卡变更记录 v0.3）；
#   4. 配置期依赖红线守卫（T-2 的 testkit 侧断言；T-1 由 core 侧守卫＋
#      TkBuild 用例双向钉住）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制；CR-07）----
# REQUIRED：找不到即配置失败（失败即停，本任务转 blocked，不静默跳过、
# 不回落第二渠道）。不消费框架 USE_gtest/RW::gtest（从未生成）、不源码
# vendor。vcpkg 的 gtest:x64-windows 为动态库（运行期 DLL 拷贝见下方）。
find_package(GTest CONFIG REQUIRED)

# ---- 数据根变量落位（testkit.md §3.6/§4.6）----
# 编译期默认＝industrialrobot/testdata（正斜杠绝对路径）。CACHE PATH：
# 集成/冒烟两模式与外部覆写（-DSDURWS_IRD_TESTDATA_DIR=...）共用同一变量；
# 运行期环境变量 SDURWS_IRD_TESTDATA_DIR（同名）优先于本默认（两级解析
# 见 TestPaths.cpp）。目录实体随 TK-T03 首个数据集建立——当前不存在时
# goldenDataRoot() 抛 env-unavailable（三态之一，行为由用例钉住）。
set(SDURWS_IRD_TESTDATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../testdata"
    CACHE PATH "industrialrobot 黄金数据集根目录（运行期可被同名环境变量覆写；实体目录随首个数据集任务建立）")

# ---- 产品库目标：INTERFACE 占位 → STATIC（目标名不变，DTB §5.1）----
# TestPaths.cpp 是首个翻译单元（STATIC 库至少需要一个）；JsonLite（TK-T02）
# 等后续实现文件并列加入 src/。
add_library(sdurws_ird_testkit STATIC
    src/TestPaths.cpp
    src/JsonLite.cpp
    src/Fault.cpp
    src/Dataset.cpp
    src/ToleranceProfile.cpp)

# C++17（testkit.md §1.4：显式 cxx_std_17，与 core D-01 同口径；不用 C++20）。
# PUBLIC 传播：testkit 公共头使用 std::filesystem/std::optional 等 C++17
# 设施（后续 ToleranceProfile 等消费 core 值类型同样要求），消费者编译
# testkit 头也必须至少 C++17。
target_compile_features(sdurws_ird_testkit PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_testkit PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：UTF-8（无 BOM）＋中文注释（AGENTS.md §2）——MSVC 默认按系统
# 代码页（GBK）解读无 BOM 源文件会产生乱码 token（C2001），显式 /utf-8。
# PUBLIC 传播理由与 core 相同：公共头同为 UTF-8，消费者需要同一解读。
if(MSVC)
    target_compile_options(sdurws_ird_testkit PUBLIC /utf-8)
endif()

# 公共头根：仅 testkit/include（内含 sdurws/ird/testkit/）。R-2 同款纪律：
# 私有实现头不入 include/（实现细节头只能放 src/ 并以相对路径被包含）。
target_include_directories(sdurws_ird_testkit PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 链接 core（testkit.md §3.2/§3.3：唯一允许的单元依赖；PUBLIC——testkit
# 公共头将消费 core 值类型（TK-T04 容差档案承载 core::Tolerance 等），
# 消费者经 testkit 传递取得 core 头）。统一用真名 sdurws_ird_core：
# 集成模式别名 RWS::ird::core 与真名并存，冒烟模式仅真名（上级
# add_subdirectory(core) 在两种模式下都执行，真名恒可解析——与 core 测试
# 目标的链接写法一致）。
target_link_libraries(sdurws_ird_testkit PUBLIC sdurws_ird_core)

# 别名（集成模式）：供各单元 _test 目标以 RWS::ird::testkit 消费；冒烟
# 模式无别名（与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::testkit ALIAS sdurws_ird_testkit)
endif()

# 编译期默认数据根注入（PRIVATE：仅实现文件 TestPaths.cpp 消费；宏缺失时
# 该文件以 #error fail-fast，杜绝静默使用猜测路径）。
target_compile_definitions(sdurws_ird_testkit PRIVATE
    IRD_TESTDATA_DEFAULT_DIR="${SDURWS_IRD_TESTDATA_DIR}")

# ---- 测试目标 sdurws_ird_testkit_test（随 TK-T01 登记，不预建空目标）----
# 冒烟模式独立配置时无上层 enable_testing，在此开启（幂等）。
enable_testing()

add_executable(sdurws_ird_testkit_test
    test/TestPathsTest.cpp
    test/JsonLiteTest.cpp
    test/FaultTest.cpp
    test/DatasetTest.cpp
    test/ToleranceProfileTest.cpp
    test/BuildRedLineTest.cpp)
# testdata 根注入（TK-TOL 示例档案定位——testdata 与 testkit 平铺）。
target_compile_definitions(sdurws_ird_testkit_test PRIVATE
    IRD_TESTDATA_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/../testdata")
target_compile_features(sdurws_ird_testkit_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_testkit_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源同为 UTF-8＋中文注释；显式自设不依赖被测目标传播（目标应独立可读）。
if(MSVC)
    target_compile_options(sdurws_ird_testkit_test PRIVATE /utf-8)
endif()

# 测试 main 自持（DTB §5.5）：GTest::gtest_main 提供入口（RecordListener
# 适配随 TK-T10 落地）。链接产品库 sdurws_ird_testkit 同时验证"公共头可
# 正常 include＋库可链接"（core 侧同一验证形态）。
target_link_libraries(sdurws_ird_testkit_test PRIVATE
    sdurws_ird_testkit
    GTest::gtest_main)

# 源码树位置与编译默认注入（运行期用例输入）：
#   IRD_TESTKIT_UNIT_ROOT —— TkBuild 用例扫描 industrialrobot 全部单元
#     产品面（T-1：产品目标零 testkit 边）；
#   IRD_TESTDATA_DEFAULT_DIR —— 三态用例核对编译默认的路径形态与存在性
#     （与库目标注入同一变量值）。
target_compile_definitions(sdurws_ird_testkit_test PRIVATE
    IRD_TESTKIT_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    IRD_TESTDATA_DEFAULT_DIR="${SDURWS_IRD_TESTDATA_DIR}")

# vcpkg 的 gtest:x64-windows 是动态库（gtest.dll/gtest_main.dll）：拷贝
# 运行期 DLL 到测试可执行文件输出目录，保证不经 PATH 配置即可直接运行
# （留痕与验收都要直接执行该可执行文件；CORE-T01 同款先例）。
add_custom_command(TARGET sdurws_ird_testkit_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_testkit_test>
            $<TARGET_FILE_DIR:sdurws_ird_testkit_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：注册测试与 XML 报告目标（DTB §5.5）----
# 宏定义放各单元自己的 CMake（core 是零依赖内核不提供构建工具设施，
# testkit 同理不跨单元共享——NFR-MNT-04）；沿用框架 ADD_RW_GTEST 宏语义：
# add_test ＋ <target>_report（运行测试并落 gtest XML 于本构建目录
# gtest_reports/）。报告目标不进默认构建：跑测试是显式动作。
macro(ird_add_gtest target)
    add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
    add_custom_target(
        ${target}_report
        COMMAND ${CMAKE_COMMAND} -E make_directory
                ${CMAKE_CURRENT_BINARY_DIR}/gtest_reports
        COMMAND $<TARGET_FILE:${target}>
                --gtest_output=xml:${CMAKE_CURRENT_BINARY_DIR}/gtest_reports/${target}.xml
        DEPENDS ${target}
        COMMENT "ird: run ${target} and write gtest XML report")
    set_target_properties(${target}_report PROPERTIES
        EXCLUDE_FROM_ALL 1 EXCLUDE_FROM_DEFAULT_BUILD 1)
endmacro()

ird_add_gtest(sdurws_ird_testkit_test)

# ---- lint 工具（TK-T03 交付：tools/testdata 全量扫描——§4.5 CI 独立运行）----
# 只链 testkit（数据根经 TestPaths 两级解析）；不进产品安装面（T-1 同款纪律——
# 分发红线：testkit 与其工具不随产品分发，§3.6）。
add_executable(sdurws_ird_testdata_lint tools/testdata/main.cpp)
target_compile_features(sdurws_ird_testdata_lint PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_testdata_lint PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_testdata_lint PRIVATE /utf-8)
endif()
target_link_libraries(sdurws_ird_testdata_lint PRIVATE sdurws_ird_testkit)

# ---- 依赖红线守卫（T-2/R-3 的 testkit 侧配置期断言；O-21 自律载体）----
# T-2（testkit.md §2.4）：sdurws_ird_testkit 链接接口只允许 core——出现
# 任何其他 industrialrobot 单元目标、Qt 目标（R-3/NFR-MNT-01）或 gtest
# 目标（D-06：库本体零 gtest，断言宏只在消费方 _test 目标展开）即配置
# 失败。读取 LINK_LIBRARIES（含 PRIVATE 边）与 INTERFACE_LINK_LIBRARIES
# （PUBLIC 边传染消费者）两面；属性未设置时 get_target_property 返回
# "<var>-NOTFOUND" 字面串，不匹配任何模式，不会误报。
# T-1 反方向（产品目标不得链 testkit）由 core 侧守卫＋TkBuild 用例的
# 源码扫描双向钉住；正式全仓依赖图门禁归 WP-01-T01 的 ird_gates。
get_target_property(_ird_tk_link_libs sdurws_ird_testkit LINK_LIBRARIES)
get_target_property(_ird_tk_iface_libs sdurws_ird_testkit INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_tk_link_libs _ird_tk_iface_libs)
    if(_lib MATCHES "^sdurws_ird_" AND NOT _lib STREQUAL "sdurws_ird_core"
                                      AND NOT _lib STREQUAL "sdurws_ird_testkit")
        message(FATAL_ERROR
            "testkit 依赖红线（T-2）：sdurws_ird_testkit 仅可链接 core＋标准库，"
            "发现单元目标 ${_lib}（units/testkit.md §2.4/§3.2）")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "testkit 依赖红线（T-2/R-3/NFR-MNT-01）：testkit 本体禁止链接 "
            "Qt，发现 ${_lib}（units/testkit.md §3.5）")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "testkit 依赖红线（D-06/T-2）：testkit 库本体不链接 gtest——"
            "断言宏仅在消费方 _test 目标的翻译单元展开，发现 ${_lib}")
    endif()
endforeach()
