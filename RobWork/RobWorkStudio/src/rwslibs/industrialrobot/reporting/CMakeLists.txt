# =====================================================================
# reporting 单元（评审报告平台服务：ReviewReport 对象/HTML·JSON·CSV 渲染/
# 证据包导出/往返复算）构建脚本
#
# 设计依据：
#   - units/reporting.md §3.4（命名空间/目标与 CMake 集成：骨架 INTERFACE →
#     RPT-T01 升级 STATIC；别名 RWS::ird::reporting；target_compile_features
#     cxx_std_17；"target_link_libraries(sdurws_ird_reporting PUBLIC
#     RWS::ird::core RWS::ird::evidence RWS::ird::diagnostics
#     RWS::ird::project)"——四条 ARCH §3.5 登记边全部落链接；`_test`/
#     `_contract_test` 随 RPT-T01 登记，gtest 按 DTB §5.5 接入；头包含形式
#     #include <sdurws/ird/reporting/Xxx.hpp>，私有实现头不入 include/
#     〔R-2〕；产品目标不链 testkit〔T-1〕）、§3.2（依赖图：四条登记单元边＋
#     C++17 标准库——零 Qt 含 Core〔D-01：报告渲染为纯字节生成〕、零 io/
#     runtime/execution/业务单元编译边〔ARCH §3.5 未登记——P-RPT-1/P-RPT-2
#     注入形态〕、零 testkit〔T-1〕、零 Eigen/rw::math 直接包含）、§1.4
#     （语言与构建约束：显式 cxx_std_17 不用 C++20、零 Qt 含 Core、零
#     rw::math/Eigen 直含、不引入第二套编码器——SA-12）、§11 RPT-T01 行
#     （产物：STATIC＋注册 `_test`/`_contract_test`；涉及文件
#     `reporting/CMakeLists.txt`（新）、`src/`（空起步）；验证方式：独立
#     冒烟＋集成双模式构建＋RP-GATE-1；完成条件：两模式零错误＋红线扫描
#     零命中）
#   - development-task-breakdown.md §5.1（双模式/目标命名 INTERFACE→真实库
#     升级时目标名不变/C++17 不用 C++20）、§5.3（全局禁止项）、§5.5
#     （gtest 定稿：vcpkg＋find_package(GTest CONFIG REQUIRED)，失败即停，
#     不消费 RW::gtest、不源码 vendor、测试 main 自持）
#   - 需求 NFR-MNT-01（计算内核零 Qt）、NFR-MNT-02（单元边界——无私有头
#     出 include）；红线 R-1（业务单元互链禁止）/R-2（跨单元私有头禁止）/
#     T-1（产品目标不链 testkit）
#
# 本文件随 RPT-T01（≙WP-12-T02，任务契约 tasks/foundation/RPT-T01.json）
# 落地，先例：project/CMakeLists.txt（PRJ-T01 同款落位形态）、execution/
# CMakeLists.txt（EX-T01 同款测试目标分工）：
#   1. 骨架期 INTERFACE 占位升级为 STATIC 库（目标名 sdurws_ird_reporting
#      不变，别名 RWS::ird::reporting 不变，公共 include 根不变——DTB
#      §5.1"INTERFACE→真实库升级时目标名不变"）；
#   2. 注册 gtest 测试目标 sdurws_ird_reporting_test（单元内红线扫描；
#      ScriptedSectionProvider/ScriptedResultSource 替身随 RPT-T11 落位于
#      test/——§11 RPT-T11 行）与 sdurws_ird_reporting_contract_test（跨
#      单元契约：与 core/evidence 值类型往返、与 project 工件汇集座
#      〔fake IReportArtifactSink〕、与 io 写出设施〔注入 fake〕的协作
#      随 RPT-T09/T11 落地）；落位期首批用例＝最小构建冒烟用例（契约
#      acceptance 3 明文允许，PRJ-T01 同款口径），业务用例随 RPT-T02~
#      T12 落地，不在骨架预建占位实现——父 CMakeLists 既有约定；
#   3. src/ 空起步＝锚点翻译单元 Reporting.cpp（§11 RPT-T01 行"src/（空
#      起步）"的既定先例形态——CORE-T01/POL-T01/DIAG-T02/PRJ-T01/EX-T01
#      同款：CMake STATIC 库至少一个翻译单元纪律，无任何接口预建——
#      NFR-MNT-04）；
#   4. 陷阱处置：P-ENV-1——目标显式 cxx_std_17、不用 C++20，双模式构建即
#      C++17 与框架 C++11 基线混链验证（governance-log P-ENV-1 已随
#      CORE-T01 关闭〔F-003〕，本任务按同口径复证并留痕）；P-RPT-1——
#      io 写出能力不落编译边：ARCH §3.5 未登记 reporting→io，CSV/JSON
#      写出与原子目标经注入的 IReportIoFactory（§9.5——公共头零 io 类型）
#      消费，P-RPT-1/P-IO-1/P-PR-5 合并裁决前维持注入、届时签名零改动
#      直连；P-RPT-9——core/evidence/diagnostics/project 各卡 v0.1 仍
#      Draft 未冻结，本任务**仅建立四条登记链接边、零上游公共头消费**
#      （产品源码与测试源码均不 include 任何 sdurws/ird/<他单元> 头；
#      测试用例对 include 面的扫描即该处置的常驻自证），各卡冻结出
#      diff 后按影响面增量同步留痕。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制）----
# gtest 经 vcpkg 安装（gtest:x64-windows，经典模式）；REQUIRED＝找不到即
# 配置失败（失败即停，不静默跳过测试目标——CR-07 定稿机制）。不消费框架
# 的 USE_gtest/RW::gtest（从未生成），也不源码 vendor——两份 gtest 混用
# 会破坏 ODR。
find_package(GTest CONFIG REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC（目标名不变，DTB §5.1）----
# src/Reporting.cpp＝落位期锚点翻译单元（§11 RPT-T01 行"src/（空起步）"；
# STATIC 库至少一个翻译单元纪律——core/src/Core.cpp、project/src/Project.cpp
# 等同先例，零源码 STATIC 在 CMake 生成期即报"No SOURCES given to target"，
# 实测）。RPT-T02+ 按本卡 §11 逐任务增列同名任务文件（落位安排登记于
# Reporting.cpp 文件头），锚点文件届时或承载共享工具或拆除。
# RPT-T02 增列（§11 RPT-T02 行产物：Errors.hpp/.cpp、Identity.hpp＋实现）：
#   - src/Errors.cpp——ReportErrorCode token 全表＋ReportError 消息拼装＋
#     RPT-* 建议码映射（§3.5）；
#   - src/Identity.cpp——ReportId 解析/格式化/生成（自建 Id128 实现，
#     P-RPT-3）＋ReportLevel token 表（§4.1/§4.2）。
# RPT-T03 增列（§11 RPT-T03 行产物：ReportModel.hpp/.cpp〔全部引用类型/
#   字段校验〕＋ReportCodec〔Data/Full 双编码〕）：
#   - src/ReportModel.cpp——SectionStatus/RenderHint/QualifierToken token
#     表（§5.3/§9.2/§6.4）、§4.2 非法实例字段校验原语
#     firstFieldViolation（非抛出判定核心）、ReviewReport::make 唯一
#     生产者（字段校验→ReportCodec 身份计算→冻结构造——调用方不可申报）；
#   - src/ReportCodec.cpp——确定性二进制双编码（magic IRDRPT1/IRDRPTD1＋
#     字段按名序/章节按 order＋长度前缀＋大端＋presence 字节＋UTF-8 禁
#     NUL＋浮点 IEEE754 位模式、NaN/±Inf 编码入口拒绝——§4.4 规则全条；
#     SHA-256 经 core ContentDigester 唯一算法）。私有头 src/ReportCodec.hpp
#     由 ReportModel.cpp 与本单元测试经相对路径消费（单元内消费先例：
#     runtime DescriptionValidator——跨单元私有头禁止不变〔R-2〕）。
# RPT-T04 增列（§11 RPT-T04 行产物：Sections.hpp/.cpp〔§5 词表/级别契约〕＋
#   SectionProvider.hpp〔§9.2 接口＋注册表，header-only——§11 产物列仅列
#   头文件，注册表小体量 inline 落位〕）：
#   - src/Sections.cpp——§5.1 词表 14 项查找/级别归属、LevelConflict 与
#     ScopeInsufficient 判定原语、§5 默认选择规则、RPT-SCOPE-INSUFFICIENT
#     诊断挂接（P-RPT-5 保守处置——§5.4）；
#   - include/sdurws/ird/reporting/SectionProvider.hpp——SectionRequest/
#     SectionContent（消费 ReportModel 冻结视图族——v0.6 偏差登记，零重复
#     定义）＋IReportSectionProvider 纯投影接口＋SectionRegistry（注册边界
#     五类拒绝/运行期 find 并发只读/registeredSections 字典序稳定）＋
#     SectionContent 结构自检原语（§5.3 presence 纪律的上下文无关判定核心，
#     RPT-T05 构建器复用）。
add_library(sdurws_ird_reporting STATIC
    src/Reporting.cpp
    src/Errors.cpp
    src/Identity.cpp
    src/ReportModel.cpp
    src/ReportCodec.cpp
    src/Sections.cpp
    src/Builder.cpp
    src/Render.cpp)

# C++17（单元卡 §1.4/core.md D-01 同源；P-ENV-1 处置）：逐目标显式设定，
# 不设全局标准。target_compile_features PUBLIC 传播——reporting 公共头将
# 使用 std::optional/variant/string_view 等 C++17 设施（§1.4），消费者编译
# 含 reporting 头的翻译单元也必须至少 C++17。不用 C++20（DTB §5.1）。
target_compile_features(sdurws_ird_reporting PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_reporting PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：本单元源文件全部为 UTF-8（无 BOM）且含中文注释（AGENTS.md §2
# 注释规范）。MSVC 默认按系统代码页（GBK）解读无 BOM 源文件，会把 UTF-8
# 中文误读成乱码 token（core 落位时曾出现 C2001"常量中有换行符"），必须
# 显式 /utf-8。PUBLIC 传播：公共头同为 UTF-8，消费者编译含 reporting 头的
# 翻译单元也需要。
if(MSVC)
    target_compile_options(sdurws_ird_reporting PUBLIC /utf-8)
endif()

# 公共头根：仅 reporting/include（内部含 sdurws/ird/reporting/）。R-2 红线：
# 私有实现头不得置于 include/——本目录是 reporting 唯一对外的头暴露位，
# 实现细节头只能放 src/ 并以相对路径被实现文件包含（§3.4 布局的构建面
# 表达）。当前为保留位期（仅 README.md——落位复核结论见该文件 2026-09-19
# 行），契约头 Errors.hpp 等随 RPT-T02+ 落地（§3.1 组成表）。
target_include_directories(sdurws_ird_reporting PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 编译链接依赖：core/evidence/diagnostics/project 四条 ARCH §3.5 登记边
# （§3.4 原文 PUBLIC 形态逐条落链接；ird_gates 白名单 "reporting->core"/
# "reporting->evidence"/"reporting->diagnostics"/"reporting->project" 行；
# alias 形态 RWS::ird::* 在集成模式即其 ALIAS，此处用统一真名 sdurws_ird_*：
# 集成与独立冒烟两种模式下都可解析——project 落位同款取舍）。
# 与 execution 落位期的差异：execution 的 diagnostics 边按注入形态暂不落
# 链接（P-EX-8），而 reporting 卡 §3.4 原文即四边全链（diagnostics 消费面
# ＝exportSafeSummary/DiagProjectionItem/StableCodeRegistry 只读——§3.2，
# 消费点随 RPT-T02+ 错误与诊断引用类型落地）。
# P-RPT-1 处置：零 io 编译边（CSV/JSON 写出/原子目标经注入的 IReportIoFactory
# ——§9.5，公共头零 io 类型；P-RPT-1/P-IO-1/P-PR-5 合并裁决前维持注入，
# 届时签名零改动直连）；P-RPT-2 处置：零 runtime 编译边（模型摘要经注入的
# IModelSummaryProvider——§9.7/§3.3，返回 reporting 自有 ModelSummary 投影
# 值）；零 execution 编译边（任务投影经注入的 ITaskStatusSource——§3.3；
# execution 与 reporting 同层且 execution 依赖面更宽，反向边无必要）。
# 零 Qt（D-01 零 Qt 含 Core——比 L3 层规则上限更严）、零业务域单元（R-1）、
# 零 testkit（T-1——产品目标不链 testkit）、零 Eigen/rw::math 直接包含
# （§1.4：数值经 SourcedValue/Quantity 承载，几何类型零出现）。
target_link_libraries(sdurws_ird_reporting PUBLIC
    sdurws_ird_core
    sdurws_ird_evidence
    sdurws_ird_diagnostics
    sdurws_ird_project)

# 别名（集成模式）：供消费方单元（ui/workflow 等——ARCH §3.5 入边单元）
# 以 RWS::ird::reporting 消费；冒烟模式无别名（与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::reporting ALIAS sdurws_ird_reporting)
endif()

# ---- 测试目标 sdurws_ird_reporting_test（随 RPT-T01 登记）----
# 首批用例＝最小构建冒烟用例（契约 acceptance 3 明文允许，PRJ-T01 同款
# 口径）：落位期红线扫描（RP-GATE-1 的单元内源码面：零 Qt 含 Core、公共头
# 路径布局无私有头出 include、零 Eigen/rw::math 直含、include 面
# sdurws/ird/* 白名单＝{core, evidence, diagnostics, project, reporting}）。
# 运行期源码扫描与文件末尾配置期守卫互为两道防线（project 落位同款）。
# RPT-T02+ 用例按 §11 各 RPT-TX 组增列；ScriptedSectionProvider/
# ScriptedResultSource 替身与 RP-* 用例体随 RPT-T11 落位于 test/
# （§11 RPT-T11 行"reporting/test/*"——替身落位目录）。
enable_testing()

add_executable(sdurws_ird_reporting_test
    test/BuildRedLineTest.cpp
    test/ErrorsIdentityTest.cpp
    test/ReportModelTest.cpp
    test/SectionsTest.cpp
    test/SectionProviderTest.cpp
    test/BuilderTest.cpp
    test/RenderTest.cpp)

target_compile_features(sdurws_ird_reporting_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_reporting_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（/utf-8 说明见产品目标处；gtest 头为
# ASCII 不受影响）。PRIVATE 显式再设一次以自洽——测试目标应独立可读，
# 不依赖被测目标的编译选项细节。
if(MSVC)
    target_compile_options(sdurws_ird_reporting_test PRIVATE /utf-8)
endif()

# 测试 main 自持（DTB §5.5/P-TK-1 定稿）：落位期以 GTest::gtest_main 提供
# 入口（PRJ-T01 同款最小形态；自有 main＋testkit RecordListener 的
# ird-test-report.json 聚合随 RPT-T11 契约套件按 T-1 允许形态登记）。
# 测试链接产品库 sdurws_ird_reporting 同时验证"公共头可被正常 include＋库
# 可链接"（reporting→core/evidence/diagnostics/project 四条登记边的 PUBLIC
# 传染链随目标自动到达——链接器半区实证）。
target_link_libraries(sdurws_ird_reporting_test PRIVATE
    sdurws_ird_reporting
    GTest::gtest_main)

# 源码树位置注入：落位期用例在运行期扫描 reporting 源码核对红线。经编译
# 定义传 industrialrobot 根（project/diagnostics/evidence/runtime/execution
# 同款——值内以单元名作子目录前缀，扫描面随单元自包含）。
target_compile_definitions(sdurws_ird_reporting_test PRIVATE
    IRD_REPORTING_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# vcpkg 的 gtest:x64-windows 是动态库（gtest.dll/gtest_main.dll）：把测试
# 可执行文件的运行期 DLL 拷到其输出目录，保证不经 PATH 配置即可直接运行
# （留痕与验收都要直接执行该可执行文件）。TARGET_RUNTIME_DLLS 需 CMake
# ≥3.21，本仓库工具链为 VS2022 自带的更新版本，满足。
add_custom_command(TARGET sdurws_ird_reporting_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_reporting_test>
            $<TARGET_FILE_DIR:sdurws_ird_reporting_test>
    COMMAND_EXPAND_LISTS)

# ---- 跨单元契约测试目标 sdurws_ird_reporting_contract_test（随 RPT-T01 登记）----
# §3.4 测试目标分工：`_contract_test`＝跨单元契约面。RPT-T01 首批＝构建图
# 边界契约（四条登记边必须真实存在＋零表外同层边＋零 testkit 边＋零 io/
# runtime/execution 编译边——P-RPT-1/P-RPT-2 注入形态的具名自证）；
# RPT-T02 增列 contract_test/IdentityCrossUnitContractTest.cpp＝首个上游头
# 消费契约（core::ContentIdentity/ContentDigester 摘要唯一算法与字节等值
# 消费、diagnostics StableCodeRegistry 的 RPT-* 收编一致性——§3.2 C-1/C-3
# 消费行，P-RPT-9 基线＝core.md v0.1 Draft）；与 project 工件汇集座
# 〔fake IReportArtifactSink〕、与 io 写出设施〔注入 fake〕的协作随 RPT-T09/
# T11 落地于 contract_test/（§11 RPT-T11 行），届时按 T-1 允许形态
# 登记 testkit 消费并同步增量修订本文件白名单扫描用例（不可静默越界）。
add_executable(sdurws_ird_reporting_contract_test
    contract_test/LinkageContractTest.cpp
    contract_test/IdentityCrossUnitContractTest.cpp
    contract_test/EvidenceCrossUnitContractTest.cpp
    contract_test/SectionCrossUnitContractTest.cpp
    contract_test/BuilderCrossUnitContractTest.cpp
    contract_test/RenderCrossUnitContractTest.cpp)

target_compile_features(sdurws_ird_reporting_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_reporting_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_reporting_contract_test PRIVATE /utf-8)
endif()

# 链接面与 _test 目标同款（GTest::gtest_main 自持入口——落位期最小形态）。
# 不显式链 core/evidence/diagnostics/project——PUBLIC 经 sdurws_ird_reporting
# 传染（四条登记边的链接器半区）；落位期零上游头消费（P-RPT-9），传染链
# 当前仅为构建图存在性证明。
target_link_libraries(sdurws_ird_reporting_contract_test PRIVATE
    sdurws_ird_reporting
    GTest::gtest_main)

# 源码树位置注入（构建图边扫描用例输入——与 _test 目标同源）。
target_compile_definitions(sdurws_ird_reporting_contract_test PRIVATE
    IRD_REPORTING_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

add_custom_command(TARGET sdurws_ird_reporting_contract_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_reporting_contract_test>
            $<TARGET_FILE_DIR:sdurws_ird_reporting_contract_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
# 框架原宏见 RobWork/RobWork/gtest/CMakeLists.txt：add_test ＋ <target>_
# report（运行测试并落 gtest XML）。本宏按 DTB §5.5 在本单元 CMake 内
# 定义（定义先于全部调用——CMake 顺序解析）；不做跨单元共享（reporting
# 是平台服务单元，不是构建工具设施提供方——NFR-MNT-04）。报告输出于
# 构建树本目录 gtest_reports/<target>.xml；任务留痕时拷贝入 doc/
# industrial-robot-design/traceability/（AGENTS.md §4.2 验证留痕）。报告
# 目标不进默认构建（EXCLUDE_FROM_ALL）：跑测试是显式动作，与框架"不随
# ordinary build 执行测试"的约定一致。
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

ird_add_gtest(sdurws_ird_reporting_test)
ird_add_gtest(sdurws_ird_reporting_contract_test)

# ---- 依赖红线守卫（配置期断言——RP-GATE-1 的 CMake 依赖图半区）----
# reporting 的 industrialrobot 单元依赖仅限 core/evidence/diagnostics/project
# 四条 ARCH §3.5 登记边（本卡 §3.2/§3.4；ird_gates 白名单 "reporting->core"/
# "reporting->evidence"/"reporting->diagnostics"/"reporting->project" 行）；
# 零 Qt（NFR-MNT-01/D-01，比 L3 上限更严——含 Core 亦禁）；产品库本体零
# gtest（断言宏只在消费方 _test 目标展开——project D-06 同款）。
# ^sdurws_ird_ 通配同时拦截 testkit（sdurws_ird_testkit ≠ 四条登记边即
# FATAL——T-1 产品目标不链 testkit）与 io/runtime/execution/业务域单元
# （P-RPT-1/P-RPT-2/R-1——表外边＝构建失败，SA-10）。两面读取
# （LINK_LIBRARIES 含 PRIVATE 边＋INTERFACE_LINK_LIBRARIES 含 PUBLIC 边）
# 防漏。属性未设置时 get_target_property 返回 "<var>-NOTFOUND" 字面串，
# 不匹配任何模式，不会误报。正式全仓门禁（依赖图白名单）归 ird_gates
# （WP-01-T01），本守卫不替代它。
get_target_property(_ird_rpt_link_libs sdurws_ird_reporting LINK_LIBRARIES)
get_target_property(_ird_rpt_iface_libs sdurws_ird_reporting INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_rpt_link_libs _ird_rpt_iface_libs)
    if(_lib MATCHES "^sdurws_ird_" AND NOT _lib STREQUAL "sdurws_ird_core"
                                       AND NOT _lib STREQUAL "sdurws_ird_evidence"
                                       AND NOT _lib STREQUAL "sdurws_ird_diagnostics"
                                       AND NOT _lib STREQUAL "sdurws_ird_project")
        message(FATAL_ERROR
            "reporting 依赖红线（R-1/T-1/P-RPT-1/P-RPT-2）：sdurws_ird_reporting "
            "仅可链接 core/evidence/diagnostics/project 四条登记边（units/"
            "reporting.md §3.2/§3.4、ARCH §3.5；io/runtime/execution 经注入"
            "消费、零编译边），发现 ${_lib}")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "reporting 依赖红线（NFR-MNT-01/D-01 零 Qt 含 Core）：发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "reporting 依赖红线（产品库本体不链接 gtest）：发现 ${_lib}")
    endif()
endforeach()
