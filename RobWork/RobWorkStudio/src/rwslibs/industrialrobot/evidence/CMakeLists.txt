# =====================================================================
# evidence 单元（评估证据内核）构建脚本
#
# 设计依据：
#   - units/evidence.md §3.4（命名空间/目标/CMake 集成）、§3.2（编译依赖：仅
#     core＋标准库——零 Qt、零 rw、零 industrialrobot 其他单元）、§12 EV-T01 行
#   - development-task-breakdown.md §5.1（双模式/目标命名）、§5.5（gtest 定稿）
#   - 需求 CON-01/05、EVI-01；红线 R-3（零 Qt）、R-1/R-2
#
# 本文件随 EV-T01（≙WP-05-T01，任务契约 tasks/foundation/EV-T01.json）落地：
#   1. 骨架期 INTERFACE 占位升级为 STATIC 库（目标名 sdurws_ird_evidence 不变，
#      别名 RWS::ird::evidence 不变，公共 include 根 evidence/include/ 不变）；
#   2. 注册 gtest 测试目标 sdurws_ird_evidence_test（红线扫描：零 Qt/依赖图仅
#      core 边/公共头路径布局）；
#   3. 首个翻译单元为占位 Evidence.cpp（快照/切片等真实接口随 EV-T02+ 落地）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制；CR-07）----
find_package(GTest CONFIG REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC ----
# src/Evidence.cpp＝EV-T01 落位锚点 TU；src/Errors.cpp＝EV-T02 错误契约
# （token/建议码两张全表＋异常轨——units/evidence.md §3.1 组成表）。
add_library(sdurws_ird_evidence STATIC
    src/Evidence.cpp
    src/Errors.cpp)

target_compile_features(sdurws_ird_evidence PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_evidence PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(sdurws_ird_evidence PUBLIC /utf-8)
endif()

# 公共头根：仅 evidence/include（R-2 纪律同 core/testkit）。
target_include_directories(sdurws_ird_evidence PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 编译依赖：仅 core（evidence.md §3.2 唯一单元边；无 rw 基线库——纯计算内核）。
target_link_libraries(sdurws_ird_evidence PUBLIC sdurws_ird_core)

if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::evidence ALIAS sdurws_ird_evidence)
endif()

# ---- 测试目标 sdurws_ird_evidence_test（随 EV-T01 登记）----
enable_testing()

add_executable(sdurws_ird_evidence_test
    test/BuildRedLineTest.cpp
    test/ErrorsTest.cpp              # EV-T02：EV-ERR 全表（token/建议码逐值）＋异常轨
    test/DependencyTest.cpp)         # EV-T02：EV-DEP 键语法/声明闭包/条目语法正反例
target_compile_features(sdurws_ird_evidence_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_evidence_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_evidence_test PRIVATE /utf-8)
endif()

# 测试 main 自持＋链接被测库（公共头可 include＋库可链接的双验证）。
target_link_libraries(sdurws_ird_evidence_test PRIVATE
    sdurws_ird_evidence
    GTest::gtest_main)

# 源码树位置注入（红线扫描用例输入——core/testkit/runtime 同款）。
target_compile_definitions(sdurws_ird_evidence_test PRIVATE
    IRD_EVIDENCE_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

add_custom_command(TARGET sdurws_ird_evidence_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_evidence_test>
            $<TARGET_FILE_DIR:sdurws_ird_evidence_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
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

ird_add_gtest(sdurws_ird_evidence_test)

# ---- 依赖红线守卫（配置期断言）----
# evidence 唯一单元依赖＝core（ARCH §3.5）；零 Qt；产品目标不链 gtest（D-06——
# 断言宏只在消费方 _test 目标展开）。两面读取（PRIVATE＋PUBLIC）防漏。
get_target_property(_ird_ev_link_libs sdurws_ird_evidence LINK_LIBRARIES)
get_target_property(_ird_ev_iface_libs sdurws_ird_evidence INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_ev_link_libs _ird_ev_iface_libs)
    if(_lib MATCHES "^sdurws_ird_" AND NOT _lib STREQUAL "sdurws_ird_core"
                                      AND NOT _lib STREQUAL "sdurws_ird_evidence")
        message(FATAL_ERROR
            "evidence 依赖红线：sdurws_ird_evidence 仅可链接 core＋标准库，"
            "发现单元目标 ${_lib}（units/evidence.md §3.2、ARCH §3.5）")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "evidence 依赖红线（R-3/NFR-MNT-01）：evidence 禁止链接 Qt，发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "evidence 依赖红线（D-06/T-2 同款）：evidence 库本体不链接 gtest，发现 ${_lib}")
    endif()
endforeach()
