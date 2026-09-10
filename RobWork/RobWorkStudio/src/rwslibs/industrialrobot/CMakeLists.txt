# =====================================================================
# 工业机械臂设计软件产品代码（industrialrobot）
# 依据: doc/industrial-robot-design/units/*.md（各单元详设——接口与任务权威）
#       与 development-task-breakdown.md §2/§5（任务编排与构建约定）；
#       DETAILED-DESIGN.md 仅作 20 单元详设索引（WP-03-T01 修订：骨架期对
#       其 §1.2/§1.5 的引用为悬空引用，改指各单元卡与任务拆分）
# 状态: core 已随 WP-03-T01（≙CORE-T01）落地为真实 STATIC 库＋gtest 测试
#       目标（见 core/CMakeLists.txt）；其余 19 个单元仍为 INTERFACE 占位，
#       源码随各自单元任务卡落地，落地时升级为真实库（CMake 目标名不变）
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

# core 自 WP-03-T01 起移出 INTERFACE 占位循环（下方 add_subdirectory(core)
# 定义真实 STATIC 库）；本列表仅剩尚未落地的单元。
set(IRD_MODULES
    testkit project evidence runtime policy execution diagnostics
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

# 依赖红线（WP-01-T01 边界门禁承载；core 侧配置期断言已随 WP-03-T01 落在
# core/CMakeLists.txt，全仓依赖图门禁随 ird_gates 落地启用）：
#   业务目标互链 / 跨单元私有头 / Widgets 进入计算核心 / RobWork 名称前缀拼接
#   —— 任一命中即构建失败（development-task-breakdown.md §5.1）

# 提醒：插件目标（sdurws_ird_<unit>_plugin/_test/_contract_test/_worker 等）
# 随对应单元任务卡逐个登记，不在骨架中预建空目标（不建占位，总体详设 §1.5 原则）。
