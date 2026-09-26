# =====================================================================
# requirements 单元（业务域：任务需求形式化——Stage 2）构建脚本
#
# 设计依据：
#   - units/requirements.md §3.1（单元组成——计算库 STATIC 零 Qt＋两测试
#     目标；"无 _worker 目标：命令在主进程命令执行线程串行"）、§3.2（目标
#     升级与门禁边登记——WP-14-T02 执行清单：五条接口依赖边/配置期红线
#     守卫自持/_plugin 随 WP-14-T08 落位）、§3.3（公共头布局——T02 行：
#     ObjectTypes/Errors/DiagCodes 三头）、§3.4（线程与确定性总约定——
#     纯函数服务可重入、物理量恒 SI）
#   - development-task-breakdown.md §5.1（双模式/目标命名/C++17 不用
#     C++20）、§5.3（全局禁止项）、§5.5（gtest 接入定稿）
#   - ARCHITECTURE.md §3.3（业务域单元二分结构）、§3.5（依赖表——各业务
#     域单元→L2/L3 公共接口的许可方向；五边为其实例化，登记于
#     cmake/ird_gates_whitelist.cmake 的 "requirements->…" 五行）
#   - 需求 NFR-MNT-01（计算内核零 Qt）、ARC-02（架构可维护）
#   - 任务契约 tasks/foundation/WP-14-T02.json（本文件即其 acceptance 1/4
#     的落地面：INTERFACE 占位→STATIC＋_test/_contract_test 随文件注册）
#
# 本文件随 WP-14-T02 落地（requirements 由上级 INTERFACE 占位升级为真实
# 库）：
#   1. sdurws_ird_requirements STATIC（目标名与别名 RWS::ird::requirements
#      不变，DTB §5.1）；T02 公共面＝ObjectTypes/Errors/DiagCodes 三头＋
#      对应实现翻译单元（§3.3 表 T02 行；RequirementTypes/Codec 等随
#      T03+ 增列）；
#   2. 测试目标 sdurws_ird_requirements_test 与
#      sdurws_ird_requirements_contract_test（自持 ird_add_gtest 宏：
#      add_test＋<target>_report XML——core/io/modeling 同款；ctest
#      LABELS "ird"）；
#   3. 配置期依赖红线守卫（R-1/R-3 的 requirements 侧断言——卡 §3.2；
#      全仓正式门禁归 ird_gates/WP-01-T01，本守卫不替代它）。
#   _plugin 目标（Qt Widgets）不在本任务：卡 §3.2/契约 acceptance 4 明文
#   "随 WP-14-T08 落位"（不预建空目标——总体详设 §1.5 原则）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制；随本任务首个测试目标引入）----
# gtest 经 vcpkg 安装（gtest:x64-windows，经典模式）；REQUIRED＝找不到即
# 配置失败（失败即停，不静默跳过测试目标）。不消费框架的 USE_gtest/RW::
# gtest（从未生成），也不源码 vendor——两份 gtest 混用会破坏 ODR。
# 独立冒烟模式必须带 vcpkg toolchain 参数方可解析（CORE-T01 G-2 口径）。
find_package(GTest CONFIG REQUIRED)

# ---- Qt6 接入（WP-14-T08——插件目标的 Qt 载体面，R-3 唯一例外面）----
# 仅插件目标消费 Qt（卡 §3.2"_plugin 落位（Widgets，仅插件目标链接 Qt）"；
# 计算库 sdurws_ird_requirements 保持零 Qt——R-3/NFR-MNT-01，下方链接块不
# 含任何 Qt 目标）。组件＝Core/Gui/Widgets（Widgets 是面板本体、Gui 因
# ui::CommandDescriptor 的 QKeySequence 经头传染、Core 为 Qt 基面；不取
# Test——本单元无 GUI 测试目标，V-22 GUI 用例仅登记流程，卡 §10.2"本次
# 不启动"）。REQUIRED 同 gtest：找不到即配置失败（不静默跳过插件目标）。
# modeling WP-13-T15 同款先例。
find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC（目标名不变，DTB §5.1）----
# T02 源码集＝三公共头的实现翻译单元（头内 token 常量零运行时成本，无需
# TU；函数面按头分文件——modeling/io 同款布局）：
#   src/ObjectTypes.cpp  ProcessTag(11)/TemplateKind(6)/ArrayKind(4) 词表
#                        转发表（§4.3/§7.1/§7.2）
#   src/Errors.cpp       错误码 token 表＋域错误→稳定码映射数据（§9.6）
#   src/DiagCodes.cpp    REQ-* 码描述符清单＋注册函数（§9.6 T02 行）
# T03 增列（tasks/foundation/WP-14-T03.json，§3.3 表 T03 行）：
#   src/RequirementTypes.cpp / Codec.cpp / Editor.cpp / Services.cpp /
#   Sampling.cpp——值模型/canonical 编解码/编辑器/领域服务/采样定义。
# T04 增列（tasks/foundation/WP-14-T04.json，§3.3 表 T04 行）：
#   src/Import.cpp——CSV/JSON 导入映射＋字段字典＋单位预览＋副本导出
#   （io 边五条登记边中 io 值类型的真实消费面——T02 期"仅链接不消费"
#   的回填，P-RPT-9 先例）。
# T05 增列（tasks/foundation/WP-14-T05.json，§3.3 表 T05 行）：
#   src/Readiness.cpp——R0~R9 分层就绪校验＋ReadinessSummary 投影
#   （evidence 边的真实消费面——evidence::ReadinessSummary ①级数据源，
#   同 P-RPT-9 回填先例）；src/CommandHandlers.cpp——命令处理器族
#   （project 边的真实消费面——ICommandHandler/CommandPlan/HandlerContext）。
# T06 增列（tasks/foundation/WP-14-T06.json，卡 §11 T06 行）：
#   src/OrientationResolution.cpp——姿态规则五规则浅解析（§5.3——
#   确定性参考姿态/方向＋resolution 留痕，AT-23）；src/Capture.cpp——
#   三维拾取/TCP 捕获域侧（§9.8 L-R6/L-R7——确认门写回/REQ-CAPTURE-
#   STATE-STALE 产码消费者/未应用不失效）。
# T07 增列（tasks/foundation/WP-14-T07.json，卡 §11 T07 行）：
#   src/TemplateArray.cpp——工艺模板/镜像/四类阵列/重生成/解除关联
#   （§7.1/§7.2 纯函数演算——REQ-DERIVE-* 派生族产码消费者；编辑器
#   批量入口随 Editor.cpp 同批增列）。
add_library(sdurws_ird_requirements STATIC
    src/ObjectTypes.cpp
    src/Errors.cpp
    src/DiagCodes.cpp
    src/RequirementTypes.cpp
    src/Codec.cpp
    src/Editor.cpp
    src/Services.cpp
    src/Sampling.cpp
    src/Import.cpp
    src/Readiness.cpp
    src/CommandHandlers.cpp
    src/OrientationResolution.cpp
    src/Capture.cpp
    src/TemplateArray.cpp)

# C++17（core.md 决策 D-01 同源）：逐目标显式设定，不设全局标准。
# PUBLIC 传播：requirements 公共头使用 C++17 设施（std::string_view/
# inline constexpr 变量等），消费者编译 requirements 头也必须至少 C++17。
target_compile_features(sdurws_ird_requirements PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_requirements PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：本单元源文件全部为 UTF-8（无 BOM）且含中文注释（AGENTS.md §2
# 注释规范）。MSVC 默认按系统代码页（GBK）解读无 BOM 源文件，会把 UTF-8
# 中文误读成乱码 token（core 落位时曾出现 C2001"常量中有换行符"），必须
# 显式 /utf-8。PUBLIC 传播：公共头同为 UTF-8，消费者编译含 requirements
# 头的翻译单元也需要。
if(MSVC)
    target_compile_options(sdurws_ird_requirements PUBLIC /utf-8)
endif()

# 公共头根：仅 requirements/include（内部含 sdurws/ird/requirements/）。
# R-2 红线：私有实现头不得置于 include/——本目录是 requirements 唯一对外
# 的头暴露位，实现细节头（若有）只能放 src/ 并以相对路径被实现文件包含。
target_include_directories(sdurws_ird_requirements PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 编译依赖：core＋diagnostics＋project＋io＋evidence（卡 §3.2 边表五条
# 接口依赖边——ARCH §3.5"各业务域单元→L2/L3 公共接口"许可方向的实例化，
# ird_gates_whitelist.cmake 的 "requirements->…" 五行同批登记）。
# PUBLIC 形态：DiagCodes.hpp 内嵌 diagnostics 承载的 CodeDescriptor/
# IDiagnosticRegistry 契约类型（其头又内嵌 core::DiagCode——消费方必须随
# 链到达）；core/diagnostics 两边本任务真实消费（DiagCodes.hpp include
# 面），project/io/evidence 三边本任务"仅链接目标不消费上游公共头"
# （P-RPT-9/modeling T02 同款先例——边随落位登记，消费随 T03+ 任务回填：
# project ICommandHandler/CommandPlan→T05、io Csv/Json 值类型→T04、
# evidence ReadinessSummary→T05，五边统一 PUBLIC 保持形态一致）。
# ★ runtime 边不登记（契约 acceptance 2：仅在实际 include 其公共值类型时
# 登记）——T02 三头零 runtime 值类型引用（五 token 全为本单元所有权登记，
# §14.4 第 1 项；不像 modeling 根对象 token 需锚定 runtime 常量），后续
# 任务确需引用 runtime 公共值类型时按卡 §3.2 增登该边。用统一真名
# sdurws_ird_<unit>：集成与独立冒烟两种模式下都可解析（别名 RWS::ird::*
# 仅集成模式存在——core/io 落位时同款取舍）。零 Qt（R-3/NFR-MNT-01——
# _plugin 目标随 WP-14-T08 落位时方为 Qt 例外载体）、零其他业务域单元
# （R-1——requirements 不直链 modeling/kinematics/trajectory/dynamics/
# drivetrain/selection/optimization，跨单元协作走六类端口）、零 testkit
# （T-1——testkit 仅随测试目标消费）。
target_link_libraries(sdurws_ird_requirements PUBLIC
    sdurws_ird_core
    sdurws_ird_diagnostics
    sdurws_ird_project
    sdurws_ird_io
    sdurws_ird_evidence)

# ---- T03 增列：rw 模板头冒烟 include 路径（runtime RT-T03 同款机制）----
# 背景：RequirementTypes.hpp 的位姿/盒/采样字段为 rw::math::Vector3D<double>
# （卡 §4.3 字段表原文）——集成模式下 sdurw_math 经 sdurws_ird_core 的
# PUBLIC 边传递可达；独立冒烟模式无框架目标，rw math/common 头为模板/
# 内联为主的自包含闭包（外部依赖仅 Eigen——经 vcpkg Eigen3 提供），冒烟
# 编译只实例化模板内联代码、不引用框架库符号，故只注入头路径、不链接
# （ird_gates 的 LIB 门禁按链接清单核对——header-only 头可达不构成库链
# 接边，runtime 同款口径）。条件＝TARGET sdurw_kinematics 的集成/冒烟判
# 定惯用式（runtime/core 同源）；实现纪律同 runtime：冒烟可达代码不得调
# 用 rw 外联符号（构造一律逐元素构造函数——本单元源码遵守）。
if(NOT TARGET sdurw_kinematics)
    set(IRD_REQ_RW_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../RobWork/src")
    if(NOT EXISTS "${IRD_REQ_RW_SRC_DIR}/rw/math/Vector3D.hpp")
        message(FATAL_ERROR
            "requirements 冒烟模式：未找到 rw 模板头（期望 ${IRD_REQ_RW_SRC_DIR}"
            "/rw/math/Vector3D.hpp）——WP-14-T03 起 RequirementTypes.hpp 需要"
            " rw::math 头（header-only），请勿移动仓库目录结构")
    endif()
    target_include_directories(sdurws_ird_requirements PUBLIC "${IRD_REQ_RW_SRC_DIR}")
    find_package(Eigen3 CONFIG QUIET)
    if(TARGET Eigen3::Eigen)
        get_target_property(IRD_REQ_EIGEN_INC Eigen3::Eigen INTERFACE_INCLUDE_DIRECTORIES)
        if(NOT IRD_REQ_EIGEN_INC)
            message(FATAL_ERROR
                "requirements 冒烟模式：Eigen3::Eigen 无 INTERFACE_INCLUDE_"
                "DIRECTORIES——vcpkg eigen3 配置包异常（RT-T03 冒烟机制）")
        endif()
        target_include_directories(sdurws_ird_requirements PUBLIC ${IRD_REQ_EIGEN_INC})
    else()
        message(FATAL_ERROR
            "requirements 冒烟模式：未找到 Eigen3（vcpkg eigen3）——rw 数学头"
            "依赖 <Eigen/Core>（RT-T03 冒烟 header-only 机制）")
    endif()
endif()

# 别名（集成模式）：供其余单元以 RWS::ird::requirements 消费；冒烟模式无
# 别名（与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::requirements ALIAS sdurws_ird_requirements)
endif()

# =====================================================================
# 插件界面目标 sdurws_ird_requirements_plugin（WP-14-T08 落位——卡 §3.2
# "T02 预留的插件行兑现"；§9.8 面板信息架构与接线）
# =====================================================================
# 形态＝STATIC（SA-01 静态白名单装配——业务插件非动态加载面，产品交付
# 路径由 L5 装配层链接；与 ui 宿主插件的 MODULE 形态区分：后者是框架
# RobWorkStudioPlugin 派生的开发期验证通道，本目标是正式装配面）。
# AUTOMOC＝仅本目标显式开启（DTB §5.1 v0.17 行口径——Q_OBJECT 面板类
# 所需；集成树全局 CMAKE_AUTOMOC 对既有目标生效与否不影响本显式声明，
# 冒烟模式默认 OFF 下本属性单独生效——两种模式"仅插件目标携带 moc 面"
# 登记一致；modeling WP-13-T15 同款先例）。
# 源码布局＝plugin/ 目录（门禁 R-3 产品面扫描域为 include/**＋src/**，
# 插件面文件不在其中——与计算库零 Qt 红线的文件域隔离；零 Qt 呈现模型
# TU 同目录承载，仅本目标与测试目标编译消费，不入 include/——R-2：插件
# 内部面不跨单元暴露）。
add_library(sdurws_ird_requirements_plugin STATIC
    plugin/PanelTreeModel.cpp            # 需求对象树投影（ACC1——根→四分组→条目，零 Qt）
    plugin/PanelStationModel.cpp         # 工位检查器/五规则显隐表/编辑回填（ACC1/2，零 Qt）
    plugin/PanelRegionModel.cpp          # 区域表/采样双模式/预览几何（ACC1，零 Qt）
    plugin/PanelConditionModel.cpp       # 工况表/检查器/必验预览（ACC1，零 Qt）
    plugin/PanelValidationModel.cpp      # R0~R9 分层/逐项/语义说明（ACC1，零 Qt）
    plugin/PanelCommandCatalog.cpp       # 九条域命令登记/装配描述符/就绪投影/L-R12（ACC4/5）
    plugin/PanelEditFlow.cpp             # L-R2/L-R9 编辑流编排＋两级撤销呈现（ACC2/4）
    plugin/ImportWizardFlow.cpp          # L-R10 导入向导域侧五步流（ACC4）
    plugin/RequirementsPanelWidget.cpp   # 需求域面板 widget（Q_OBJECT——AUTOMOC 载体）
    plugin/RequirementsPanelWidget.hpp   #   （头入源列表——AUTOMOC 扫描 Q_OBJECT 声明）
    plugin/RequirementsUiModule.cpp)     # ui.md §11.2 模块实现＋草稿源（P-REQ-4 冻结面）
set_target_properties(sdurws_ird_requirements_plugin PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    AUTOMOC ON)   # Q_OBJECT 面板类所需——仅插件目标开启（上方口径）

# 插件目标编译特性/编码：C++17 由被测公共头传染（cxx_std_17 PUBLIC 已在
# 计算库声明——本目标显式再设防自洽）；/utf-8 同款（plugin/ 中文注释源码）。
target_compile_features(sdurws_ird_requirements_plugin PUBLIC cxx_std_17)
if(MSVC)
    target_compile_options(sdurws_ird_requirements_plugin PRIVATE /utf-8)
endif()

# 插件私有头解析（plugin/*.hpp——PRIVATE，不外溢；ui/modeling 同款先例）。
target_include_directories(sdurws_ird_requirements_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

# 插件链接面（卡 §3.2 明文"插件目标 → 本单元计算库＋sdurws_ird_ui；
# 禁止链接任何其他业务域单元（R-1）"；acceptance 3 钉死同面）：
#   - sdurws_ird_requirements＝本单元计算库（业务判定唯一来源——面板零逻辑）；
#   - sdurws_ird_ui＝ui 平台公共面（CommandDescriptor/UiTypes/IDraftController
#     ——已落位公共头直用；IPluginUiRegistrar 未落位面按 P-REQ-8 以卡文
#     本为基线消费，见 plugin/ 各头 P-REQ-8 注）；
#   - Qt 三件套＝plugin 分类面（ird_gates R-3 仅查产品面形态——plugin 目标
#     链接 Qt 为卡 §3.2 sanctioned 的例外面）。
# ★ 门禁登记说明（ird_gates 引擎直跑口径，modeling T15 同款先例）：插件
#   链接面恰增 2 处既有模式命中——"requirements->requirements"（自边，
#   R-1 码面）与"requirements->ui"（表外 SUB），均为卡 §3.2 明文链接面的
#   引擎解析结果，与 modeling 插件 modeling->modeling/modeling->ui 两行
#   同型；登记册回填归 WP-01-T03 治理面（本任务 allowedFiles 不含
#   cmake/ird_gates_whitelist.cmake——按编排者口径出具 base..head 归一化
#   比对附件，裁决权在验收者）。
# 单行链接语句＝构建图契约测试的登记形态约束（ui 边与插件目标名同行
# 可见——BuildGraphContractTest 行扫描口径）。
target_link_libraries(sdurws_ird_requirements_plugin PUBLIC sdurws_ird_requirements sdurws_ird_ui Qt6::Core Qt6::Gui Qt6::Widgets)

# ---- 测试目标 sdurws_ird_requirements_test（随 WP-14-T02 登记）----
# 用例面（任务契约 acceptance 逐条具名自证）：
#   test/ObjectTypesTest.cpp   五对象 token 字面＋三词表（ProcessTag 11/
#                              TemplateKind 6/ArrayKind 4）token 转发与
#                              try 轨全表往返＋词表外拒绝（acceptance 3
#                              ——ObjectTypes.hpp 行）
#   test/ErrorsTest.cpp        RequirementErrorCode 全表 token＋
#                              RequirementError 值语义＋映射数据分批纪律
#                              （acceptance 3——Errors.hpp 行）
#   test/DiagCodesTest.cpp     REQ-* 工厂登记值＋真实 StableCodeRegistry
#                              注册行为（acceptance 3——DiagCodes.hpp 行，
#                              P-REQ-8 处置：以真实注册表验证全部通过）
#   test/BuildRedLineTest.cpp  产品面源码红线运行期扫描（零 Qt＋include
#                              面限于五边——acceptance 1/5，与文件末尾
#                              配置期守卫互为两道防线）
#   test/RequirementTypesTest.cpp  I-REQ-1~10 逐条正反例（T03 acceptance 1
#                              ——不变量用例逐条通过；具名 I-REQ-x）
#   test/CodecTest.cpp         canonical roundtrip/确定性/schema 拒绝/
#                              Profile 不入编码（T03 acceptance 2——编码
#                              登记供 evidence 切片）
#   test/ServicesTest.cpp      createXxx 错误语义逐项＋resolveRequiredCases
#                              §6.2 冻结 schema＋五规则拒绝＋I-REQ-7/9
#                              （T03 acceptance 4）
#   test/SamplingTest.cpp      GridBySpacing 规范化确定性/零样本合法/
#                              builder 零样本出口（T03 acceptance 3——
#                              KIN-04 冻结前提）
#   test/EditorTest.cpp        载入/编辑/撤销重做/删除引用保护/O-36 锚
#                              （T03 acceptance 4/5）
#   test/ImportTest.cpp        CSV 行级部分成功/字段字典自动识别/单位预览
#                              与 SI 归一/JSON roundtrip 副本导出/公式按数
#                              据解析/纯函数确定性（T04 acceptance 1~6——
#                              V-08/V-09/V-10 观测点，AT-02/AT-24）
#   test/ReadinessTest.cpp     R0~R9 分层正反例＋ReadinessSummary 数据源
#                              ＋Must/Should 分级＋预览纯函数＋P-REQ-6 边
#                              界（T05 acceptance 1~5/7）
#   test/CommandHandlersTest.cpp  O-35 无点 token＋载荷往返＋prepare 管线
#                              （首应用/增量逆命令/逆放）＋就绪现场重估
#                              ＋导入溯源＋防御复核（T05 acceptance 6）
#   test/OrientationResolutionTest.cpp  五规则解析成功/resolution 留痕/
#                              引用悬空与参数非法可定位/浅校验边界/
#                              确定性（T06 acceptance 1/2——V-03、AT-23）
#   test/CaptureTest.cpp       捕获/拾取确认写回/未确认零变更/STALE 警告/
#                              methodTag 留痕/未应用不失效/接口面不可绕过
#                              （T06 acceptance 3/4——REQ-08/10、AT-23）
#   test/TemplateArrayTest.cpp 六模板黄金默认/镜像黄金断言/不可镜像规则
#                              处置/四类阵列几何/溯源完整零回写/重生成冲突
#                              不静默覆盖/解除关联/批次整体回滚/删除源提示
#                              （T07 acceptance 1~5——V-06/V-07、AT-24）
# T08 增列（tasks/foundation/WP-14-T08.json，acceptance 1~5 具名自证；
#          QCoreApplication 级模型测试——消费插件目标的呈现模型面，Qt 经
#          插件目标 PUBLIC 传染；modeling T15 PluginPanelTest 同款先例）：
#   test/PluginPanelTest.cpp  四面板信息架构投影（ACC1：对象树锚/工位五
#                          规则显隐＋来源徽标/区域双模式采样＋预览几何/
#                          工况必验预览/校验 R0~R9 计数＋跳转＋语义说明/
#                          §10.9 装配描述符）＋编辑→草稿→应用链路（ACC2：
#                          L-R2 接受刷新＋就地拒绝、L-R3 buildDraftCommand
#                          信封与 P-REQ-6 不本地复判）＋插件零计算逻辑
#                          （ACC3：源面扫描——零 Qt 呈现模型/零业务域互链
#                          include；链接面由下方单行链接语句＋配置期守卫
#                          承载）＋两级撤销与辅助链路（ACC4：L-R4 两级分离
#                          /L-R10 向导五步/L-R11 副本导出提示/L-R12 只读
#                          门控）＋域命令登记（ACC5：九条卡表值/SA-16 零
#                          默认键/HotkeyBindingTable 无冲突登记/两套命名
#                          空间分离）——V-22 GUI 流程仅登记不启动（卡 §10.2）
# T09 增列（tasks/foundation/WP-14-T09.json，acceptance 1/2/5——黄金
#          数据集契约套件；GoldenFixture（TK-T08）六步生命周期消费四套
#          req-* 黄金数据集＋容差档案）：
#   test/GoldenDatasetsContractTest.cpp  四套黄金数据集全链装载（lint 语
#                          义）＋容差档案附录 D 锚定不放宽＋卡 §10.2 V 行
#                          requirements 侧断言（V-01~V-06/V-08~V-11/
#                          V-15~V-18/V-20/V-21；联合观测行仅登记）＋确定
#                          性负样例（同输入同字节/I-REQ-1 稳定排序复验/
#                          非有限与非法不静默）＋V-22 envUnavailable 登记
# 测试 main 自持＝testkit 报告设施（TestMainReport.cpp 安装 TestRecord
# Listener——ird-test-report.json 用例级明细与 gtest XML 并存；IO-T07/
# PRJ-T15/modeling 同款先例；IRD_TEST_INFO 携带需求/AT 追溯字段）。
enable_testing()

add_executable(sdurws_ird_requirements_test
    test/TestMainReport.cpp
    test/ObjectTypesTest.cpp
    test/ErrorsTest.cpp
    test/DiagCodesTest.cpp
    test/BuildRedLineTest.cpp
    test/RequirementTypesTest.cpp
    test/CodecTest.cpp
    test/ServicesTest.cpp
    test/SamplingTest.cpp
    test/EditorTest.cpp
    test/ImportTest.cpp
    test/ReadinessTest.cpp
    test/CommandHandlersTest.cpp
    test/OrientationResolutionTest.cpp
    test/CaptureTest.cpp
    test/TemplateArrayTest.cpp
    test/PluginPanelTest.cpp
    test/GoldenDatasetsContractTest.cpp)

target_compile_features(sdurws_ird_requirements_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_requirements_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（/utf-8 说明见产品目标处；gtest 头为
# ASCII 不受影响）。显式再设一次以自洽——测试目标应独立可读，不依赖
# 被测目标的编译选项细节。
if(MSVC)
    target_compile_options(sdurws_ird_requirements_test PRIVATE /utf-8)
endif()

# 测试链接产品库 sdurws_ird_requirements 同时验证"公共头可被正常 include
# ＋库可链接"（五边 PUBLIC 传染链随目标自动到达）。T08 增链插件目标（同
# 单元被测面——面板呈现模型/命令目录/编辑流/向导流的测试消费面；ird_gates
# 形态一"测试目标仅允许同单元产品目标"：plugin face 亦属同单元许可集，
# modeling T15 同款先例）。
# sdurws_ird_testkit＝报告/追溯设施消费面（RecordListener/IRD_TEST_INFO——
# T-1 允许形态：仅测试目标链 testkit，产品目标不链）。单行链接语句＝契约
# 测试 T-1 扫描的登记形态约束（testkit 与测试目标名同行可见——modeling/
# reporting 同款）。
target_link_libraries(sdurws_ird_requirements_test PRIVATE sdurws_ird_requirements sdurws_ird_requirements_plugin sdurws_ird_testkit GTest::gtest)

# 插件私有头解析（测试消费 plugin/*.hpp 呈现模型头——同单元 PRIVATE 面；
# 插件目标自身的 include 已 PRIVATE 自持，测试目标另行声明同路径；
# modeling T15 同款）。
target_include_directories(sdurws_ird_requirements_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

# 源码树位置注入：红线扫描用例在运行期扫描 requirements 源码核对红线
# （零 Qt/零表外 include——§3.2/§14.5）。经编译定义传 industrialrobot 根
# （modeling/io 同款——值内以单元名作子目录前缀，扫描面随单元自包含）。
target_compile_definitions(sdurws_ird_requirements_test PRIVATE
    IRD_REQUIREMENTS_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# vcpkg 的 gtest:x64-windows 是动态库（gtest.dll/gtest_main.dll）：把测试
# 可执行文件的运行期 DLL 拷到其输出目录，保证不经 PATH 配置即可直接运行
# （留痕与验收都要直接执行该可执行文件）。TARGET_RUNTIME_DLLS 需 CMake
# ≥3.21，本仓库工具链为 VS2022 自带的更新版本，满足。
add_custom_command(TARGET sdurws_ird_requirements_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_requirements_test>
            $<TARGET_FILE_DIR:sdurws_ird_requirements_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
# 框架原宏见 RobWork/RobWork/gtest/CMakeLists.txt：add_test ＋ <target>_
# report（运行测试并落 gtest XML）。本宏按 DTB §5.5 在本单元 CMake 内
# 定义（定义先于全部调用——CMake 顺序解析；core/io/modeling 同款形态，
# 不做跨单元共享——requirements 是业务域单元，不提供构建工具设施，
# NFR-MNT-04）。报告输出于构建树本目录 gtest_reports/<target>.xml；任务
# 留痕时拷贝入 doc/industrial-robot-design/traceability/（AGENTS.md §4.2
# 验证留痕）。报告目标不进默认构建（EXCLUDE_FROM_ALL）：跑测试是显式
# 动作，与框架"不随 ordinary build 执行测试"的约定一致。
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

ird_add_gtest(sdurws_ird_requirements_test)

# ---- 契约测试目标 sdurws_ird_requirements_contract_test（随 WP-14-T02
#      登记——卡 §3.1/§3.3）----
# 落位期契约面＝构建图边界契约（modeling BuildGraphContractTest 同款扫描
# 形态——五条登记边的存在性/封闭性/无业务域互链/testkit 仅测试目标）；
# T09 增列（tasks/foundation/WP-14-T09.json acceptance 3/4）：对端面契约
# 套件 PeerFaceContractTest.cpp——FaultInterceptor（TK-T09）伪造 project/
# io/evidence/diagnostics 四对端接口逐项断言形状（命令 payload 对 project/
# mapCsv 对 io RawTable/ReadinessSummary 对 evidence/REQ- 码对 diagnostics
# 契约夹具）＋T-1/T-2 测试侧红线具名自证（对端形状漂移由测试面捕获——
# 不构造产品边）。自有 main 同 _test 目标（报告设施接入）。
add_executable(sdurws_ird_requirements_contract_test
    contract_test/ContractTestMain.cpp
    contract_test/BuildGraphContractTest.cpp
    contract_test/PeerFaceContractTest.cpp)

target_compile_features(sdurws_ird_requirements_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_requirements_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（同 _test 目标口径）。
if(MSVC)
    target_compile_options(sdurws_ird_requirements_contract_test PRIVATE /utf-8)
endif()

# 链接面与 _test 同款（testkit＝报告设施——T-1 允许形态；单行链接语句
# ＝契约测试 T-1 扫描的登记形态约束，同 _test 目标口径）。
target_link_libraries(sdurws_ird_requirements_contract_test PRIVATE sdurws_ird_requirements sdurws_ird_testkit GTest::gtest)

# 源码树位置注入（红线/构建图扫描用例同 _test 口径）。
target_compile_definitions(sdurws_ird_requirements_contract_test PRIVATE
    IRD_REQUIREMENTS_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# 运行期 DLL 拷贝（vcpkg gtest 动态库——同 _test 目标的 POST_BUILD 口径）。
add_custom_command(TARGET sdurws_ird_requirements_contract_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_requirements_contract_test>
            $<TARGET_FILE_DIR:sdurws_ird_requirements_contract_test>
    COMMAND_EXPAND_LISTS)

ird_add_gtest(sdurws_ird_requirements_contract_test)

# ---- ctest LABELS "ird"（modeling/ui 落位同款 set_tests_properties）----
# 验收过滤口径：ctest -L "^ird$"——两测试（模型层、进程内、确定性）同
# 标签；_gui_test 形态本单元无（GUI 呈现随 WP-14-T08 插件目标另登记）。
set_tests_properties(sdurws_ird_requirements_test sdurws_ird_requirements_contract_test
    PROPERTIES LABELS "ird")

# ---- 依赖红线守卫（配置期断言——卡 §3.2，core/io/modeling foreach 同款）----
# requirements 的 industrialrobot 单元依赖仅限五条登记边：core＋diagnostics
# ＋project＋io＋evidence（卡 §3.2 边表——ARCH §3.5 业务域→L2/L3 公共
# 接口许可方向的实例化，白名单 "requirements->…" 五行同源）；链接到任何
# 其他 sdurws_ird_* 目标（业务域单元＝R-1 命中面；表外平台单元＝SUB 命中
# 面）即配置失败。零 Qt（R-3/NFR-MNT-01）。产品库本体零 gtest/testkit
# （D-06/T-1——断言宏只在消费方测试目标展开；本守卫遍历的是产品目标
# sdurws_ird_requirements 的链接面，测试目标的 gtest/testkit 边不在其内）。
# 两面读取（LINK_LIBRARIES 含 PRIVATE 边＋INTERFACE_LINK_LIBRARIES 含
# PUBLIC 边）防漏。属性未设置时 get_target_property 返回 "<var>-NOTFOUND"
# 字面串，不匹配任何模式，不会误报。testkit 不在第一条模式中逐字列出
# （modeling 同款先例）：sdurws_ird_testkit 落在通配 "^sdurws_ird_" 的
# 拦截范围（≠五条登记边即 FATAL），避免守卫正则字样干扰契约测试的 T-1
# 链接语句扫描（BuildGraphContractTest 的 NoTestkitEdge 用例以"逐行只认
# 测试目标链接语句"钉住产品目标零 testkit）。正式全仓门禁（依赖图白名单）
# 归 ird_gates（WP-01-T01），本守卫不替代它。
get_target_property(_ird_requirements_link_libs sdurws_ird_requirements LINK_LIBRARIES)
get_target_property(_ird_requirements_iface_libs sdurws_ird_requirements INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_requirements_link_libs _ird_requirements_iface_libs)
    if(_lib MATCHES "^sdurws_ird_"
       AND NOT _lib STREQUAL "sdurws_ird_core"
       AND NOT _lib STREQUAL "sdurws_ird_diagnostics"
       AND NOT _lib STREQUAL "sdurws_ird_project"
       AND NOT _lib STREQUAL "sdurws_ird_io"
       AND NOT _lib STREQUAL "sdurws_ird_evidence")
        message(FATAL_ERROR
            "requirements 依赖红线（R-1/卡 §3.2）：sdurws_ird_requirements "
            "仅可链接五条登记边 core/diagnostics/project/io/evidence"
            "（units/requirements.md §3.2、ARCH §3.5；含 testkit——T-1 产品"
            "目标禁链），发现 ${_lib}")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "requirements 依赖红线（R-3/NFR-MNT-01，计算库零 Qt——卡 §3.1）："
            "发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "requirements 依赖红线（D-06）：产品库本体不链接 gtest，发现 ${_lib}")
    endif()
endforeach()
