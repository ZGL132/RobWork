# =====================================================================
# core 单元（L2 平台内核）构建脚本
#
# 设计依据：
#   - units/core.md §3.3（命名空间/目标/CMake 集成）、§3.2（依赖：仅 C++17
#     标准库＋sdurw_math，零 Qt、零 industrialrobot 其他单元）
#   - development-task-breakdown.md §5.1（双模式/目标命名/C++17 约定）、
#     §5.5（gtest 接入定稿：vcpkg 安装＋find_package(GTest CONFIG REQUIRED)）
#   - 需求 NFR-MNT-01（计算内核零 Qt）、ARC-02（架构可维护）
#
# 本文件随 WP-03-T01（≙CORE-T01，任务契约 tasks/foundation/CORE-T01.json）落地：
#   1. 骨架期的 INTERFACE 占位升级为 STATIC 库（目标名 sdurws_ird_core 不变，
#      别名 RWS::ird::core 不变，公共 include 根 core/include/ 不变）；
#   2. 注册 gtest 测试目标 sdurws_ird_core_test 与 UT-BUILD 红线用例；
#   3. 配置期依赖红线守卫（R-1/R-2/R-3/T-1 的 core 侧断言；全仓正式门禁
#      归 WP-01-T01 的 ird_gates，本守卫不替代它）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制）----
# gtest 经 vcpkg 安装（gtest:x64-windows，经典模式）；REQUIRED 意味着找不到
# 即配置失败（失败即停，不静默跳过测试目标）。不消费框架的 USE_gtest/
# RW::gtest（从未生成），也不源码 vendor——两份 gtest 混用会破坏 ODR。
find_package(GTest CONFIG REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC（目标名不变，DTB §5.1）----
add_library(sdurws_ird_core STATIC
    src/Core.cpp
    src/Identity.cpp
    src/Digest.cpp
    src/Provenance.cpp
    src/Units.cpp
    src/Compare.cpp
    src/Evaluation.cpp
    src/DiagData.cpp
    src/Events.cpp)

# C++17（core.md 决策 D-01）：逐目标显式设定，不设全局标准——基线 RobWork
# 为 C++11，混链可行性正是本任务双模式构建要验证的内容（P-ENV-1 消账）。
# PUBLIC 传播：core 公共头使用 std::optional/std::variant 等 C++17 设施，
# 消费者编译 core 头也必须至少 C++17。
target_compile_features(sdurws_ird_core PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_core PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：本单元源文件全部为 UTF-8（无 BOM）且含中文注释（AGENTS.md §2
# 注释规范）。MSVC 默认按系统代码页（GBK）解读无 BOM 源文件，会把 UTF-8
# 中文误读成乱码 token（曾出现 C2001"常量中有换行符"），必须显式 /utf-8。
# PUBLIC 传播：core 公共头同为 UTF-8，消费者编译含 core 头的翻译单元也需要。
if(MSVC)
    target_compile_options(sdurws_ird_core PUBLIC /utf-8)
endif()

# 公共头根：仅 core/include（内部含 sdurws/ird/core/）。
# R-2 红线：私有实现头不得置于 include/——本目录是 core 唯一对外的头暴露位，
# 实现细节头（若有）只能放 src/ 并以相对路径被实现文件包含。
target_include_directories(sdurws_ird_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 链接 L1 数学库 sdurw_math（rw::math；core.md §3.2 唯一允许的框架依赖）。
# 仅集成模式（被 RobWorkStudio 构建树包含）下该目标存在；独立冒烟模式没有
# 框架目标，此时不链接——冒烟只验证本单元目标注册与 C++17 编译，占位实现
# 也不引用 rw::math 头，两模式各自成立。
if(TARGET sdurw_math)
    target_link_libraries(sdurws_ird_core PUBLIC sdurw_math)
endif()

# 别名（集成模式）：供其余单元以 RWS::ird::core 消费；冒烟模式无别名
# （与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::core ALIAS sdurws_ird_core)
endif()

# ---- 测试目标 sdurws_ird_core_test（随 CORE-T01 登记，不预建于骨架）----
# 冒烟模式独立配置时无上层 enable_testing，在此开启（幂等）；集成模式下
# 若顶层已开启亦无副作用。
enable_testing()

add_executable(sdurws_ird_core_test
    test/BuildRedLineTest.cpp
    test/IdentityTest.cpp
    test/DigestTest.cpp
    test/ConventionValueSemanticsTest.cpp
    test/ProvenanceTest.cpp
    test/UnitsTest.cpp
    test/CompareTest.cpp
    test/EvaluationTest.cpp
    test/DiagDataTest.cpp
    test/EventsTest.cpp)
target_compile_features(sdurws_ird_core_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_core_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（/utf-8 说明见产品目标处；gtest 头为 ASCII
# 不受影响）。core 的 PUBLIC /utf-8 经链接传播亦可覆盖，显式再设一次以自洽
# ——测试目标应独立可读，不依赖被测目标的编译选项细节。
if(MSVC)
    target_compile_options(sdurws_ird_core_test PRIVATE /utf-8)
endif()

# 测试 main 自持（DTB §5.5）：GTest::gtest_main 提供入口；测试链接产品库
# sdurws_ird_core 同时验证"公共头可被正常 include＋库可链接"。
target_link_libraries(sdurws_ird_core_test PRIVATE
    sdurws_ird_core
    GTest::gtest_main)

# 源码树位置注入：UT-BUILD 用例在运行期扫描 core 源码核对红线（core.md §8）。
# 经编译定义传绝对路径，避免测试对工作目录的依赖。
target_compile_definitions(sdurws_ird_core_test PRIVATE
    IRD_CORE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

# vcpkg 的 gtest:x64-windows 是动态库（gtest.dll/gtest_main.dll）：把测试
# 可执行文件的运行期 DLL 拷到其输出目录，保证不经 PATH 配置即可直接运行
# （留痕与验收都要直接执行该可执行文件）。TARGET_RUNTIME_DLLS 需 CMake ≥3.21，
# 本仓库工具链为 VS2022 自带的更新版本，满足。
add_custom_command(TARGET sdurws_ird_core_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_core_test>
            $<TARGET_FILE_DIR:sdurws_ird_core_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：注册测试与 XML 报告目标（沿用框架 ADD_RW_GTEST 宏语义）----
# 框架原宏见 RobWork/RobWork/gtest/CMakeLists.txt：add_test ＋ <target>_report
# （运行测试并落 gtest XML）。本宏按 DTB §5.5 在各单元 CMake 内定义，不做
# 跨单元共享（core 是零依赖内核，不提供构建工具设施——NFR-MNT-04）。
# 报告输出于构建树本目录 gtest_reports/<target>.xml；任务留痕时拷贝入
# doc/industrial-robot-design/traceability/（AGENTS.md §4.2 验证留痕）。
# 报告目标不进默认构建（EXCLUDE_FROM_ALL）：跑测试是显式动作，
# 与框架"不随 ordinary build 执行测试"的约定一致。
macro(ird_add_gtest target)
    add_test(NAME ${target} COMMAND $<TARGET_FILE:${target}>)
    add_custom_target(
        ${target}_report
        COMMAND ${CMAKE_COMMAND} -E make_directory
                ${CMAKE_CURRENT_BINARY_DIR}/gtest_reports
        COMMAND $<TARGET_FILE:${target}>
                --gtest_output=xml:${CMAKE_CURRENT_BINARY_DIR}/gtest_reports/${target}.xml
        DEPENDS ${target}
        COMMENT "ird: run ${target} and write gtest XML report"
    )
    set_target_properties(${target}_report PROPERTIES
        EXCLUDE_FROM_ALL 1 EXCLUDE_FROM_DEFAULT_BUILD 1)
endmacro()

ird_add_gtest(sdurws_ird_core_test)

# ---- 依赖红线守卫（R-1/R-2/R-3/T-1 的 core 侧配置期断言）----
# core 是"任何层都可依赖"的零依赖内核（core.md §1.3）：链接接口出现任何
# 其他 industrialrobot 单元目标（R-1/R-2）或 Qt 目标（R-3，NFR-MNT-01）
# 即配置失败。同时读取 LINK_LIBRARIES（含 PRIVATE 边——PRIVATE 的 Qt 同样
# 违反零 Qt）与 INTERFACE_LINK_LIBRARIES（PUBLIC 边会传染给全部消费者）。
# 注意：属性未设置时 get_target_property 返回 "<var>-NOTFOUND" 字面串，
# 不匹配下列任何模式，不会误报。正式全仓门禁（依赖图白名单）归 WP-01-T01。
get_target_property(_ird_core_link_libs sdurws_ird_core LINK_LIBRARIES)
get_target_property(_ird_core_iface_libs sdurws_ird_core INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_core_link_libs _ird_core_iface_libs)
    if(_lib MATCHES "^sdurws_ird_" AND NOT _lib STREQUAL "sdurws_ird_core")
        message(FATAL_ERROR
            "core 依赖红线（R-1/R-2/T-1）：sdurws_ird_core 不得链接 industrialrobot "
            "其他单元目标，发现 ${_lib}（core 是零依赖内核，units/core.md §3.2）")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "core 依赖红线（R-3/NFR-MNT-01）：L2 计算内核禁止链接 Qt，"
            "发现 ${_lib}（ARCHITECTURE §3.2）")
    endif()
endforeach()
