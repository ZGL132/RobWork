# =====================================================================
# runtime 单元（运动学编译内核）构建脚本
#
# 设计依据：
#   - units/runtime.md §3.4（命名空间/目标/CMake 集成）、§3.2（依赖：core＋
#     L1 RobWork 基线库〔rw kinematics/models＋rwsim dynamics——P-RT-3〕，零 Qt、
#     零 industrialrobot 其他单元）、§12 RT-T01 行
#   - development-task-breakdown.md §5.1（双模式/目标命名）、§5.5（gtest 定稿）、
#     §4.6（L1 基线库目标名实测表——本任务实测核对回登）
#   - 需求 ARC-03（规范真值唯一）、MDL-22；红线 R-3（零 Qt）、R-1/R-2
#
# 本文件随 RT-T01（≙WP-06-T01，任务契约 tasks/foundation/RT-T01.json）落地：
#   1. 骨架期 INTERFACE 占位升级为 STATIC 库（目标名 sdurws_ird_runtime 不变，
#      别名 RWS::ird::runtime 不变，公共 include 根 runtime/include/ 不变）；
#   2. 注册 gtest 测试目标 sdurws_ird_runtime_test（构建红线扫描＋L1 链接清单
#      钉住——P-RT-3 消账输入）；
#   3. 首个翻译单元为占位 Runtime.cpp（与 core/Core.cpp 同例——真实接口随
#      RT-T02+ 落地，本任务只做构建落位）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制；CR-07）----
find_package(GTest CONFIG REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC（RT-T01）→ 契约源随 RT-T02 起增列 ----
add_library(sdurws_ird_runtime STATIC
    src/Runtime.cpp   # 单元锚点（RT-T01 占位，保留）
    src/Errors.cpp)   # RT-T02：错误码全表（token/稳定码/分类）＋RuntimeError

target_compile_features(sdurws_ird_runtime PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_runtime PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

if(MSVC)
    target_compile_options(sdurws_ird_runtime PUBLIC /utf-8)
endif()

# 公共头根：仅 runtime/include（R-2 纪律同 core/testkit）。
target_include_directories(sdurws_ird_runtime PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# ---- L1 基线库链接（P-RT-3：rw kinematics/models＋rwsim dynamics）----
# sdurw_math 经 sdurws_ird_core 的 PUBLIC 边传递获得（core 是唯一 industrialrobot
# 编译依赖——ARCH §3.5）。
# 集成/冒烟判定＝TARGET sdurw_kinematics（框架库目标只在集成配置树存在——
# CORE-T01 同款探测；顶层目录比较在单元子目录不可用）。集成模式：kinematics/
# models 直接 TARGET 链接；sdurwsim 因顶层顺序（RobWorkStudio 先于 RobWorkSim
# 配置）尚无目标，按名称链接（CMake 生成期解析）。冒烟模式：三者皆无→不链接
# （占位实现零 rw 引用，两模式各自成立）。
set(IRL_BASELINE_LIBS "")
if(TARGET sdurw_kinematics)
    target_link_libraries(sdurws_ird_runtime PUBLIC
        sdurw_kinematics
        sdurw_models
        sdurwsim)
    set(IRL_BASELINE_LIBS "sdurw_kinematics, sdurw_models, sdurwsim")
endif()

target_link_libraries(sdurws_ird_runtime PUBLIC sdurws_ird_core)

if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::runtime ALIAS sdurws_ird_runtime)
endif()

# ---- 测试目标 sdurws_ird_runtime_test（随 RT-T01 登记）----
enable_testing()

add_executable(sdurws_ird_runtime_test
    test/BuildRedLineTest.cpp   # RT-T01：构建红线扫描＋L1 清单钉住
    test/ErrorsTest.cpp         # RT-T02：RT-ERR 全表（token/稳定码/分类逐值）＋异常轨＋错误载荷
    test/ExpectedTest.cpp)      # RT-T02：Expected 两态语义（§3.4——成功/失败不静默吞错）
target_compile_features(sdurws_ird_runtime_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_runtime_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_runtime_test PRIVATE /utf-8)
endif()

target_link_libraries(sdurws_ird_runtime_test PRIVATE
    sdurws_ird_runtime
    GTest::gtest_main)

# 源码树位置与 L1 链接清单注入（红线扫描用例输入；清单＝P-RT-3 消账输入——
# 运行期断言"清单与声明一致"，集成三库、冒烟空串）。
target_compile_definitions(sdurws_ird_runtime_test PRIVATE
    IRD_RUNTIME_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/.."
    IRD_RUNTIME_BASELINE_LIBS="${IRL_BASELINE_LIBS}")

add_custom_command(TARGET sdurws_ird_runtime_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_runtime_test>
            $<TARGET_FILE_DIR:sdurws_ird_runtime_test>
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

ird_add_gtest(sdurws_ird_runtime_test)

# ---- 依赖红线守卫（T 系/R 系的 runtime 侧配置期断言）----
# runtime 唯一 industrialrobot 编译依赖＝core（ARCH §3.5）；L1 基线库白名单
# ＝P-RT-3 清单（kinematics/models/sdwsim 拼写以实际目标为准——见上方实测循环）。
# 出现任何其他 industrialrobot 单元目标或 Qt 目标即配置失败。
get_target_property(_ird_rt_link_libs sdurws_ird_runtime LINK_LIBRARIES)
get_target_property(_ird_rt_iface_libs sdurws_ird_runtime INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_rt_link_libs _ird_rt_iface_libs)
    if(_lib MATCHES "^sdurws_ird_" AND NOT _lib STREQUAL "sdurws_ird_core"
                                      AND NOT _lib STREQUAL "sdurws_ird_runtime")
        message(FATAL_ERROR
            "runtime 依赖红线：sdurws_ird_runtime 仅可链接 core＋L1 基线库，"
            "发现单元目标 ${_lib}（units/runtime.md §3.2、ARCH §3.5）")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "runtime 依赖红线（R-3/NFR-MNT-01）：runtime 禁止链接 Qt，发现 ${_lib}")
    endif()
endforeach()
