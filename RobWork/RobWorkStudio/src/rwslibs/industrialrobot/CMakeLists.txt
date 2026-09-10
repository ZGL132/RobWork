# =====================================================================
# 工业机械臂设计软件产品代码（industrialrobot）
# 依据: doc/industrial-robot-design/units/*.md（各单元详设——接口与任务权威）
#       与 development-task-breakdown.md §2/§5（任务编排与构建约定）；
#       DETAILED-DESIGN.md 仅作 20 单元详设索引（WP-03-T01 修订：骨架期对
#       其 §1.2/§1.5 的引用为悬空引用，改指各单元卡与任务拆分）
# 状态: core 已随 WP-03-T01（≙CORE-T01）、testkit 已随 WP-02-T01（≙TK-T01）
#       落地为真实 STATIC 库＋gtest 测试目标（见各自 CMakeLists.txt）；
#       其余 18 个单元仍为 INTERFACE 占位，源码随各自单元任务卡落地，
#       落地时升级为真实库（CMake 目标名不变）
# =====================================================================

# 独立冒烟模式：允许脱离 RobWorkStudio 单独配置本目录（验证目标注册、include
# 路径与 C++17 编译；无任何框架依赖——见 patches/PATCHES.md/WP-01-T02）。
# LANGUAGES 由 NONE 改为 CXX（WP-03-T01）：core 自本任务起含真实编译目标
# （STATIC 库＋测试），冒烟也必须编译；其余单元仍为 INTERFACE 占位、无源码。
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(industrialrobot_smoke LANGUAGES CXX)
    message(STATUS "industrialrobot: standalone smoke mode（独立冒烟，不代表集成构建）")
endif()

# core 与 testkit 分别自 WP-03-T01/WP-02-T01 起移出 INTERFACE 占位循环
# （下方 add_subdirectory 定义真实 STATIC 库）；本列表仅剩尚未落地的单元。
set(IRD_MODULES
    project execution diagnostics
    ui io reporting
    modeling requirements kinematics trajectory dynamics drivetrain
    selection optimization workflow
)

foreach(ird_m ${IRD_MODULES})
    add_library(sdurws_ird_${ird_m} INTERFACE)
    if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        add_library(RWS::ird::${ird_m} ALIAS sdurws_ird_${ird_m})
    endif()
    target_include_directories(sdurws_ird_${ird_m} INTERFACE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/${ird_m}/include>)
endforeach()

# core（WP-03-T01≙CORE-T01 落地）：骨架 INTERFACE 占位升级为 STATIC 库
# （C++17、集成模式链 sdurw_math），并注册 gtest 测试目标 sdurws_ird_core_test
# 与依赖红线守卫（R-1/R-2/R-3/T-1 core 侧）——目标名/别名/include 根不变，
# 实现与守卫细节见 core/CMakeLists.txt。
add_subdirectory(core)

# PLACEHOLDER-RT：INTERFACE 占位升级为 STATIC 库（C++17、
# 链 core——testkit 是其唯一允许的单元依赖），并注册 gtest 测试目标
# sdurws_ird_testkit_test（数据根三态用例＋TK-BUILD 红线用例）与 T-2
# 配置期守卫（依赖红线 testkit 侧自律，O-21）——细节见 testkit/CMakeLists.txt。
# 次序约束：必须在 core 之后（testkit 链接 sdurws_ird_core 目标）。
add_subdirectory(testkit)
# runtime（RT-T01≙WP-06-T01 落地）：INTERFACE 占位升级为 STATIC（C++17、链 core＋
# L1 基线库〔P-RT-3：kinematics/models/sdurwsim——目标存在性实测后链接〕），并注册
# gtest 测试目标 sdurws_ird_runtime_test（红线扫描＋L1 链接清单钉住）——细节见
# runtime/CMakeLists.txt。次序约束：必须在 core 之后（链接 sdurws_ird_core）。
add_subdirectory(runtime)
# evidence（EV-T01≙WP-05-T01 落地）：INTERFACE 占位升级为 STATIC（C++17、仅链
# core——evidence 是纯计算内核零 rw），并注册 gtest 测试目标 sdurws_ird_evidence_test
# （红线扫描）与配置期守卫——细节见 evidence/CMakeLists.txt。次序约束：必须在
# core 之后（链接 sdurws_ird_core）。
add_subdirectory(evidence)
# policy（POL-T01≙WP-07-T01 落地）：INTERFACE 占位升级为 STATIC（C++17、链 core＋
# RobWork 基线 math/kinematics/models/proximity——proximity 直链＝R-5 唯一产品例外，
# DTB §4.5 生效行），并注册 gtest 测试目标 sdurws_ird_policy_test（红线扫描＋R-5
# 钉住）与配置期守卫——细节见 policy/CMakeLists.txt。次序约束：在 core 之后。
add_subdirectory(policy)

# 依赖红线门禁（WP-01-T01 落地）：ird_gates 自定义目标＝构建期硬门禁
# （ARCH §3.2 SA-10"红线即构建门禁"）。检查项与命中码见 cmake/ird_gates.cmake
# 文件头；边白名单与例外登记数据见 cmake/ird_gates_whitelist.cmake（数据面；
# 人工登记册＝development-task-breakdown.md §4.5，两处必须一致）。运行方式：
#   cmake --build build --config Release --target ird_gates
# 目标自带引擎自测（干净夹具必须通过＋7 类植入违例必须检出——DTB §2.2
# 验收第 5 条），任一命中即目标失败。日常构建不自动挂接该目标（避免每次
# 构建全树扫描），CI/一键门禁随 WP-01-T02 挂接。
add_custom_target(ird_gates
    COMMAND "${CMAKE_COMMAND}"
        "-DIRD_ROOT=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DIRD_SELFTEST_DIR=${CMAKE_BINARY_DIR}/ird_gates_selftest"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ird_gates.cmake"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "ird_gates：依赖红线与补丁门禁（R-1~R-5/T-1/T-2/SUB/SA-02＋引擎自测）"
    VERBATIM)

# 提醒：插件目标（sdurws_ird_<unit>_plugin/_test/_contract_test/_worker 等）
# 随对应单元任务卡逐个登记，不在骨架中预建空目标（不建占位，总体详设 §1.5 原则）。
