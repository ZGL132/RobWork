# =====================================================================
# workflow 单元（L4 业务域·编排：七阶段门控、下一步建议与生命周期入口
# 整合——ARCH §3.4）构建脚本
#
# 设计依据：
#   - units/workflow.md §3.1（单元组成——计算库 STATIC 零 Qt＋两测试目标
#     ＋插件界面目标）、§3.2（构建目标与依赖红线——依赖白名单八单元
#     core/ui/diagnostics/project/execution/io/evidence/reporting；"禁止
#     链接任何业务域单元计算库（R-1 门禁）"；"worker 目标：不建"）、
#     §6.5（R1 零新增 WF- 稳定码——本单元不建 DiagCodes 面，不做码登记）
#   - development-task-breakdown.md §5.1（双模式/目标命名/C++17 不用
#     C++20）、§5.3（全局禁止项）、§5.5（gtest 接入定稿）
#   - ARCHITECTURE.md §3.4（workflow 编排定位——"不直链任何业务域单元，
#     只经端口与事件协作"）、§3.5（依赖表——L4 编排单元→L2/L3 公共接口
#     许可方向；八边为其实例化，登记于 cmake/ird_gates_whitelist.cmake
#     的 "workflow->…" 八行）
#   - 需求 NFR-MNT-01（计算内核零 Qt——workflow 计算库为 L4 编排纯函数面，
#     Qt 界面面收敛在 _plugin 目标）
#   - 任务契约 tasks/foundation/WP-22-T02.json（本文件即其 acceptance 1/2
#     的落地面：INTERFACE 占位→STATIC＋八边白名单＋_plugin/_test/
#     _contract_test 三目标同批创建）
#
# 本文件随 WP-22-T02 落地（workflow 由上级 INTERFACE 占位升级为真实库）：
#   1. sdurws_ird_workflow STATIC（目标名与别名 RWS::ird::workflow 不变，
#      DTB §5.1）；T02 源码集＝src/Workflow.cpp 锚点翻译单元（空起步＝
#      无接口预建——NFR-MNT-04，project/execution/reporting/io 落位同款
#      先例；七公共头 Types/Gate/Advice/Lifecycle/Settings/Comparison/
#      Projection 与其实现随 WP-22-T03~T12 增列——卡 §3.1 布局表为权威，
#      增列路线见 src/Workflow.cpp 文件尾注）；
#   2. 插件界面目标 sdurws_ird_workflow_plugin（T02 即落位——本单元契约
#      acceptance 1 明文"创建 _plugin 目标"，与 kinematics"_plugin 随
#      T12"不同）：最小可注册实现＝ui.md §11.2 IPluginUiModule 三方法
#      实现＋§10.9 装配门面（assembly/ 公共头），零业务计算逻辑；Qt
#      Widgets 界面面（向导/首页/比较视图）随 WP-22-T04~T12 增列；
#   3. 测试目标 sdurws_ird_workflow_test 与
#      sdurws_ird_workflow_contract_test（自持 ird_add_gtest 宏：add_test
#      ＋<target>_report XML——core/io/modeling/requirements/kinematics
#      同款；ctest LABELS "ird"）；
#   4. 配置期依赖红线守卫（R-1/R-3 的 workflow 侧断言——卡 §3.2；全仓
#      正式门禁归 ird_gates/WP-01-T01，本守卫不替代它）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制；随本任务首个测试目标引入）----
# gtest 经 vcpkg 安装（gtest:x64-windows，经典模式）；REQUIRED＝找不到即
# 配置失败（失败即停，不静默跳过测试目标）。不消费框架的 USE_gtest/RW::
# gtest（从未生成），也不源码 vendor——两份 gtest 混用会破坏 ODR。
# 独立冒烟模式必须带 vcpkg toolchain 参数方可解析（CORE-T01 G-2 口径）。
find_package(GTest CONFIG REQUIRED)

# ---- Qt6 接入（WP-22-T02——插件目标的 Qt 载体面，R-3 唯一例外面）----
# 仅插件目标与测试目标传染面消费 Qt（卡 §3.2"_plugin＝L4 插件界面（Qt
# Widgets）"；modeling WP-13-T15/requirements WP-14-T08/kinematics
# WP-15-T12 同款先例）。组件＝Core/Gui/Widgets（Widgets 是后续向导/首页/
# 比较视图面板本体、Gui 因 ui 公共头 CommandDescriptor 的 QKeySequence
# 经头传染、Core 为 Qt 基面；不取 Test——本单元无 GUI 测试目标，GUI 用例
# 仅设计不执行——卡 §11.3 Windows GUI 约束）。REQUIRED 同 gtest：找不到
# 即配置失败（不静默跳过插件目标）。独立冒烟模式必须带 CMAKE_PREFIX_PATH
# 指向 Qt 安装（编排口径：Qt6 位于 D:/software/Qt/6.11.1/msvc2022_64）。
# 计算库 sdurws_ird_workflow 保持零 Qt——R-3/NFR-MNT-01：产品面（include/
# ＋src/）源码零 Qt 包含，链接面八边中 ui 边的 Qt 传染仅达测试/插件目标
# （ui 单元自身是 R-3 唯一例外登记目标——ui.md §3.2/DTB §4.5）。
find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC（目标名不变，DTB §5.1）----
# T02 源码集＝锚点翻译单元（空起步；后续任务增列路线见该文件尾注）。
# 门控/建议纯函数面（D-WF-3）与生命周期流程编排（§7）随 T03+ 逐头落位，
# 每公共头一个对应实现翻译单元——modeling/requirements 同款布局。
# WP-22-T03 增列（§3.1 布局表 Types/Gate/Advice 三头兑现——每头一个实现
# 翻译单元；门控判定只消费投影快照＋事件，级联提示纯函数面，D-WF-3）。
# WP-22-T04 增列（§7.1 新建项目三步向导——Lifecycle.hpp＋Lifecycle.cpp；
# 编排面消费 project createNew/①端口（白名单边），模板/导入经
# IDomainInitSubmitter 端口触达、零业务域头（R-1））。
# WP-22-T05 增列（§7.2 打开协议＋§7.8 无项目首页/最近项目——同头追加：
# OpenProjectFlow::run 编排（PM-02 五步 workflow 面——入口格式识别分流、
# 失败呈现 UX-03＋具体文件定位）、IRecentProjectsService/RecentProjects-
# Service（PM-10 上限 10/去重/失效标记/移除）、buildNoProjectHomeScreen
# （入口禁用数据）；零业务域头（R-1）维持）。
# WP-22-T06 增列（§7.3 关闭/切换/退出统一确认——同头追加：CloseFlow::run
# 编排（PM-03 三选/二选/9 态短标签——决策/草稿/排空三端口接缝，存储
# 上下文排空经 project requestClose/closed 白名单边；execution 侧排空经
# ICloseDrainPort 端口承载——零 execution 头依赖）、SchemeBranchSwitchFlow::
# run（PM-12 零写入会话选择——签名零 store 的结构性保证）；零业务域头
# （R-1）维持）。
add_library(sdurws_ird_workflow STATIC
    src/Workflow.cpp
    src/Types.cpp       # 词表与映射（§4.2 域↔阶段表／UX-02 文案键唯一构造点）
    src/Gate.cpp        # 七阶段门控（§4.4 状态机重放）＋级联失效映射（§4.3）
    src/Advice.cpp      # 下一步建议引擎（§6.3 八规则——UX-01 四要素）
    src/Lifecycle.cpp   # 新建向导（§7.1 PM-01）＋打开协议（§7.2 PM-02）＋
                        # 最近项目/无项目首页（§7.8 PM-10）编排面
    src/Projection.cpp  # 标题栏/状态栏与恢复横幅状态投影（§7.6 PM-11/PM-15——
                        # WP-22-T09：O9 服务＋两取数端口接缝＋纯函数组装核）
    src/Settings.cpp   # 用户设置持久化（§7.7 PM-14——WP-22-T10：
                        # IUserSettingsStore/UserSettingsStore＋canonical JSON
                        # 经 io 通道；首次消费 io 边——白名单八边既登记）
    src/Comparison.cpp  # 方案比较视图编排（§8.1/§8.2 UX-13——WP-22-T11：
                        # ISchemeComparisonController Draft 逐字＋两取数端口
                        # 接缝（ISchemeMetricPort 指标投影／IComparisonDiffPort
                        # diff 投影——零 modeling 编译边，字符串化同构投影）＋
                        # 纯函数组装核（高亮行/分组块）＋EVI-02/RPT-04 基准
                        # 一致性检查经 evidence 公共契约消费（白名单八边既
                        # 登记——evidence 边首消费）＋P-OPT-8 警告透传不虚构
    src/Commands.cpp)   # 命令集编排核（§8.3/§8.4 UX-13——WP-22-T12：
                        # 14 条命令 id 词表＋五类执行分流（修订经①端口/
                        # 会话零修订 KIN-06/向导触发/碰撞检查 P-WF-3 最小
                        # 语义/报告导出编排 reporting 服务——reporting 边
                        # 产品面首次 include 消费）＋五端口接缝（纯接口，
                        # L5 装配桥接）；计算库零 Qt——ui::CommandDescriptor
                        # 组装与 IWorkflowCommandContributor 兑现在 plugin/
                        # 目录（R-3 文件域隔离——§10.2 v1.3 偏差登记）

# C++17（core.md 决策 D-01 同源）：逐目标显式设定，不设全局标准。
# PUBLIC 传播：workflow 公共头（T03+ 落位）使用 C++17 设施，消费者编译
# workflow 头也必须至少 C++17。
target_compile_features(sdurws_ird_workflow PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_workflow PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：本单元源文件全部为 UTF-8（无 BOM）且含中文注释（AGENTS.md §2
# 注释规范）。MSVC 默认按系统代码页（GBK）解读无 BOM 源文件，会把 UTF-8
# 中文误读成乱码 token（core 落位时曾出现 C2001"常量中有换行符"），必须
# 显式 /utf-8。PUBLIC 传播：公共头同为 UTF-8，消费者编译含 workflow
# 头的翻译单元也需要。
if(MSVC)
    target_compile_options(sdurws_ird_workflow PUBLIC /utf-8)
endif()

# 公共头根：仅 workflow/include（内部含 sdurws/ird/workflow/）。当前仅
# README 保留位（公共头随 T03+ 增列）；R-2 红线：私有实现头不得置于
# include/——本目录是 workflow 唯一对外的头暴露位，实现细节头（若有）
# 只能放 src/ 并以相对路径被实现文件包含。
target_include_directories(sdurws_ird_workflow PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 编译依赖：卡 §3.2 依赖白名单八条接口依赖边——core＋ui＋diagnostics＋
# project＋execution＋io＋evidence＋reporting（ARCH §3.4"不直链任何业务
# 域单元，只经端口与事件协作"＋§3.5"L4 编排→L2/L3 公共接口"许可方向的
# 实例化，ird_gates_whitelist.cmake 的 "workflow->…" 八行同批登记）。
# PUBLIC 形态（kinematics 六边统一 PUBLIC 同款取舍）：
#   - core/diagnostics/project/execution/io/evidence/reporting/ui 八边
#     本任务"仅链接目标不消费上游公共头"（P-RPT-9/modeling/requirements/
#     kinematics T02 同款先例——边随落位登记，消费随 T03+ 任务回填：
#     core IDomainEventBus/DomainEventKind/TaskState→T03 门控触发与
#     PM-03 任务清单数据、ui StageId/StageReadinessSnapshot/ICommandRegistry
#     →T03 门控输入与 T12 命令贡献（D-WF-6 L4→L3 接口依赖）、project
#     ①命令提交协议/打开协议服务侧→T04~T08 生命周期编排、execution
#     shutdown(DrainPolicy)/drained()→T06 关闭编排（A7）、io 包校验/
#     JSON canonical→T07 包向导与 T10 设置持久化、evidence 当前性投影
#     →T03 级联提示（D-WF-2）、reporting 导出服务→T12 报告导出命令；
#     八边统一 PUBLIC 保持形态一致——契约 acceptance 1 白名单全登记）；
#   - 零业务域单元（R-1——workflow 不直链 modeling/requirements/
#     kinematics/trajectory/dynamics/drivetrain/selection/optimization，
#     跨单元协作走六类端口与事件，卡 §9 协作表）；
#   - 零 testkit（T-1——testkit 仅随测试目标消费）；
#   - 零 Qt 目标（R-3——本目标直接链接面无 Qt；ui 边的 Qt 传染经
#     sdurws_ird_ui 目标间接到达，属 ui 例外登记目标自身的链接面）。
# 用统一真名 sdurws_ird_<unit>：集成与独立冒烟两种模式下都可解析（别名
# RWS::ird::* 仅集成模式存在——kinematics/requirements 落位时同款取舍）。
target_link_libraries(sdurws_ird_workflow PUBLIC
    sdurws_ird_core
    sdurws_ird_ui
    sdurws_ird_diagnostics
    sdurws_ird_project
    sdurws_ird_execution
    sdurws_ird_io
    sdurws_ird_evidence
    sdurws_ird_reporting)

# ---- 插件界面目标 sdurws_ird_workflow_plugin（WP-22-T02 落位——契约
#      acceptance 1 明文创建；卡 §3.2"WP-22-T02~T12 落位"）----
# 最小可注册实现（零业务计算逻辑）：WorkflowUiModule＝ui.md §11.2
# IPluginUiModule 三方法最小实现（诚实空态——编排单元恒无域投影/无草稿
# 信封）；WorkflowPluginAssembly＝§10.9 装配门面（descriptor 现产：
# pluginId="workflow" 白名单第 8 token、三能力 false、panels/commands 空）。
# 源码布局＝plugin/ 目录（门禁 R-3 产品面扫描域为 include/**＋src/**，
# plugin/ 不在扫描域——Qt 红线的文件域隔离，modeling/requirements/
# kinematics 同款先例）；装配契约头＝assembly/（plugin 目标的 PUBLIC
# include 面——宿主装配层只见门面头与 ui 公共头，零插件私有头依赖——
# R-2/O-31 装配器单行适配面；requirements O-45 补缝教训前移，落位即建）。
# 本批次零 Q_OBJECT 类（界面控件随 T04+ 落位时引入），故不开 AUTOMOC；
# 首个 Q_OBJECT 类落位任务按 modeling/requirements 先例置 AUTOMOC ON
# （DTB §5.1 v0.17 行口径）。
add_library(sdurws_ird_workflow_plugin STATIC
    plugin/WorkflowUiModule.hpp           # §11.2 三方法最小实现（诚实空态）
    plugin/WorkflowUiModule.cpp
    plugin/WorkflowCommandCatalog.hpp     # 命令贡献目录（WP-22-T12——§8.3：
                                          #   14 条描述符冻结行＋IWorkflowCommand-
                                          #   Contributor Draft 兑现〔§10.2 v1.3
                                          #   落位偏差：ui::CommandDescriptor 携带
                                          #   QKeySequence，零 Qt 计算库头不可承载〕
                                          #   ＋处理器薄适配＋注册装配函数）
    plugin/WorkflowCommandCatalog.cpp
    plugin/WorkflowPluginAssembly.cpp)    # §10.9 装配门面实现（assembly/
                                          #   公共头的私有实现）
set_target_properties(sdurws_ird_workflow_plugin PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_workflow_plugin PRIVATE /utf-8)
endif()

# 插件私有头解析（plugin/*.hpp——PRIVATE，不外溢；modeling/ui 同款先例）。
target_include_directories(sdurws_ird_workflow_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
# 装配层（宿主插件）消费 <sdurws/ird/workflow/WorkflowPluginAssembly.hpp>
# ——PUBLIC include 面（非 include/ 产品扫描域——零 Qt 红线文件域隔离同
# plugin/ 口径）。
target_include_directories(sdurws_ird_workflow_plugin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/assembly>)

# 链接面（卡 §3.2"插件界面目标"行——L4 插件宿主面）：
#   - sdurws_ird_workflow＝本计算库（模块/门面消费 ui 公共头经其 PUBLIC
#     传染面到达 core/project 等八边值类型；自边 workflow->workflow 不在
#     ARCH §3.5 白名单——ird_gates 引擎按产品目标间边粗粒度报 SUB，与
#     modeling_plugin/requirements_plugin 的 R-1 自边命中同款存量模式，
#     DTB §4.5 登记册既有项，照实留痕交验收侧——WP-15-T02 比对留痕先例）；
#   - sdurws_ird_ui＝ui 公共面（IPluginUiModule/IPluginUiRegistrar/
#     PluginUiDescriptor/StageId——装配契约与词表面；workflow->ui 边按
#     卡 §3.2 白名单八边同批登记）；
#   - Qt 三件套＝plugin 分类面（ird_gates R-3 仅查 product/worker 形态；
#     T02 源码零 Qt 类消费，链接面先行建立——后续向导/面板批次直接
#     增列源文件即可）。
target_link_libraries(sdurws_ird_workflow_plugin PUBLIC
    sdurws_ird_workflow sdurws_ird_ui
    Qt6::Core Qt6::Gui Qt6::Widgets)

# 别名（集成模式）：供其余单元以 RWS::ird::workflow 消费；冒烟模式无
# 别名（与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::workflow ALIAS sdurws_ird_workflow)
    add_library(RWS::ird::workflow_plugin ALIAS sdurws_ird_workflow_plugin)
endif()

# ---- 测试目标 sdurws_ird_workflow_test（随 WP-22-T02 登记）----
# 用例面（任务契约 acceptance 1/2 逐条具名自证）：
#   test/BuildRedLineTest.cpp     产品面源码红线运行期扫描（零 Qt＋八边
#                                 include 面＋R-4 字面量＋文件头注释——
#                                 acceptance 1/2，与文件末尾配置期守卫
#                                 互为两道防线；kinematics 同款四用例）
#   test/GateAdviceTest.cpp       门控/建议模型测试（WP-22-T03 增列——
#                                 WF-VER-101~109/111 逐用例承载；直调
#                                 纯函数面，固定投影夹具，卡 §11.1）
# 测试 main 自持＝testkit 报告设施（TestMainReport.cpp 安装 TestRecord
# Listener——ird-test-report.json 用例级明细与 gtest XML 并存；IO-T07/
# PRJ-T15/modeling/requirements/kinematics 同款先例；IRD_TEST_INFO 携带
# 需求/AT 追溯字段）。
enable_testing()

add_executable(sdurws_ird_workflow_test
    test/TestMainReport.cpp
    test/BuildRedLineTest.cpp
    test/GateAdviceTest.cpp
    test/LifecycleTest.cpp  # 新建向导模型测试（WP-22-T04——词表/校验/摘要/
                            # P-03 零数值/commit 前置契约）
    test/OpenWizardHomeTest.cpp  # 打开分流/最近项目/首页模型测试（WP-22-T05——
                                  # PM-02 格式识别与失败呈现、PM-10 容量/去重/
                                  # 失效标记/移除、入口禁用数据）
    test/CloseFlowTest.cpp  # 关闭/切换/退出统一确认模型测试（WP-22-T06——
                            # PM-03 三选/二选/9 态短标签/取消中止、PM-12
                            # 零写入编排、前置 fail-fast；全桩直调编排核）
    test/SaveAsPackageTest.cpp  # 另存为/包导出/包导入模型测试（WP-22-T07——
                                # PM-05 记忆默认/取消即清理观测位/失败呈现
                                # UX-03/校验报告行集/前置 fail-fast；三执行
                                # 端口脚本化桩直调编排核）
    test/ReconnectReadOnlyTest.cpp  # 旧格式升级指引/只读会话/重关联模型测试
                                    # （WP-22-T08——PM-06 指引三键提取、PM-07
                                    # 只读会话数据面、PM-09 编排核全分支；
                                    # 端口脚本化桩直调编排核）
    test/StatusProjectionTest.cpp  # 标题栏/状态栏与恢复横幅模型测试（WP-22-T09——
                                    # WF-VER-217 标题格式逐组合黄金串、PM-11 五路
                                    # 来源透传、P-UI-2 结果状态选键、PM-15 三场景
                                    # 择一序/动作可用位/空集不渲染、接口消费路径
                                    # 钉扎；纯函数直调＋端口脚本化桩）
    test/SettingsTest.cpp  # 用户设置持久化模型测试（WP-22-T10——WF-VER-221：
                            # canonical 黄金字节/损坏变体矩阵＋诊断/I-WF-5
                            # 分析配置字段双向隔离/P-WF-5 容量截断与零脱敏/
                            # 接口消费路径钉扎；真实落盘临时目录直调存储面）
    test/ComparisonTest.cpp  # 方案比较视图模型测试（WP-22-T11——值渲染黄金串/
                              # 维度键词表/高亮行组装〔同构违约/不可算"—"/零容差
                              # 高亮〕/diff 分组分拣〔未知 token fail-fast/通道
                              # 不可用降级/P-OPT-8 警告透传零补造〕/编排核前置
                              # fail-fast 三面/拒绝与接受互斥/接口消费路径钉扎）
    test/CommandsTest.cpp)  # 命令集编排核模型测试（WP-22-T12——WF-VER-222 词表/
                            # 分流黄金表＋WF-VER-223 模型半区〔复位关节零修订端
                            # 口触达〕＋五类编排分流〔修订经①端口/会话/向导/
                            # 碰撞 P-WF-3 最小语义触达隔离/报告导出编排〕＋
                            # fail-fast 装配违约＋接口消费路径钉扎；零 Qt——
                            # 描述符/注册/模糊搜索可达面在契约测试目标）

target_compile_features(sdurws_ird_workflow_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_workflow_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（/utf-8 说明见产品目标处；gtest 头为
# ASCII 不受影响）。显式再设一次以自洽——测试目标应独立可读，不依赖
# 被测目标的编译选项细节。
if(MSVC)
    target_compile_options(sdurws_ird_workflow_test PRIVATE /utf-8)
endif()

# 测试链接产品库 sdurws_ird_workflow 同时验证"公共头可被正常 include
# ＋库可链接"（八边 PUBLIC 传染链随目标自动到达）。
# sdurws_ird_testkit＝报告/追溯设施消费面（RecordListener/IRD_TEST_INFO——
# T-1 允许形态：仅测试目标链 testkit，产品目标不链）。单行链接语句＝契约
# 测试 T-1 扫描的登记形态约束（testkit 与测试目标名同行可见——modeling/
# reporting/requirements/kinematics 同款）。本目标零 Qt（红线扫描用例面）。
target_link_libraries(sdurws_ird_workflow_test PRIVATE sdurws_ird_workflow sdurws_ird_testkit GTest::gtest)

# 源码树位置注入：红线扫描用例在运行期扫描 workflow 源码核对红线
# （零 Qt/八边 include 面——卡 §3.2）。经编译定义传 industrialrobot 根
# （modeling/io/requirements/kinematics 同款——值内以单元名作子目录前缀，
# 扫描面随单元自包含）。
target_compile_definitions(sdurws_ird_workflow_test PRIVATE
    IRD_WORKFLOW_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# vcpkg 的 gtest:x64-windows 是动态库（gtest.dll/gtest_main.dll）：把测试
# 可执行文件的运行期 DLL 拷到其输出目录，保证不经 PATH 配置即可直接运行
# （留痕与验收都要直接执行该可执行文件）；八边传染面（ui→Qt6）的共享库
# 同批拷贝。TARGET_RUNTIME_DLLS 需 CMake ≥3.21，本仓库工具链为 VS2022
# 自带的更新版本，满足。
add_custom_command(TARGET sdurws_ird_workflow_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_workflow_test>
            $<TARGET_FILE_DIR:sdurws_ird_workflow_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
# 框架原宏见 RobWork/RobWork/gtest/CMakeLists.txt：add_test ＋ <target>_
# report（运行测试并落 gtest XML）。本宏按 DTB §5.5 在本单元 CMake 内
# 定义（定义先于全部调用——CMake 顺序解析；core/io/modeling/requirements/
# kinematics 同款形态，不做跨单元共享——workflow 是业务编排单元，不提供
# 构建工具设施，NFR-MNT-04）。报告输出于构建树本目录 gtest_reports/
# <target>.xml；任务留痕时拷贝入 doc/industrial-robot-design/traceability/
# （AGENTS.md §4.2 验证留痕）。报告目标不进默认构建（EXCLUDE_FROM_ALL）：
# 跑测试是显式动作，与框架"不随 ordinary build 执行测试"的约定一致。
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

ird_add_gtest(sdurws_ird_workflow_test)

# ---- 契约测试目标 sdurws_ird_workflow_contract_test（随 WP-22-T02
#      登记——契约 acceptance 1 明文创建）----
# 落位期契约面＝构建图边界契约（kinematics/requirements BuildGraphContract-
# Test 同款扫描形态——八条登记边的存在性/封闭性/无业务域互链/testkit
# 仅测试目标/include 面同界）＋插件可注册契约（PluginRegistrationContract
# Test——真实 IPluginUiRegistrar 校验序三查：白名单 Ok/白名单外拒绝/
# 重复拒绝——acceptance 1"最小可注册实现"的执行证明）；业务对端面契约
# （ui 快照/事件 fake 套件）随 T03~T12 落地（卡 §11——T02 不预建占位
# 用例）。
# WP-22-T03 增列 GateContractTest.cpp：门控零领域调用运行断言（WF-VER-112
# ——R-1/I-WF-2/I-WF-3 判定面文本扫描＋GateInputs 类型面 static_assert）
# ＋P-WF-2 数据形状对齐（status 词表＝ui::StageViewStatus）＋词表零新增
# ＋投影不双权威（DTB 禁止项）的契约面自证。
# 自有 main 同 _test 目标（报告设施接入）。
add_executable(sdurws_ird_workflow_contract_test
    contract_test/ContractTestMain.cpp
    contract_test/BuildGraphContractTest.cpp
    contract_test/PluginRegistrationContractTest.cpp
    contract_test/GateContractTest.cpp
    contract_test/NewProjectWizardContractTest.cpp  # WF-VER-201~204（WP-22-T04——
                                                    # createNew＋①端口真实落盘
                                                    # 契约；领域处理器为测试桩，
                                                    # R-1 不链 modeling）
    contract_test/OpenWizardContractTest.cpp  # WF-VER-205/206（WP-22-T05——打开
                                              # 五步协议三入口真实落盘＋失败
                                              # 不动当前项目 Fault 契约）
    contract_test/CloseFlowContractTest.cpp  # WF-VER-209~212（WP-22-T06——统一
                                             # 确认数据面＋取消中止＋A7 等待
                                             # 排空＋协作取消＋PM-12 零写入＋
                                             # 切换候选验证，真实落盘契约）
    contract_test/SaveAsPackageContractTest.cpp  # WF-VER-213~215（WP-22-T07——
                                                 # 另存真实复制勾选裁剪＋打开
                                                 # 协议进入＋取消零残留；包导出
                                                 # 真实 ZIP 产物＋源零写＋取消
                                                 # 不产出；包导入越界路径真实
                                                 # 拒绝＋失败不留目标目录＋导出
                                                 # 导入回路，真实落盘契约——
                                                 # 与 io 真实包设施联合）
    contract_test/ReconnectReadOnlyContractTest.cpp  # WF-VER-207/208/216（WP-22-T08——
                                                     # 旧格式稳定拒绝＋升级指引
                                                     # ＋原文件不动；双实例只读
                                                     # 降级＋PID 提示＋提交/草稿
                                                     # 写轨拒绝；重关联显式提交
                                                     # 真实产生新修订＋失败零写
                                                     # 盘面复核，真实落盘契约）
    contract_test/StatusBannerContractTest.cpp  # WF-VER-217/218（WP-22-T09——标题栏
                                                 # 来源真实链〔显示名/分支/只读位
                                                 # 经真实 store 取数〕＋恢复横幅
                                                 # 三场景真实落盘〔.staging 残留/
                                                 # 孤儿草稿真实 open 产出＋execution
                                                 # 真实 T14 重建〕＋统一诊断目录
                                                 # 集成〔真实 DiagCatalog 折叠〕，
                                                 # 真实落盘契约）
    contract_test/SettingsContractTest.cpp  # WF-VER-221 契约半区（WP-22-T10——
                                            # 设置 canonical 字节 io 通道交叉复核
                                            # 〔store 二次幂等＋io 语法 reader 独立
                                            # 读回〕＋Win32 独占锁注入的原子保留
                                            # 〔失败先前文件原样＋暂存零残留〕＋
                                            # 诊断通道词形，真实落盘契约——与 io
                                            # 真实 JSON 设施联合）
    contract_test/ComparisonContractTest.cpp  # WF-VER-224/225（WP-22-T11——方案
                                               # 比较 2~4 方案真实编排契约：与真实
                                               # project store 联合〔createNew＋建支
                                               # 命令族桩产生方案分支〕＋分支 label
                                               # 经 branchTips 真实取数＋指标/diff
                                               # 端口为 L5 装配桥接的测试等价物
                                               # 〔R-1 零 modeling 边〕＋EVI-02 基准
                                               # 不一致真实拒绝〔evidence 公共契约
                                               # 检查〕＋P-OPT-8 警告透传零虚构＋
                                               # 接口消费路径钉扎）
    contract_test/CommandsContractTest.cpp  # WF-VER-222/223/226（WP-22-T12——命令
                                              # 集真实注册与端到端契约：真实 ui
                                              # CommandRegistry 注册 14 条＋命令面板
                                              # 模糊搜索可达 ≥10 条〔UX-13/F5——过
                                              # 渡中文/关键字子序列命中〕＋重复 id
                                              # 注册边界拒绝〔ui 设施行为——不覆盖
                                              # 不静默〕＋复位关节经注册表 submit
                                              # 端到端零修订〔KIN-06——真实 store
                                              # 修订链/branchTips/目录树盘面复核〕＋
                                              # 撤销/重做主线〔PM-18/AT-29——真实
                                              # UndoRedoService 逆命令恰好一新修订、
                                              # 历史只增不改、空历史稳定提示〕；
                                              # IWorkflowCommandContributor 经接口
                                              # 虚派发钉扎——插件目标链接供
                                              # WorkflowCommandCatalog 消费）
    contract_test/GateAdviceFixtureContractTest.cpp  # golden wf-* 黄金夹具（WP-22-T13——
                                                      # §11.0 contract-fixture 登记契约
                                                      # ＋门控/建议/映射纯函数面固定投影
                                                      # 夹具确定性重放〔真 testkit 装载器
                                                      # ＋独立生成器双实现互证〕＋档案零
                                                      # 容差通道；经 IStageGateService&/
                                                      # INextStepAdvisor& 接口虚派发消费）
    contract_test/InFlightArchiveContractTest.cpp  # WF-VER-210 端到端在途归档（WP-22-T13——
                                                    # wp22-t06 划归的执行半区：真实
                                                    # execution 全链〔TaskController/
                                                    # DrainCoordinator/TaskScheduler/
                                                    # DomainEventBusImpl〕＋ICloseDrainPort
                                                    # 真实桥〔tasksByProject 投影＋
                                                    # shutdown(CancelQueuedAndWait)＋
                                                    # drained 有界轮询〕；A7 等待期间
                                                    # 存储上下文保持＋归档完成事件锚定
                                                    # 原修订＋协作取消对照腿）
    contract_test/LifeCycleMainlineContractTest.cpp)  # WF-VER-227 全主线回归（WP-22-T13——
                                                       # 新建→编辑→应用→撤销/重做→包
                                                       # 导出导入→另存单用例串接〔AT-20/
                                                       # 29——真实 store 全程＋真实 ui
                                                       # CommandRegistry 统一 submit 路径
                                                       # ＋真实 io 导出/导入器＋另存真实
                                                       # 复制；修订链黄金序列 2→3→4→5〕）

target_compile_features(sdurws_ird_workflow_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_workflow_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（同 _test 目标口径）。
if(MSVC)
    target_compile_options(sdurws_ird_workflow_contract_test PRIVATE /utf-8)
endif()

# 插件私有头解析（WP-22-T12——CommandsContractTest 消费 plugin/ 目录的
# WorkflowCommandCatalog.hpp：单元根 PRIVATE include，modeling test 消费
# plugin/PanelCommandCatalog.hpp 同款先例；仅测试目标可达，产品面不外溢
# ——R-2 红线不受影响）。
target_include_directories(sdurws_ird_workflow_contract_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

# 链接面与 _test 同款（testkit＝报告设施——T-1 允许形态；单行链接语句
# ＝契约测试 T-1 扫描的登记形态约束，同 _test 目标口径）。
# sdurws_ird_workflow_plugin＝同单元插件目标（装配门面/模块——可注册
# 契约用例的被测面；"测试目标仅允许同单元产品目标"纪律下 plugin face
# 亦属同单元许可集——modeling WP-13-T15 先例同款；其 PUBLIC 面携带
# sdurws_ird_ui→Qt 传染，供注册端口 createPluginUiRegistrar 符号解析）。
target_link_libraries(sdurws_ird_workflow_contract_test PRIVATE sdurws_ird_workflow sdurws_ird_workflow_plugin sdurws_ird_testkit GTest::gtest)

# 源码树位置注入（构建图扫描用例同 _test 口径）。
target_compile_definitions(sdurws_ird_workflow_contract_test PRIVATE
    IRD_WORKFLOW_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# 运行期 DLL 拷贝（vcpkg gtest 动态库＋ui→Qt6 传染面——同 _test 目标的
# POST_BUILD 口径；注册端口用例运行期加载 Qt6 Core 所需）。
add_custom_command(TARGET sdurws_ird_workflow_contract_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_workflow_contract_test>
            $<TARGET_FILE_DIR:sdurws_ird_workflow_contract_test>
    COMMAND_EXPAND_LISTS)

ird_add_gtest(sdurws_ird_workflow_contract_test)

# ---- ctest LABELS "ird"（modeling/ui/requirements/kinematics 落位同款）----
# 验收过滤口径：ctest -L "^ird$"——两测试（模型层、进程内、确定性）同
# 标签；_gui_test 形态本单元无（GUI 用例仅设计不执行——卡 §11.3）。
set_tests_properties(sdurws_ird_workflow_test sdurws_ird_workflow_contract_test
    PROPERTIES LABELS "ird")

# ---- 依赖红线守卫（配置期断言——卡 §3.2，core/io/modeling/requirements/
#      kinematics foreach 同款）----
# workflow 的 industrialrobot 单元依赖仅限八条登记边：core＋ui＋diagnostics
# ＋project＋execution＋io＋evidence＋reporting（卡 §3.2 依赖白名单——
# ARCH §3.4 编排定位＋§3.5 L4→L2/L3 许可方向的实例化，白名单
# "workflow->…" 八行同源）；链接到任何其他 sdurws_ird_* 目标（业务域单元
# ＝R-1 命中面；表外平台单元 runtime/policy＝SUB 命中面；含 testkit＝T-1
# 命中面）即配置失败。零 Qt（R-3/NFR-MNT-01——本目标直接链接面无 Qt
# 目标；ui 边的 Qt 传染经 sdurws_ird_ui 间接到达，不落本遍历的字面集合）。
# 产品库本体零 gtest/testkit（D-06/T-1——断言宏只在消费方测试目标展开；
# 本守卫遍历的是产品目标 sdurws_ird_workflow 的链接面，测试目标的
# gtest/testkit 边不在其内）。两面读取（LINK_LIBRARIES 含 PRIVATE 边＋
# INTERFACE_LINK_LIBRARIES 含 PUBLIC 边）防漏。属性未设置时
# get_target_property 返回 "<var>-NOTFOUND" 字面串，不匹配任何模式，不
# 会误报。testkit 不在第一条模式中逐字列出（kinematics 同款先例）：
# sdurws_ird_testkit 落在通配 "^sdurws_ird_" 的拦截范围（≠八条登记边即
# FATAL），避免守卫正则字样干扰契约测试的 T-1 链接语句扫描
# （BuildGraphContractTest 的 NoTestkitEdge 用例以"逐行只认测试目标链接
# 语句"钉住产品目标零 testkit）。正式全仓门禁（依赖图白名单）归
# ird_gates（WP-01-T01），本守卫不替代它。
get_target_property(_ird_workflow_link_libs sdurws_ird_workflow LINK_LIBRARIES)
get_target_property(_ird_workflow_iface_libs sdurws_ird_workflow INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_workflow_link_libs _ird_workflow_iface_libs)
    if(_lib MATCHES "^sdurws_ird_"
       AND NOT _lib STREQUAL "sdurws_ird_core"
       AND NOT _lib STREQUAL "sdurws_ird_ui"
       AND NOT _lib STREQUAL "sdurws_ird_diagnostics"
       AND NOT _lib STREQUAL "sdurws_ird_project"
       AND NOT _lib STREQUAL "sdurws_ird_execution"
       AND NOT _lib STREQUAL "sdurws_ird_io"
       AND NOT _lib STREQUAL "sdurws_ird_evidence"
       AND NOT _lib STREQUAL "sdurws_ird_reporting")
        message(FATAL_ERROR
            "workflow 依赖红线（R-1/卡 §3.2）：sdurws_ird_workflow 仅可链接"
            "八条登记边 core/ui/diagnostics/project/execution/io/evidence/"
            "reporting（units/workflow.md §3.2、ARCH §3.4/§3.5；含 testkit"
            "——T-1 产品目标禁链），发现 ${_lib}")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "workflow 依赖红线（R-3/NFR-MNT-01，计算库零 Qt——卡 §3.1）："
            "发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "workflow 依赖红线（D-06）：产品库本体不链接 gtest，发现 ${_lib}")
    endif()
endforeach()
