# =====================================================================
# 工业机械臂设计软件产品代码（industrialrobot）
# 依据: doc/industrial-robot-design/DETAILED-DESIGN.md §1.2/§1.5（20 单元）
#       与 units/*.md（单元详设）；构建约定见 development-task-breakdown.md §5.1
# 状态: WP-01 构建骨架——仅注册目标与头文件路径；源码随各单元任务卡落地，
#       落地时将 INTERFACE 升级为 STATIC/实际类型（CMake 目标名不变）
# =====================================================================

# 独立冒烟模式：允许脱离 RobWorkStudio 单独配置本目录（仅验证目标注册与
# include 路径存在性；无任何框架依赖——见 patches/PATCHES.md/WP-01-T02）
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    cmake_minimum_required(VERSION 3.16)
    project(industrialrobot_smoke LANGUAGES NONE)
    message(STATUS "industrialrobot: standalone smoke mode（独立冒烟，不代表集成构建）")
endif()

set(IRD_MODULES
    core testkit project evidence runtime policy execution diagnostics
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

# 依赖红线（WP-01-T01 边界门禁承载；本骨架暂无可链接目标，门禁随首批源码启用）：
#   业务目标互链 / 跨单元私有头 / Widgets 进入计算核心 / RobWork 名称前缀拼接
#   —— 任一命中即构建失败（development-task-breakdown.md §5.1）

# 提醒：插件目标（sdurws_ird_<unit>_plugin/_test/_contract_test/_worker 等）
# 随对应单元任务卡逐个登记，不在骨架中预建空目标（不建占位，总体详设 §1.5 原则）。
