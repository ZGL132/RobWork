# =====================================================================
# 工业机械臂设计软件产品代码（industrialrobot）
# 依据: doc/industrial-robot-design/units/*.md（各单元详设——接口与任务权威）
#       与 development-task-breakdown.md §2/§5（任务编排与构建约定）；
#       DETAILED-DESIGN.md 仅作 20 单元详设索引（WP-03-T01 修订：骨架期对
#       其 §1.2/§1.5 的引用为悬空引用，改指各单元卡与任务拆分）
# 状态: core 已随 WP-03-T01（≙CORE-T01）、testkit 已随 WP-02-T01（≙TK-T01）、
#       runtime/evidence/policy 已随 RT-T01/EV-T01/POL-T01、diagnostics 已随
#       DIAG-T02（≙WP-09-T02）、project 已随 PRJ-T01（≙WP-04-T01）、io 已随
#       IO-T01（≙WP-11-T02）落地为真实 STATIC 库（见各自 CMakeLists.txt；
#       io 落位期无测试目标——随 IO-T06/T07 登记）、execution 已随 EX-T01
#       （≙WP-08-T02）落地为真实 STATIC 库（测试目标 sdurws_ird_execution_
#       test/_contract_test 随落位登记——见 execution/CMakeLists.txt）、ui 已
#       随 WP-10-T02（≙UI-T02）落地为真实 STATIC 库（R-3 唯一 Qt Widgets
#       例外目标——链 core/diagnostics/Qt6 三件套，测试目标 sdurws_ird_ui_
#       test/_contract_test/_gui_test 随落位登记——见 ui/CMakeLists.txt）、
#       reporting 已随 RPT-T01（≙WP-12-T02）落地为真实 STATIC 库（PUBLIC 链
#       core/evidence/diagnostics/project 四条 ARCH §3.5 登记边，测试目标
#       sdurws_ird_reporting_test/_contract_test 随落位登记——见
#       reporting/CMakeLists.txt）、modeling 已随 WP-13-T02 落地为真实
#       STATIC 库（PUBLIC 链 core/diagnostics/project/runtime/policy/io
#       六条卡 §3.2 登记边，测试目标 sdurws_ird_modeling_test/
#       _contract_test 随落位登记——见 modeling/CMakeLists.txt；_plugin
#       随 WP-13-T15）；其余 8 个单元仍为 INTERFACE 占位，源码
#       随各自单元任务卡落地，落地时升级为真实库（CMake 目标名不变）
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
# diagnostics 自 DIAG-T02（≙WP-09-T02）、project 自 PRJ-T01（≙WP-04-T01）、
# io 自 IO-T01（≙WP-11-T02）、execution 自 EX-T01（≙WP-08-T02）、ui 自
# WP-10-T02（≙UI-T02，R-3 唯一 Qt Widgets 例外目标）、reporting 自
# RPT-T01（≙WP-12-T02）、modeling 自 WP-13-T02 起移出（下方
# add_subdirectory）。
set(IRD_MODULES
    requirements kinematics trajectory dynamics drivetrain
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
# diagnostics（DIAG-T02≙WP-09-T02 落位）：INTERFACE 占位升级为 STATIC（C++17、
# PUBLIC 链 core——ARCH §3.5 唯一出边；零 Qt 含 Core——D-01；零 Eigen/rw::math
# 直接包含），并注册 gtest 测试目标 sdurws_ird_diagnostics_test（DT-BUILD 红线
# 扫描）与 sdurws_ird_diagnostics_contract_test（P-DIAG-8 处置：契约测试以桩＋
# 契约夹具验证，不私改 execution 侧链接——双库集成冒烟随 P-EX-8 联动另行登记）
# 与配置期守卫——细节见 diagnostics/CMakeLists.txt。次序约束：必须在 core 之后
# （链接 sdurws_ird_core）；先于其消费方（project/execution/io/ui/reporting——
# ARCH §3.5 入边单元，dtb §1.1：diagnostics 须先于四者实现）。
add_subdirectory(diagnostics)
# project（PRJ-T01≙WP-04-T01 落位）：INTERFACE 占位升级为 STATIC（C++17、
# PUBLIC 链 core——ARCH §3.5 唯一登记编译链接边；diagnostics 边按注入形态
# 不落链接——P-PR-6/P-EX-8 裁决，白名单预登记；零 Qt 含 Core——D-01；零
# io 编译期消费——O-09 处置，阶段 B WP-04-T17/T18 回填；P-PR-1 处置：仅
# 链接不消费 core 头），并注册 gtest 测试目标 sdurws_ird_project_test
# （PRJ-TX-14 前两项源码面扫描）与 sdurws_ird_project_contract_test
# （§3.3 跨单元契约面——落位期为构建图边界契约）与配置期守卫——细节见
# project/CMakeLists.txt。次序约束：必须在 core 之后（链接 sdurws_ird_core）、
# diagnostics 之后（其卡 §3.2 消费方次序约定——project 属 ARCH §3.5 入边
# 单元）。
add_subdirectory(project)
# io（IO-T01≙WP-11-T02 落位）：INTERFACE 占位升级为 STATIC（C++17、PUBLIC
# 链 core＋diagnostics——io.md §3.2/§3.3 仅有的两条登记单元边，ird_gates
# 白名单 "io->core"/"io->diagnostics" 行），并新建 io/CMakeLists.txt（本
# 注册行即其挂载点）；IoFwd/IoError 公共头就位，src/ 空起步＝锚点翻译单元
# Io.cpp（零接口实现预建——NFR-MNT-04）。P-IO-3 处置：ZIP/XML 候选库
# 仅做 vcpkg 可用性验证留痕（traceability/io-pio3-dependency-probe.md），
# 选型冻结权在 WP-11 评审——L1 文件/XML 第三方目标冻结前不登记（不预引入，
# NFR-DEP-03）。本任务不注册测试目标（sdurws_ird_io_test/_contract_test 随
# IO-T06/T07 登记，不预建——io.md §12 IO-T01 禁止项）。零 Qt、零 Widgets
# （NFR-MNT-01＋WP-11-T02 禁止项）。次序约束：必须在 core 与 diagnostics
# 之后（链接两者目标——diagnostics 卡 §3.2 已注明 io 属其消费方）。
add_subdirectory(io)
# execution（EX-T01≙WP-08-T02 落位）：INTERFACE 占位升级为 STATIC（C++17、
# PUBLIC 链 core＋evidence＋project——ARCH §3.5 三条登记编译链接边，本卡
# §3.4 原文；diagnostics 边按注入形态不落链接——P-EX-8 裁决，白名单预登记
# 不构成链接义务；零 Qt 含 Core——D-01；零 runtime/policy 编译边——P-EX-3
# 处置：经本卡 §3.3 最小注入接口消费、policy 经快照 PolicyRef 值传递，不私
# 建表外边），并注册 gtest 测试目标 sdurws_ird_execution_test（EX-BLD-1 前
# 两项源码面扫描＋P-EX-3/O-24 具名自证）与 sdurws_ird_execution_contract_
# test（构建图边界契约——落位期允许的最小构建冒烟用例，业务用例随
# EX-T02~T09 落地，不在骨架预建占位实现）与配置期守卫——细节见
# execution/CMakeLists.txt。src/ 空起步＝锚点翻译单元 Execution.cpp
# （PRJ-T01 同款先例，无接口预建——NFR-MNT-04）。次序约束：必须在 core、
# evidence、project 之后（链接三者目标——ARCH §3.5 登记边）。
add_subdirectory(execution)
# ui（WP-10-T02≙UI-T02 落位）：INTERFACE 占位升级为 STATIC（C++17、PUBLIC
# 链 core＋diagnostics——ARCH §3.5 两条登记边，ird_gates 白名单
# "ui->core"/"ui->diagnostics" 行；PUBLIC 链 Qt6::Core/Gui/Widgets——R-3 唯一
# 例外登记目标，NFR-MNT-01，DTB §4.5"预登记（WP-10-T02 生效）"行），并新建
# ui/CMakeLists.txt（本注册行即其挂载点）；Ui.cpp 锚点翻译单元（空起步＝
# 无接口预建——NFR-MNT-04，公共头随 UI-T03+ 落地），测试目标
# sdurws_ird_ui_test（QCoreApplication 级模型测试，LABELS ird）/
# sdurws_ird_ui_contract_test（构建图边界契约，LABELS ird）/
# sdurws_ird_ui_gui_test（GUI 测试载体，LABELS ird_gui 串行，链
# sdurws_ird_testkit_qt——其启用触发面）随本任务登记。O-31 处置：交付面仅
# 含上述两条表内边与 R-3 例外登记，零指向 project/evidence/execution/policy/
# runtime 的链接或 include（配置期守卫＋ui_test O-31 扫描用例双面钉住）；
# P-UI-5 处置：按 ui.md CF-1 结论（命令注册表类型归 ui 自有头），ui→core
# 边照常建立。本任务不修改 cmake/ird_gates_whitelist.cmake（不在
# allowedFiles）——sdurws_ird_ui 的 Qt 链接在机器面豁免条目回填（
# WP-01-T03）前将产生 R3 命中，登记提交件见
# doc/industrial-robot-design/traceability/wp10-t02-gate-registrations.md。
# 零 testkit（T-1）、零业务域单元（R-1）、零第三方新依赖（NFR-DEP-03）。
# 次序约束：必须在 core 与 diagnostics 之后（链接两者目标——ARCH §3.5
# 登记边，diagnostics 卡 §3.2 已注明 ui 属其消费方）。
add_subdirectory(ui)
# reporting（RPT-T01≙WP-12-T02 落位）：INTERFACE 占位升级为 STATIC（C++17、
# PUBLIC 链 core＋evidence＋diagnostics＋project——ARCH §3.5 四条登记编译
# 链接边，reporting.md §3.4 原文全链，ird_gates 白名单 "reporting->core"/
# "reporting->evidence"/"reporting->diagnostics"/"reporting->project" 行），
# 并新建 reporting/CMakeLists.txt（本注册行即其挂载点）；src/ 空起步＝
# 锚点翻译单元 Reporting.cpp（PRJ-T01/EX-T01 同款先例，无接口预建——
# NFR-MNT-04），测试目标 sdurws_ird_reporting_test（单元内红线扫描）/
# sdurws_ird_reporting_contract_test（构建图边界契约——落位期允许的最小
# 构建冒烟用例，业务用例与 ScriptedSectionProvider/ScriptedResultSource
# 替身随 RPT-T02~T12 落地，不在骨架预建占位实现）随本任务登记。
# P-RPT-1 处置：零 io 编译边（CSV/JSON 写出经注入的 IReportIoFactory——
# §9.5，P-RPT-1/P-IO-1/P-PR-5 合并裁决前维持注入）；P-RPT-2 处置：零
# runtime/execution 编译边（IModelSummaryProvider/ITaskStatusSource 注入
# ——§3.3）；P-RPT-9 处置：四条边仅链接目标不消费上游公共头（各卡 v0.1
# Draft 基线，冻结 diff 后按影响面增量同步留痕）。零 Qt 含 Core（D-01）、
# 零 Eigen/rw::math 直接包含（§1.4）、零 testkit（T-1）、零业务域单元
# （R-1）。次序约束：必须在 core、evidence、diagnostics、project 之后
# （链接四者目标——ARCH §3.5 登记边，diagnostics 卡 §3.2 已注明
# reporting 属其消费方）。
add_subdirectory(reporting)
# modeling（WP-13-T02 落位）：INTERFACE 占位升级为 STATIC（C++17、PUBLIC
# 链 core＋diagnostics＋project＋runtime＋policy＋io——modeling.md §3.2
# 六条登记接口依赖边，属 ARCH §3.5"各业务域单元→L2/L3 公共接口"许可方向
# 的实例化，ird_gates 白名单 "modeling->…" 六行随本任务登记；零 Qt 含
# Core——D-01/NFR-MNT-01，插件目标 sdurws_ird_modeling_plugin 随
# WP-13-T15 落位时方为 Qt 例外载体），并新建 modeling/CMakeLists.txt
# （本注册行即其挂载点）；T02 公共面＝ObjectTypes/Errors/DiagCodes 三头
# （卡 §3.3 表 T02 行），测试目标 sdurws_ird_modeling_test（模型层用例，
# LABELS ird）/sdurws_ird_modeling_contract_test（落位期构建图边界契约，
# LABELS ird）随本任务登记。零其他业务域单元（R-1）、零 testkit（T-1）。
# 次序约束：必须在 core、diagnostics、project、runtime、policy、io 之后
# （链接六者目标——modeling.md §3.2 边表；各被链单元均已在上方
# add_subdirectory）。
add_subdirectory(modeling)

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
