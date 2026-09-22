# =====================================================================
# ui 单元（工作台壳、命令入口与状态投影——L3 界面支撑）构建脚本
#
# 设计依据：
#   - units/ui.md §3.1（构建目标与依赖红线：sdurws_ird_ui STATIC——自
#     UI-T02 起由 INTERFACE 占位升级、目标名不变；PUBLIC 链 RWS::ird::core
#     ＋RWS::ird::diagnostics＋Qt Core/Gui/Widgets；R-3 唯一例外登记目标；
#     三个测试目标 test/_contract_test/_gui_test 分工与 ird_gui 串行标签）、
#     §3.3（公共头布局——头随 UI-T03+ 落地，本任务仅落位构建面）、§12.1
#     （测试目标与分层）、§13 UI-T02 行（本任务产物：目标编译＋红线门禁
#     含 ui 例外登记文本＋静态扫描零命中）
#   - development-task-breakdown.md §5.1（双模式/目标命名/C++17 不用
#     C++20）、§5.3（全局禁止项）、§5.5（gtest 经 vcpkg find_package(GTest
#     CONFIG REQUIRED) 失败即停；测试 main 自持；ADD_RW_GTEST 语义宏自持）
#   - 任务契约 tasks/foundation/UI-T02.json acceptance 1~5（构建落位/R-3
#     例外登记文本/三测试目标/testkit_qt 启用/O-31＋P-UI-5 处置）
#   - 需求 NFR-MNT-01（ui 例外登记：全产品唯一允许 Qt Widgets 的平台单元）
#
# 陷阱处置（契约 knownPitfalls，acceptance 4/5 编入）：
#   - O-31（ui.md 依赖声明三方矛盾，DTB §4.2 登记未决）：本任务交付面仅含
#     ARCH §3.5 表内边（ui→core、ui→diagnostics——ird_gates_whitelist.cmake
#     IRD_ALLOWED_UNIT_EDGES 既有两行）与 R-3 例外登记；不创建任何指向
#     project/evidence/execution/policy/runtime 的链接或 include（C-3/4/5/
#     7/8/10/11 协作的形态裁决前置，不私裁增边或最小注入模式）。落位期
#     连公共头都未产出（include/ 仅 README 保留位），零跨单元头消费；
#     配置期守卫（下方"依赖红线守卫"块）把"仅两条登记边＋Qt"钉为硬约束。
#   - P-UI-5（ARCH §3.5 "ui→core：命令注册表类型"行消歧，登记未决）：按
#     ui.md CF-1 结论落位——命令注册表类型归 ui 自有头（ICommandRegistry.hpp，
#     §10.3），core 侧无此类型（core.md §2.3 同口径）；对本任务无实现影响
#     （落位期零接口），ui→core 边照常建立（本文件链接行）。消账权在架构
#     所有者，本任务不私改 ARCH 文本。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制）----
# REQUIRED：找不到即配置失败（失败即停，不静默跳过测试目标）。不消费框架
# USE_gtest/RW::gtest（从未生成）、不源码 vendor。vcpkg 的 gtest:x64-windows
# 为动态库（运行期 DLL 拷贝见各测试目标 POST_BUILD）。
find_package(GTest CONFIG REQUIRED)

# ---- Qt 组件发现（R-3 例外承载面——NFR-MNT-01 唯一例外目标）----
# ui.md §3.1：sdurws_ird_ui 链 Qt Core/Gui/Widgets（三件套——Widgets 为
# R-3 例外的本体，Core/Gui 为其依赖底座）。本构建树与框架同源实测 Qt6
# （build/CMakeCache.txt Qt6_DIR=D:/software/QT/6.11.1/msvc2022_64；框架
# RobWork/CMakeLists.txt 优先 Qt6、回落 Qt5）。本任务只落 Qt6 REQUIRED：
#   1. ird_gates 对 target_link_libraries 做"文本级静态解析"（ird_gates.cmake
#      第 1 步确定性前提——链接目标名必须字面可见，变量形态会被判"未知
#      目标族"LIB 命中），Qt6/Qt5 双分支的字面名切换会破坏该前提；
#   2. Qt5 回落属框架级兼容面（RobWork 自身的 Qt5 分支），industrialrobot
#      侧如需回落随构建约定任务统一登记，不在本任务私裁。
# 组件与框架 Qt6 探测同源（Core/Gui/Widgets；不含 OpenGL——ui 落位期
# 三维视图宿主 RWStudioView3D 由框架目标承载，UI-T05 阶段 B 复核）。
# Test 组件（UI-T06 增量）：仅 gui_test 目标消费（QTest::keyClick 键盘
# 注入——§7.4 键盘导航/§7.3 物理键触发的 GUI 观测面）；测试目标链接面
# 增量登记 ui.md §3.1/§16.7 v0.8，产品面不变（R-3 例外范围不动）。
find_package(Qt6 COMPONENTS Core Gui Widgets Test REQUIRED)

# ---- 产品库目标：STATIC（UI-T02 落位，目标名不变，DTB §5.1）----
# 源文件（随 UI-T03 增列——§13 UI-T03 行"工作台壳与五区布局"）：
#   - src/Ui.cpp＝UI-T02 锚点翻译单元（保留——锚点/共享工具兼用位）；
#   - src/WorkbenchShell.cpp＝工作台壳实现（IWorkbenchShell §10.1：五区组装/
#     布局记忆/首页/状态栏/门控）；
#   - src/ShellSupport.cpp＝壳支撑设施（壳层命令板/最近项目模型/后台落盘
#     线程/布局记忆/§3.5 UI 码描述符表）。
#   - src/StatusProjection.cpp＝七态公共状态呈现实现（UI-T04——§6.3 词表
#     token/文案键表与映射求值纯函数；§13 UI-T04 行"七态公共状态呈现"）。
#   - src/View3DPlaceholder.cpp＝三维视图占位实现（UI-T05 阶段 A——§13
#     UI-T05 行"阶段 A 仅中央区占位与视图契约登记"：View3DContract.hpp
#     契约数据〔UX-11 清单/KIN-06 语义钉住〕唯一权威定义点＋中央区占位
#     面板构建；交互实现归阶段 B，届时并入实现 TU 而契约定义点不变）。
#   - src/CommandRegistry.cpp＝命令注册表实现（UI-T06——§10.3/§7.2/§7.4
#     模糊投影/§7.5 谓词；§13 UI-T06 行"CommandRegistry"）。
#   - src/GlobalShortcutRegistry.cpp＝全局快捷键唯一注册点实现（UI-T06——
#     §10.4/§7.3；全产品唯一允许"QShortcut＋WindowShortcut"形态的 TU，
#     UI-HKY-3 静态扫描豁免面）。
#   - src/CommandPalette.cpp＝命令面板实现（UI-T06——§7.4 键盘导航/投影
#     渲染；私有头 src/CommandPalette_p.hpp 不进 include/——R-2）。
#   - src/PolicySummaryCard.cpp＝工程策略摘要只读卡实现（UI-T07——§6.7
#     呈现数据纯函数装配〔分组异名/单位换算/显式不适用〕＋右栏卡控件构建；
#     公共契约头 include/sdurws/ird/ui/PolicySummaryCard.hpp 随 §16.7 v0.9
#     登记，文案/行键单点）。
#   - src/FormEditCommon.cpp＝表单公共件模型层实现（UI-T08——公共编辑
#     规则的唯一规则承载：显示投影解析/格式化〔core Units 唯一换算入口〕、
#     就地错误、筛选、批量粘贴、表单级确认应用、取消恢复；零 Qt——模型
#     层无 GUI 可测；公共契约头 include/sdurws/ird/ui/FormEditCommon.hpp
#     随 §16.7 v1.0 登记）。
#   - src/ParamTablePanel.cpp＝参数表面板实现（UI-T08——表单公共件的第一
#     渲染面：筛选行＋四列表格〔值｜单位同显＋状态列就地错误〕＋非模态
#     确认区＋剪贴板批量粘贴＋可选开发诊断开关；GUI 只渲染不持语义）。
#   - src/UiText.cpp＝工程用语文案体系实现（UI-T09——§3.5 键解析唯一出口
#     ＋§6.6 数值带单位/不适用占位/零哈希守卫＋内建中文过渡文案表；公共
#     契约头 include/sdurws/ird/ui/UiText.hpp 随 §16.7 v1.1 登记）。
#   - src/StageNavigationModel.cpp＝阶段导航投影实现（UI-T09——§10.2
#     IStageNavigationModel＋§6.4 呈现合成与导航时序＋§6.5 StageStatusModel
#     汇聚＋观察者分发；公共契约头 include/sdurws/ird/ui/
#     IStageNavigationModel.hpp 随 §16.7 v1.1 登记；零 Qt——模型层可测）。
#   - src/AboutDialog.cpp＝帮助入口与关于对话框实现（UI-T10——§11.4 静态
#     白名单 ∩ 装配报告的清单装配＋版本基线呈现行＋用户手册入口＋关于
#     对话框 Widget；公共契约头 include/sdurws/ird/ui/AboutDialog.hpp 随
#     §16.7 v1.2 登记，白名单词表/行模型/文案常量单点）。
#   - src/UiSessionController.cpp＝UI 会话控制器实现（UI-T11——§5.2 七态
#     状态机＋§5.4 关闭/切换统一确认对话框机制〔数据装配/决议解析/处置
#     执行〕＋§5.6 Draining 四级防线〔T_force/T_force2 装配期可配——
#     P-UI-8 默认保守值〕＋§5.3/§5.7 迟到写提示；零 Qt——模型层可测；
#     公共契约头 include/sdurws/ird/ui/UiSessionController.hpp 随 §16.7
#     v1.3 登记，C-3/5/8 会话端口消费面）。
#   - src/DraftController.cpp＝草稿控制器实现（UI-T12——§8 DraftController
#     与草稿交互：保存/应用分离落盘编排〔PM-04 零修订红线——控制器零
#     命令通道〕＋§8.2 未保存标记与跨模块汇总合并＋§8.4 定时保存〔UI
#     线程零磁盘 IO——写轨只经串行落盘执行器；60 s 默认周期〕＋§8.3
#     打开恢复与 .bak 回退横幅＋§8.5 StaleRevisionRejected 冲突呈现
#     ＋§8.7 草稿局部撤销栈；零 Qt——模型层可测；公共契约头
#     include/sdurws/ird/ui/IDraftController.hpp 随 §16.7 v1.4 登记，
#     C-5 写半区端口 IUiDraftStorePort 消费面）。
#   - src/DiagnosticPresentation.cpp＝诊断呈现模型实现（UI-T13——§9.1/
#     §10.8：查询转发/原因链上溯/动作映射表/三要素同显〔四态占位不伪造
#     数值〕/恢复横幅 PM-15/Tier-U 面板/目录订阅 Marshal；公共契约头
#     include/sdurws/ird/ui/IDiagnosticPresentationModel.hpp 随 §16.7
#     v1.5 登记，C-9 呈现半区端口消费面）。
#   - src/CommandInteractionBridge.cpp＝命令确认交互桥实现（UI-T13——
#     §9.2/§9.3：命令执行线程同步回调→Marshal 至 UI 线程→阻塞等待；
#     isAlive 探针；principal 会话采集缓存 P-UI-4；C-6 外翻形态——实现
#     ui 自有 IUiCommandInteraction 端口；公共契约头 include/sdurws/ird/
#     ui/CommandInteractionBridge.hpp 随 §16.7 v1.5 登记）。
#   - src/TaskPresentation.cpp＝任务呈现模型实现（UI-T13——§9.4/§9.5/
#     §10.7：清单双区〔当前置顶/后台只读 P-UI-7〕/进度轮询三规则/
#     能力启用与反馈不静默/强杀独立确认/归档子标/当前性徽标搬运；零
#     Qt——模型层可测；公共契约头 include/sdurws/ird/ui/
#     ITaskPresentationModel.hpp 随 §16.7 v1.5 登记，C-8 呈现面端口
#     IUiTaskPresentationPort 消费面）。
# 不开启 AUTOMOC（UI-T03 增量登记，UI-T05/UI-T06 复核维持）：十五个源
# 文件与私有头零 Q_OBJECT 声明——壳与占位面板的 Qt 交互全部经虚函数覆盖
# （resizeEvent/keyPressEvent/eventFilter）与既有信号＋lambda 接线，无信号
# 槽声明需求（ui.md §10.1 v0.5 登记；命令面板键盘交互同款——v0.8 复核；
# 策略摘要卡同款复核——v0.9；参数表面板同款复核——v1.0；UiText/阶段导航
# 同款复核——v1.1；关于对话框同款复核——v1.2：纯 QLabel/QTableWidget
# 渲染＋工厂函数，零信号槽声明需求；会话控制器同款复核——v1.3：零 Qt
# 纯模型层，零 Widget 零信号槽需求；UI-T13 三实现 TU 同款复核——v1.5：
# Marshal 经 QMetaObject::invokeMethod 函子重载〔不需要 Q_OBJECT 元对象
# 声明〕，零信号槽需求）。
add_library(sdurws_ird_ui STATIC
    src/Ui.cpp
    src/WorkbenchShell.cpp
    src/ShellSupport.cpp
    src/StatusProjection.cpp
    src/View3DPlaceholder.cpp
    src/CommandRegistry.cpp
    src/GlobalShortcutRegistry.cpp
    src/CommandPalette.cpp
    src/PolicySummaryCard.cpp
    src/FormEditCommon.cpp
    src/ParamTablePanel.cpp
    src/UiText.cpp
    src/StageNavigationModel.cpp
    src/AboutDialog.cpp
    src/UiSessionController.cpp
    src/DraftController.cpp
    src/DiagnosticPresentation.cpp
    src/CommandInteractionBridge.cpp
    src/TaskPresentation.cpp)

# C++17（DTB §5.1/core.md D-01 同口径）：逐目标显式设定，不设全局标准。
# target_compile_features PUBLIC 传播——ui 公共头（UI-T03+）使用 C++17
# 设施时，消费者编译含 ui 头的翻译单元也必须至少 C++17。不用 C++20。
target_compile_features(sdurws_ird_ui PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_ui PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：UTF-8（无 BOM）＋中文注释（AGENTS.md §2 注释规范）。MSVC 默认
# 按系统代码页（GBK）解读无 BOM 源文件会把 UTF-8 中文误读成乱码 token
# （core 落位期实测 C2001），显式 /utf-8。PUBLIC 传播：公共头同为 UTF-8，
# 消费者编译含 ui 头的翻译单元需要同一解读。
if(MSVC)
    target_compile_options(sdurws_ird_ui PUBLIC /utf-8)
endif()

# 公共头根：仅 ui/include（内部含 sdurws/ird/ui/）。R-2 红线：私有实现头
# 不得置于 include/（§3.3：WorkbenchShell_p.hpp 等私有头只能放 src/）。
# 落位期该目录仅 README.md 保留位（头文件随 UI-T03+ 落地）。
target_include_directories(sdurws_ird_ui PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 编译链接依赖：core＋diagnostics 两条 ARCH §3.5 登记边（ui.md §3.1 依赖边
# 原文"ui → core, diagnostics"；ird_gates 白名单 "ui->core"/"ui->diagnostics"
# 既有行）。PUBLIC 形态：ui 公共头将消费 core 契约类型（§3.2 冻结引用清单
# ——ObjectId/DomainEvent/DiagnosticRecord 等），消费者经 ui 传递取得。
# 统一用真名 sdurws_ird_*：集成模式别名 RWS::ird::* 与真名并存，冒烟模式
# 仅真名（上级 add_subdirectory 在两种模式下都执行，真名恒可解析——
# project/execution 落位同款取舍）。Qt 三件套字面书写（不用变量——ird_gates
# 文本级解析前提，见上方 Qt 发现块说明）。除此之外零链接边：
#   - 零 project/evidence/execution/policy/runtime（O-31 处置——acceptance 5
#     边界澄清；协作经 L5 应用壳装配期注入，ui.md §3.1 依赖边原文）；
#   - 零业务域单元（R-1）、零 testkit（T-1——产品目标不链 testkit）；
#   - 零第三方新依赖（NFR-DEP-03——Qt 为 §3.1 登记的既有例外面）。
# 配置期守卫（下方"依赖红线守卫"块）对链接面做硬断言。
target_link_libraries(sdurws_ird_ui PUBLIC
    sdurws_ird_core
    sdurws_ird_diagnostics
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets)

# 别名（集成模式）：供 L5 应用壳/workflow（ARCH §3.5 入边单元——随其任务
# 落地）以 RWS::ird::ui 消费；冒烟模式无别名（与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::ui ALIAS sdurws_ird_ui)
endif()

# ---- 依赖红线守卫（配置期断言——O-31 的 ui 侧自律载体）----
# sdurws_ird_ui 的链接接口仅允许：core/diagnostics 两条 §3.5 登记边＋Qt6
# 三件套（R-3 例外承载面）。读取 LINK_LIBRARIES（含 PRIVATE 边）与
# INTERFACE_LINK_LIBRARIES（PUBLIC 边传染消费者）两面；属性未设置时
# get_target_property 返回 "<var>-NOTFOUND" 字面串，不匹配任何模式，不误报。
# 本守卫与 sdurws_ird_ui_test 的 O-31 include 面扫描用例互为两道防线
 # （目标链接面＋源码 include 面；正式全仓门禁归 ird_gates——WP-01-T01）。
get_target_property(_ird_ui_link_libs sdurws_ird_ui LINK_LIBRARIES)
get_target_property(_ird_ui_iface_libs sdurws_ird_ui INTERFACE_LINK_LIBRARIES)
set(_ird_ui_qt_seen "")
foreach(_lib IN LISTS _ird_ui_link_libs _ird_ui_iface_libs)
    # ① ird 单元目标白名单：仅 core/diagnostics（O-31——表外边即配置失败，
    #    含 testkit 与全部业务域单元；ird_gates SUB 白名单同口径）。
    if(_lib MATCHES "^sdurws_ird_" AND NOT _lib STREQUAL "sdurws_ird_core"
                                       AND NOT _lib STREQUAL "sdurws_ird_diagnostics")
        message(FATAL_ERROR
            "ui 依赖红线（O-31/R-1/T-1）：sdurws_ird_ui 仅可链接 core/diagnostics "
            "两条 ARCH §3.5 登记边（units/ui.md §3.1、任务契约 acceptance 5），"
            "发现 ${_lib}——对 project/evidence/execution/policy/runtime 的协作"
            "属 O-31 形态裁决范围，不得私裁增边")
    endif()
    # ② gtest 禁入产品库（D-06：断言宏只在消费方 _test 目标展开）。
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "ui 依赖红线（D-06）：产品库本体不链接 gtest，发现 ${_lib}")
    endif()
    # ③ Qt 链接面登记（R-3 例外范围核对）：记录出现的 Qt 模块，循环后断言
    #    三件套齐备——例外范围仅 sdurws_ird_ui（DTB §4.5"预登记（WP-10-T02
    #    生效）"行），多链/少链都属落位形态漂移。
    if(_lib MATCHES "^Qt[0-9]?::")
        list(APPEND _ird_ui_qt_seen "${_lib}")
    endif()
endforeach()
foreach(_need Qt6::Core Qt6::Gui Qt6::Widgets)
    list(FIND _ird_ui_qt_seen "${_need}" _qt_idx)
    if(_qt_idx EQUAL -1)
        message(FATAL_ERROR
            "ui 落位形态漂移：sdurws_ird_ui 缺少 ${_need}（units/ui.md §3.1 "
            "PUBLIC 链 Qt Core/Gui/Widgets——R-3 例外承载面，acceptance 1）")
    endif()
endforeach()

# =====================================================================
# 测试目标（随 UI-T02 登记；§12.1 三目标分层——不预建空目标，本批即首个
# 消费者落位。落位期用例＝最小构建冒烟用例（契约 acceptance 3 明文允许
# "实现任务未落地前允许最小构建冒烟用例，不在骨架预建占位实现"——父
# CMakeLists 既有约定，EX-T01 同款口径）；业务用例（命令注册表/快捷键/
# 七态映射/epoch 过滤/DraftController 协议与 GUI 契约）随 UI-T03~T14 落地）。
# =====================================================================
enable_testing()

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
# 沿用框架 ADD_RW_GTEST 宏语义（add_test＋<target>_report 运行并落 gtest
# XML），与 core/testkit/execution 等单元宏同款（不跨单元共享——NFR-MNT-04）。
# 报告输出于构建树本目录 gtest_reports/<target>.xml，任务留痕时拷贝入
# doc/industrial-robot-design/traceability/（AGENTS.md §4.2）。报告目标不进
# 默认构建（EXCLUDE_FROM_ALL）：跑测试是显式动作。
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

# ---- 测试目标 sdurws_ird_ui_test（§12.1 第一层：无界面模型测试）----
# QCoreApplication 级（test/TestMainReport.cpp 构造 QCoreApplication——无需
# GUI 平台插件，AGENTS 模型测试豁免/testkit.md §6.7 模型测试豁免行）；
# ctest LABELS ird（见下方 set_tests_properties）。用例面：
#   - BuildRedLineTest.cpp＝落位期红线扫描（O-31 include 面/R-4/QShortcut/
#     R-3 例外登记文本核对——UI-T02；UI-T06 起 QShortcut 扫描钉住唯一
#     创建点豁免面 GlobalShortcutRegistry.cpp）；
#   - ShellModelTest.cpp＝码表注册契约＋PM-11 格式矩阵（UI-T03）；
#   - StatusWordModelTest.cpp＝七态映射/优先级/显示纪律/九态标签键
#     （UI-STG-3——UI-T04，具名追溯字段见各用例 IRD_TEST_INFO）；
#   - View3DContractTest.cpp＝三维视图占位契约钉住：UX-11 九项清单逐项
#     字面冻结（序/id 唯一性/词法）＋KIN-06/AT-04 会话姿态语义四位钉住
#     （UI-T05 阶段 A——契约头 View3DContract.hpp 的互证锚）；
#   - CommandRegistryModelTest.cpp＝命令注册/未知拒绝/谓词/面板模糊投影
#     （UI-CMD-1/2/3、§7.4——UI-T06）；
#   - ShortcutRegistryModelTest.cpp＝快捷键冲突拒绝/改绑解绑/冲突流/持久
#     化回放（UI-HKY-1/2——UI-T06）；
#   - PolicySummaryCardTest.cpp＝策略摘要卡呈现契约：分组异名/逐字段行/
#     单位换算/显式不适用/未装载占位/聚合派生/编辑路径说明（UI-POL-1 模型
#     面——UI-T07）；
#   - FormEditModelTest.cpp＝表单公共件编辑规则：数值＋单位同显与切换
#     仅投影（KIN-12）/非法输入就地原因保留原值/表单级确认应用与错误
#     阻断（UX-07）/批量粘贴影响明细/筛选/错误定位/取消恢复/非模态
#     静态红线/高级面板字段集（UX-04）/装配契约 fail-fast（UI-T08）。
#   - StageNavigationModelTest.cpp＝七阶段状态投影矩阵与门控注入/导航
#     允许拒绝时序与就地提示/只读查看路径/StageStatusModel 单侧冻结汇聚/
#     装配 fail-fast/UiText 键解析与替换纪律与零内部名断言（UI-STG-1/2、
#     §6.5、N-11、UX-02——UI-T09）。
#   - AboutDialogModelTest.cpp＝静态白名单词表冻结（§11.1）/清单＝白名单∩
#     报告与交集丢弃/重复报告确定性防御/标题键族 UX-02 零 token 泄漏/
#     版本行与注入基线一致（NFR-DEP-05）/未装载占位/占位与 fail-fast 面/
#     手册入口路径部署布局（UI-PLG-2 模型半区——UI-T10）。
#   - SessionControllerModelTest.cpp＝会话状态机与关闭/切换对话框机制：
#     对话框数据装配与确认/取消/等待分支（UI-SES 系列机制面——§5.4 S1/
#     S2）、只读打开横幅含 PID 与 actionKind 逐场景区分（UI-SES-2/3——
#     PM-07/§5.3）、状态机迁移矩阵与 INV-SES-1/2/3 不变量（§5.2）、
#     Draining 四级防线与 T_force/T_force2 可配阈值（UI-SES-5/6——§5.6
#     P-UI-8）、迟到写提示不重试（UI-SES-7/UI-LCY-1——§5.3/§5.7）
#     （UI-T11 模型层）。
#   - DraftControllerModelTest.cpp＝草稿保存/恢复/冲突呈现/两栈（UI-T12
#     ——UI-DRF-1~4 模型面）。
#   - DiagPresentationModelTest.cpp＝诊断呈现模型：过滤/去重折叠/原因链
#     上溯/动作映射表冻结/呈现映射/三要素同显与四态占位（UI-DIA-1 模型
#     半区——§9.1/§10.8/UX-03/PM-15，UI-T13）。
#   - TaskPresentationModelTest.cpp＝任务呈现模型：进度三规则/清单双区
#     与当前置顶/后台只读自限/强杀独立确认/暂停反馈不静默/归档子标/
#     崩溃重跑/当前性徽标（UI-TSK-1~5 模型面——§9.4/TASK-01~03/P-UI-7，
#     UI-T13）。
#   - CommandInteractionBridgeTest.cpp＝确认交互桥：装配映射/批量决议/
#     拒绝不放行/拆除唤醒/失效标注/principal 缓存（UI-CMD-4~7 模型面
#     ——§9.2/§9.3/SA-15/P-UI-4/P-DIAG-6，UI-T13）。
add_executable(sdurws_ird_ui_test
    test/TestMainReport.cpp
    test/BuildRedLineTest.cpp
    test/ShellModelTest.cpp
    test/StatusWordModelTest.cpp
    test/View3DContractTest.cpp
    test/CommandRegistryModelTest.cpp
    test/ShortcutRegistryModelTest.cpp
    test/PolicySummaryCardTest.cpp
    test/FormEditModelTest.cpp
    test/StageNavigationModelTest.cpp
    test/AboutDialogModelTest.cpp
    test/SessionControllerModelTest.cpp
    test/DraftControllerModelTest.cpp
    test/DiagPresentationModelTest.cpp
    test/TaskPresentationModelTest.cpp
    test/CommandInteractionBridgeTest.cpp)

target_compile_features(sdurws_ird_ui_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_ui_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_ui_test PRIVATE /utf-8)
endif()

# 链接面：被测产品目标（core/diagnostics/Qt 经其 PUBLIC 传染链到达——两条
# 登记边的链接器半区实证）＋testkit（RecordListener 报告设施——T-1 允许
# 形态：仅测试目标链 testkit）＋GTest::gtest（main 自持，不再用 gtest_main）。
# testkit 直链的 SUB 命中属 F-221 同型既有登记（12 单元测试目标同形态，
# 机器面回填待 WP-01-T03——登记提交件 traceability/wp10-t02-gate-registrations.md）。
target_link_libraries(sdurws_ird_ui_test PRIVATE
    sdurws_ird_ui
    sdurws_ird_testkit
    GTest::gtest)

# 源码树位置注入（运行期红线扫描用例输入——execution/io 同款：值内以单元
# 名作子目录前缀，扫描面随单元自包含；两模式行为一致）。
target_compile_definitions(sdurws_ird_ui_test PRIVATE
    IRD_UI_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# vcpkg gtest 与 Qt6 均为动态库：拷贝运行期 DLL 到测试可执行文件输出目录，
# 保证不经 PATH 配置即可直接运行（留痕与验收都要直接执行该可执行文件；
# TARGET_RUNTIME_DLLS 需 CMake ≥3.21，本仓库 VS2022 工具链满足——execution
# 同款先例）。
add_custom_command(TARGET sdurws_ird_ui_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_ui_test>
            $<TARGET_FILE_DIR:sdurws_ird_ui_test>
    COMMAND_EXPAND_LISTS)

# ---- 契约测试目标 sdurws_ird_ui_contract_test（§12.1 第二层）----
# 跨单元契约面（§3.4 测试目标分工同 execution：_test＝红线扫描，
# _contract_test＝跨单元契约）。首批＝构建图边界契约
# （LinkageContractTest.cpp——O-31 链接面零表外边/T-1 测试侧/三目标与
# LABELS 登记）；UI-T11 增列 SessionContractTest.cpp＝§5 生命周期与
# project/execution 对接契约（契约 verify 行"ui_contract_test"的承接面：
# 会话端口协议〔requestClose 在途计数/closed 时序/subscribeClose 回调〕、
# 任务端口协作取消/强制终止逐任务可达、INV-SES 不变量、T_force/T_force2
# 默认保守值 120 s/300 s 冻结——全部以可控替身承载，O-31 测试侧惯例）。
# UI-T14 增列（本任务＝ui 契约测试套件）：
#   - PeerBridgeContractTest.cpp＝对端公共头桥接契约（§12.1 第二层行
#     "对接 project/execution/diagnostics 公共头与桩实现"）——L5 适配器
#     形态的测试侧镜像：adapter 实现对端接口/委托 ui 自有端口，以
#     testkit FaultInterceptor 注入故障（ITaskScheduler/ICommandInteraction
#     两缝——§12.1 行具名面）；对端类型形状漂移由本 TU 编译期捕获
#     （字段访问＋override＋成员函数指针 static_assert 三重钉）。
#   - Section12RegistryTest.cpp＝§12.3 用例清单逐例注册审计（11 个验收
#     承接族 WB/SES/DRF/CMD/HKY/STG/TSK/PLG/DIA/PERF/LCY 全用例编号
#     ＋UI-EXEC-1 执行限制登记——契约 acceptance 1 的具名自证面）。
add_executable(sdurws_ird_ui_contract_test
    contract_test/TestMainReport.cpp
    contract_test/LinkageContractTest.cpp
    contract_test/SessionContractTest.cpp
    contract_test/DraftContractTest.cpp
    contract_test/InteractionTaskContractTest.cpp
    contract_test/PeerBridgeContractTest.cpp
    contract_test/Section12RegistryTest.cpp)

target_compile_features(sdurws_ird_ui_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_ui_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_ui_contract_test PRIVATE /utf-8)
endif()

# 链接面：被测产品目标＋对端三单元公共头直链（O-31 裁决 2026-09-19 的
# 测试面承载——units/ui.md §3.1 该目标行 v0.4 原文"落位直链面；core/
# diagnostics 经被测目标 PUBLIC 传染可达；project/execution 直链随
# UI-T14 契约测试套件落地登记；测试目标链接面按本表承载，不属 ARCH
# §3.5 产品边管辖"，F-228 随之消解）：
#   - sdurws_ird_project：C-6 ICommandInteraction 对端形状（P-PR-7 冻结
#     点镜像的编译期承载）；
#   - sdurws_ird_execution：ITaskScheduler/TaskSnapshot 对端形状（§12.1
#     第二层行"testkit FaultInterceptor 伪造 ITaskScheduler"的对端半区）；
#   - sdurws_ird_diagnostics：RedactionPolicy/FindingRecord 值面（表内
#     登记边 ui→diagnostics 的测试侧直链——经被测目标 PUBLIC 传染亦可达，
#     直链为显式自证）；
#   - sdurws_ird_testkit：FaultInterceptor＋RecordListener（T-1 允许形态：
#     仅测试目标链 testkit）；GTest::gtest：main 自持。
# ★ 产品面守卫不变（O-31 常驻红线）：本链接面仅属本测试目标——产品库
#   sdurws_ird_ui 的链接块仍仅 core/diagnostics＋Qt 三件套（上方"依赖
#   红线守卫"块硬断言＋BuildRedLineTest 的 include 面扫描互为两道防线，
#   LinkageContractTest.ArchEdgesCoreDiagnosticsOnly 钉住产品链接块文本）。
target_link_libraries(sdurws_ird_ui_contract_test PRIVATE
    sdurws_ird_ui
    sdurws_ird_project
    sdurws_ird_execution
    sdurws_ird_diagnostics
    sdurws_ird_testkit
    GTest::gtest)

target_compile_definitions(sdurws_ird_ui_contract_test PRIVATE
    IRD_UI_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

add_custom_command(TARGET sdurws_ird_ui_contract_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_ui_contract_test>
            $<TARGET_FILE_DIR:sdurws_ird_ui_contract_test>
    COMMAND_EXPAND_LISTS)

# ---- GUI 测试目标 sdurws_ird_ui_gui_test（§12.1 第三层，单列）----
# Widgets/GUI 契约测试的注册载体。ctest LABELS ird_gui 串行（不与模型/Meta
# 测试合并命令——ui.md §12.2 执行纪律；testkit.md §6.7 单实例启动行）。
# UI-T03 起本目标承载真实 GUI 用例（WorkbenchShellGuiTest.cpp——UI-WB-1/2/3、
# UI-SES-1，§12.3）：main 升级为 QApplication 形态（BuildPlacementTest 的
# 落位期"不启动 GUI"钉子按其用例注释的既定授权同步拆除），执行纪律按
# §12.2 全文（VS x64 环境＋QT_QPA_PLATFORM=windows＋绝对路径单实例）。
# 仍链接 sdurws_ird_testkit_qt（Qt 测试辅助消费者形态——testkit.md §3.5）。
add_executable(sdurws_ird_ui_gui_test
    gui_test/TestMainReport.cpp
    gui_test/BuildPlacementTest.cpp
    gui_test/WorkbenchShellGuiTest.cpp
    gui_test/CommandPaletteGuiTest.cpp
    gui_test/PolicySummaryCardGuiTest.cpp
    gui_test/ParamTablePanelGuiTest.cpp
    gui_test/AboutDialogGuiTest.cpp)

target_compile_features(sdurws_ird_ui_gui_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_ui_gui_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_ui_gui_test PRIVATE /utf-8)
endif()

target_link_libraries(sdurws_ird_ui_gui_test PRIVATE
    sdurws_ird_ui
    sdurws_ird_testkit
    sdurws_ird_testkit_qt
    Qt6::Test
    GTest::gtest)

target_compile_definitions(sdurws_ird_ui_gui_test PRIVATE
    IRD_UI_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

add_custom_command(TARGET sdurws_ird_ui_gui_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_ui_gui_test>
            $<TARGET_FILE_DIR:sdurws_ird_ui_gui_test>
    COMMAND_EXPAND_LISTS)

# ---- ctest 标签登记（ui.md §12.1 表）----
# ird＝模型/契约测试（可与全仓 ird 标签并行调度）；ird_gui＝GUI 串行
# （CI 按 labels 过滤单实例执行——testkit.md §6.7，不用 RUN_SERIAL 属性：
# 设计文本钉住的是"串行标签"口径）。set_tests_properties 以 add_test 的
# NAME（与目标同名）精确登记，必须在 ird_add_gtest（add_test）之后调用
 # ——CMake 对未注册测试名直接报错（首次配置实测，调整次序）。
ird_add_gtest(sdurws_ird_ui_test)
ird_add_gtest(sdurws_ird_ui_contract_test)
ird_add_gtest(sdurws_ird_ui_gui_test)

set_tests_properties(sdurws_ird_ui_test sdurws_ird_ui_contract_test
    PROPERTIES LABELS "ird")
set_tests_properties(sdurws_ird_ui_gui_test
    PROPERTIES LABELS "ird_gui")

# ---- 开发验证工作台 harness：sdurws_ird_ui_app（UI-T15）----------------
# 建模开发期间的可视化验证手段：开发期 L5 应用壳雏形——真实诊断栈＋真实
# 工作台壳（createWorkbenchShell）＋真实项目打开协议（经 ui 自有端口适配
# project::ProjectStoreFactory，O-31 装配层特权边）。目标名启用引擎预留的
# _app 后缀形态（ird_gates 分类面 plugin/worker/app——Qt 链接合法）；harness
# 源码在 app/ 子目录（非 include/、src/ 产品面——产品扫描域零触碰）。
# 链接面＝装配层特权（R-1/R-3 表不适用；ui->project 表外边经 DTB §4.5
# 例外登记——UI-T15 契约与 traceability/wp10-t15-gate-registrations.md）。
# 本目标非测试（不入 ctest 标签）、非产品交付路径（产品装配层落位时按同
# 一契约替换；ui 库产品面零改动）。
add_executable(sdurws_ird_ui_app
    app/HarnessMain.cpp
    app/PortAdapters.cpp)

target_compile_features(sdurws_ird_ui_app PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_ui_app PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_ui_app PRIVATE /utf-8)
endif()

# sdurws_ird_project＝ui->project 表外边（DTB §4.5 登记的装配层例外——
# 打开五步协议适配的链接面；ui 库产品面仍零对 project 链接）。
target_link_libraries(sdurws_ird_ui_app PRIVATE
    sdurws_ird_ui
    sdurws_ird_project
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets)

add_custom_command(TARGET sdurws_ird_ui_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_ui_app>
            $<TARGET_FILE_DIR:sdurws_ird_ui_app>
    COMMAND_EXPAND_LISTS)
