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
# 不开启 AUTOMOC（UI-T03 增量登记，UI-T05/UI-T06 复核维持）：八个源文件与
# 私有头零 Q_OBJECT 声明——壳与占位面板的 Qt 交互全部经虚函数覆盖
# （resizeEvent/keyPressEvent/eventFilter）与既有信号＋lambda 接线，无信号
# 槽声明需求（ui.md §10.1 v0.5 登记；命令面板键盘交互同款——v0.8 复核）。
add_library(sdurws_ird_ui STATIC
    src/Ui.cpp
    src/WorkbenchShell.cpp
    src/ShellSupport.cpp
    src/StatusProjection.cpp
    src/View3DPlaceholder.cpp
    src/CommandRegistry.cpp
    src/GlobalShortcutRegistry.cpp
    src/CommandPalette.cpp)

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
#     化回放（UI-HKY-1/2——UI-T06）。
add_executable(sdurws_ird_ui_test
    test/TestMainReport.cpp
    test/BuildRedLineTest.cpp
    test/ShellModelTest.cpp
    test/StatusWordModelTest.cpp
    test/View3DContractTest.cpp
    test/CommandRegistryModelTest.cpp
    test/ShortcutRegistryModelTest.cpp)

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
# _contract_test＝构建图边界契约）。落位期首批＝构建图边界契约
# （LinkageContractTest.cpp——O-31 链接面零表外边/T-1 测试侧/三目标与
# LABELS 登记）；与 project/execution 的真实协作契约（§12.1 表"对接
# project/execution/diagnostics 公共头与桩实现"）属 O-31 形态裁决范围
# （UI-T14 契约测试套件kept planned——DOC-T13 契约 acceptance 3），本任务
# 不私裁落链接。
add_executable(sdurws_ird_ui_contract_test
    contract_test/TestMainReport.cpp
    contract_test/LinkageContractTest.cpp)

target_compile_features(sdurws_ird_ui_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_ui_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_ui_contract_test PRIVATE /utf-8)
endif()

# 链接面与 sdurws_ird_ui_test 同款（QCoreApplication 级 headless——ui.md
# §3.1 "带 Qt 事件循环 headless 用 QCoreApplication"）。
target_link_libraries(sdurws_ird_ui_contract_test PRIVATE
    sdurws_ird_ui
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
    gui_test/CommandPaletteGuiTest.cpp)

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
