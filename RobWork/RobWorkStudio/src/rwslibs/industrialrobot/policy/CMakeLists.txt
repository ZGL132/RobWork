# =====================================================================
# policy 单元（策略单元）构建脚本
#
# 设计依据：
#   - units/policy.md §3.4（命名空间/目标/CMake 集成）、§3.2（依赖：core＋标准库
#     ＋RobWork 基线——含 sdurw_proximity 直链，R-5 唯一产品例外）、§12 POL-T01 行
#   - development-task-breakdown.md §5.1（双模式/目标命名）、§5.5（gtest 定稿）、
#     §4.5（R-5 例外登记册：policy 产品实现＝proximity 直链唯一许可，生效行）
#   - 需求 ARC-05（策略单一权威）、CON-06；红线 R-3（零 Qt）、CR-02/CR-04 陷阱
#
# 本文件随 POL-T01（≙WP-07-T01，任务契约 tasks/foundation/POL-T01.json）落地：
#   1. 骨架期 INTERFACE 占位升级为 STATIC 库（目标名 sdurws_ird_policy 不变，
#      别名 RWS::ird::policy 不变）；
#   2. 注册 gtest 测试目标 sdurws_ird_policy_test（红线扫描：零 Qt/依赖图仅
#      core＋RobWork 边/R-5 例外钉住）；
#   3. 首个翻译单元为占位 Policy.cpp（真实接口随 POL-T02+ 落地）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制；CR-07）----
find_package(GTest CONFIG REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC ----
add_library(sdurws_ird_policy STATIC src/Policy.cpp)

target_compile_features(sdurws_ird_policy PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_policy PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(sdurws_ird_policy PUBLIC /utf-8)
endif()

# 公共头根：仅 policy/include（R-2 纪律同 core/testkit）。
target_include_directories(sdurws_ird_policy PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# ---- RobWork 基线库链接（集成模式；冒烟模式占位实现零 rw 引用各自成立）----
# 判定＝TARGET sdurw_kinematics（框架库目标只在集成配置树存在——runtime RT-T01
# 同款探测；详见其 CMakeLists 顶层顺序说明）。
# ⚠ R-5（DTB §4.5 例外登记册生效行）：policy 产品实现是 proximity 直链的**唯一**
# 许可方（碰撞评估唯一实现——ARC-05）；其他单元直链 proximity 即构建失败。
set(IRL_BASELINE_LIBS "")
if(TARGET sdurw_kinematics)
    target_link_libraries(sdurws_ird_policy PUBLIC
        sdurw_math
        sdurw_kinematics
        sdurw_models
        sdurw_proximity)
    set(IRL_BASELINE_LIBS "sdurw_math, sdurw_kinematics, sdurw_models, sdurw_proximity")
endif()

target_link_libraries(sdurws_ird_policy PUBLIC sdurws_ird_core)

if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::policy ALIAS sdurws_ird_policy)
endif()

# ---- 测试目标 sdurws_ird_policy_test（随 POL-T01 登记）----
enable_testing()

add_executable(sdurws_ird_policy_test
    test/BuildRedLineTest.cpp)
target_compile_features(sdurws_ird_policy_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_policy_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_policy_test PRIVATE /utf-8)
endif()

target_link_libraries(sdurws_ird_policy_test PRIVATE
    sdurws_ird_policy
    GTest::gtest_main)

# 源码树位置与 L1 链接清单注入（红线扫描用例输入；清单＝R-5 例外钉住——运行期
# 断言"清单与登记一致"，集成四库、冒烟空串）。
# 注：值内用逗号分隔——MSVC 预处理器定义按分号拆分（RT-T01 实测留痕）。
target_compile_definitions(sdurws_ird_policy_test PRIVATE
    IRD_POLICY_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    IRD_POLICY_BASELINE_LIBS="${IRL_BASELINE_LIBS}")

add_custom_command(TARGET sdurws_ird_policy_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_policy_test>
            $<TARGET_FILE_DIR:sdurws_ird_policy_test>
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

ird_add_gtest(sdurws_ird_policy_test)

# ---- 依赖红线守卫（配置期断言）----
# policy 唯一 industrialrobot 单元依赖＝core；RobWork 基线＝math/kinematics/models/
# proximity（R-5 例外清单）；零 Qt；产品库本体零 gtest（D-06）。
get_target_property(_ird_pol_link_libs sdurws_ird_policy LINK_LIBRARIES)
get_target_property(_ird_pol_iface_libs sdurws_ird_policy INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_pol_link_libs _ird_pol_iface_libs)
    if(_lib MATCHES "^sdurws_ird_" AND NOT _lib STREQUAL "sdurws_ird_core"
                                      AND NOT _lib STREQUAL "sdurws_ird_policy")
        message(FATAL_ERROR
            "policy 依赖红线：sdurws_ird_policy 仅可链接 core＋RobWork 基线，"
            "发现单元目标 ${_lib}（units/policy.md §3.2、ARCH §3.5）")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "policy 依赖红线（R-3/NFR-MNT-01）：policy 禁止链接 Qt，发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "policy 依赖红线（D-06）：policy 库本体不链接 gtest，发现 ${_lib}")
    endif()
endforeach()
