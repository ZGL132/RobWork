# =====================================================================
# dynamics 单元（业务域：动力学评估——逆动力学〔RNEA〕/正动力学一致性/
# 峰值 RMS 包络/可信等级）构建脚本
#
# 设计依据：
#   - units/dynamics.md §3.1（依赖形态——计算库 L2 零 Qt；接口依赖
#     core/evidence/runtime/execution/project/diagnostics 公共头；L1 基线
#     链接集 sdurw_math/kinematics/models＋sdurwsim＋Eigen3；R-1 业务域
#     互链禁止——trajectory/requirements/drivetrain/selection 零编译期
#     依赖）、§3.2（目标布局 CMake 行——WP-17-T02：INTERFACE→STATIC
#     ＋L1 基线＋_test/_contract_test 注册＋配置期红线守卫；_plugin/
#     plugin/ 目录）、§3.3（不创建 sdurws_ird_dynamics_worker——共享
#     worker 唯一，D-DYN-10）、§9.4（DYN-* 15 码登记表——装配期注册）、
#     §9.5（插件零计算逻辑——界面面随 WP-17-T09）
#   - development-task-breakdown.md §5.1（双模式/目标命名/C++17 不用
#     C++20）、§5.3（全局禁止项）、§5.5（gtest 接入定稿）、§4.6（L1 基线
#     表——sdurwsim 消费方＝runtime、dynamics〔WP-17-T02〕）
#   - ARCHITECTURE.md §3.3（业务域单元二分结构——零 Qt 计算库＋Qt Widgets
#     插件）、§3.5（依赖表——各业务域单元→L2/L3 公共接口的许可方向；
#     五边为其实例化，登记于 cmake/ird_gates_whitelist.cmake 的
#     "dynamics->…" 五行；O-07 的 dynamics 侧承接＝本卡 §3.1/§3.2 明文
#     runtime 接口依赖）
#   - 需求 NFR-MNT-01（计算内核零 Qt——dynamics 为 L2 等效业务计算库；
#     DYN-01/05 经 RobWorkSim 消费＝链 rwsim 零 Qt 基线）
#   - 任务契约 tasks/foundation/WP-17-T02.json（本文件即其 acceptance
#     1/2 的落地面：INTERFACE 占位→STATIC＋_plugin/_test/_contract_test
#     同批创建＋DYN-* 域诊断码装配期注册＋二分结构扫描）
#
# 本文件随 WP-17-T02 落地（dynamics 由上级 INTERFACE 占位升级为真实库）：
#   1. sdurws_ird_dynamics STATIC（目标名与别名 RWS::ird::dynamics 不变，
#      DTB §5.1）；T02 源码集＝src/DiagCodes.cpp——DYN-* 15 码描述符清单
#      ＋装配注册函数（§9.4 登记表物化；本单元依赖白名单含 diagnostics
#      编译边，kinematics WP-15-T02 注册函数形态适用——与 selection/
#      drivetrain 的"纯登记表、注册归 L5 装配"形态不同，差异来自各自卡
#      §3.2 边表有无 diagnostics 编译边，诚实登记非形态漂移）。T03+ 源码
#      集（DynTypes/Errors/DynConfig/InverseDynamics/ForwardDynamics/
#      SeriesBuilder/Envelope/PowerEnergy/Evidence/Evaluators/
#      TrajectorySourcePort/Commands/Handoff——卡 §3.2 布局表）随各自
#      任务增列，本任务不预建占位（NFR-MNT-04）；
#   2. 插件界面目标 sdurws_ird_dynamics_plugin（T02 即落位——契约
#      acceptance 1 明文创建）：最小可注册实现＝assembly/ 自持描述符
#      门面（pluginId/titleKey 两字段均有 ui.md §11.1/§3.5 登记出处），
#      零业务计算逻辑（卡 §9.5"UI 线程不得执行动力学计算"——契约测试
#      词表扫描钉住）。界面面（曲线联动/峰值定位/时刻回放面板与
#      dynamics.* 命令族注册）随 WP-17-T09；
#   3. 测试目标 sdurws_ird_dynamics_test 与
#      sdurws_ird_dynamics_contract_test（自持 ird_add_gtest 宏：
#      add_test＋<target>_report XML——core/io/kinematics/selection 同款；
#      ctest LABELS "ird"）；
#   4. 配置期依赖红线守卫（R-1/R-3 的 dynamics 侧断言——卡 §3.2；全仓
#      正式门禁归 ird_gates/WP-01-T01，本守卫不替代它）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制；随本任务首个测试目标引入）----
# gtest 经 vcpkg 安装（gtest:x64-windows，经典模式）；REQUIRED＝找不到即
# 配置失败（失败即停，不静默跳过测试目标）。不消费框架的 USE_gtest/RW::
# gtest（从未生成），也不源码 vendor——两份 gtest 混用会破坏 ODR。
# 独立冒烟模式必须带 vcpkg toolchain 参数方可解析（CORE-T01 G-2 口径）。
find_package(GTest CONFIG REQUIRED)

# ---- Qt6 接入（WP-17-T02——插件目标的 Qt 载体面，R-3 唯一例外面）----
# 仅插件目标消费 Qt（卡 §3.2 二分结构"零 Qt 计算库＋Qt Widgets 插件"；
# selection/workflow T02 同款先例）。组件＝Core/Gui/Widgets（Widgets 是
# WP-17-T09 曲线联动/峰值定位/回放面板的本体、Gui/Core 为 Qt 基面；不取
# Test——本单元无 GUI 测试目标，GUI 手动验证通道随 WP-17-T09——卡 §11.5
# 明文本批不启动 GUI）。REQUIRED 同 gtest：找不到即配置失败（不静默跳过
# 插件目标）。独立冒烟模式必须带 CMAKE_PREFIX_PATH 指向 Qt 安装（编排
# 口径：Qt6 位于 D:/software/Qt/6.11.1/msvc2022_64）。计算库
# sdurws_ird_dynamics 保持零 Qt——R-3/NFR-MNT-01：产品面（include/＋
# src/）源码零 Qt 包含、链接面零 Qt 目标（BuildRedLineTest 与配置期
# 守卫双面钉住）。
find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC（目标名不变，DTB §5.1）----
# T02 源码集＝src/DiagCodes.cpp（DYN-* 15 码登记表——CodeDescriptor 描述
# 符清单＋装配注册函数的唯一实现翻译单元；逐码落值依据见该文件逐码注释）。
# WP-17-T03 增列 src/InverseDynamics.cpp（RNEA 逆动力学评估器实现——卡
# §3.2 布局表 InverseDynamics.hpp 行：模型提取/末端体合成/几何与力学双
# 递推/摩擦叠加/样本行组装；自持 Vec3/Mat3 逐元素算术——零 rw 外联符号，
# 冒烟 header-only 纪律同 runtime RT-T03 先例）。
# WP-17-T04 增列三实现 TU（卡 §3.2 布局表 SeriesBuilder/Envelope/
# PowerEnergy 行——输出序列与峰值/RMS 统计面）：
#   src/SeriesBuilder.cpp  §10.3 序列构建（排序/完整性/身份冻结/canonical
#                          内容身份——SHA-256 编码唯一实现点）；
#   src/Envelope.cpp       §10.4 统计面（工况级峰值含持续时间窗与所在段
#                          ＋完整循环时间加权 RMS 含驻留；mergeEnvelope
#                          多工况包络合并归 WP-17-T07——DTB §2.18 分界）；
#   src/PowerEnergy.cpp    §10.5 功率/能量（§7.5 符号约定唯一实现点——
#                          E⁺/E⁻ 分项梯形积分含驻留）。
# 正动力学/证据装配等实现翻译单元随 WP-17-T05~T10 按卡 §3.2 布局表增列
# （ForwardDynamics/Evidence/…——不预建占位实现，NFR-MNT-04）。
add_library(sdurws_ird_dynamics STATIC
    src/DiagCodes.cpp
    src/InverseDynamics.cpp
    src/SeriesBuilder.cpp
    src/Envelope.cpp
    src/PowerEnergy.cpp)

# C++17（core.md 决策 D-01 同源）：逐目标显式设定，不设全局标准。
# PUBLIC 传播：dynamics 公共头使用 C++17 设施（inline constexpr 变量
# 等），消费者编译 dynamics 头也必须至少 C++17。
target_compile_features(sdurws_ird_dynamics PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_dynamics PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：本单元源文件全部为 UTF-8（无 BOM）且含中文注释（AGENTS.md §2
# 注释规范）。MSVC 默认按系统代码页（GBK）解读无 BOM 源文件，会把 UTF-8
# 中文误读成乱码 token（core 落位时曾出现 C2001"常量中有换行符"），必须
# 显式 /utf-8。PUBLIC 传播：公共头同为 UTF-8，消费者编译含 dynamics
# 头的翻译单元也需要。
if(MSVC)
    target_compile_options(sdurws_ird_dynamics PUBLIC /utf-8)
endif()

# 公共头根：仅 dynamics/include（内部含 sdurws/ird/dynamics/）。T02
# 公共面＝DiagCodes.hpp（DYN-* 15 码登记表——契约 acceptance 2）；卡
# §3.2 布局表十三头（DynTypes/Errors/DynConfig/InverseDynamics/…）随
# T03+ 任务增列。R-2 红线：私有实现头不得置于 include/——本目录是
# dynamics 唯一对外的头暴露位，实现细节头（若有）只能放 src/ 并以相对
# 路径被实现文件包含。
target_include_directories(sdurws_ird_dynamics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 编译依赖：core＋evidence＋runtime＋execution＋diagnostics（卡 §3.2
# CMake 行边表五条登记边——ARCH §3.5"各业务域单元→L2/L3 公共接口"许可
# 方向的实例化，ird_gates_whitelist.cmake 的 "dynamics->…" 五行随本任务
# 登记；O-07 的 dynamics 侧承接＝卡 §3.1/§3.2 明文 runtime 接口依赖——
# runtime 视图（DWC/gravityBase/makeState）是 RNEA 输入面，WP-17-T03 起
# 消费）。
# PUBLIC 形态（与卡面 "PRIVATE 链" 字样的差异——诚实登记，非形态漂移）：
# DiagCodes.hpp 内嵌 diagnostics 承载的 CodeDescriptor/IDiagnosticRegistry
# 契约类型（其头又内嵌 core::DiagCode——消费方必须随链到达），PRIVATE
# 链会使测试目标/下游单元编译 dynamics 头时 core/diagnostics 头不可达，
# 而测试目标直链他单元产品目标即 ird_gates SUB——kinematics/selection/
# modeling 等全部已落位单元同以 PUBLIC 建边（P-RPT-9 同款先例：边随落位
# 登记，消费随 T03+ 任务回填——evidence IEngineeringEvaluator/EvaluationRequest
# 契约→T03 评估器面、execution 任务提交/取消令牌/检查点→T05 批量通道、
# core ObjectId/ContentIdentity/SourcedValue/单位→T03/T04 数据模型面；
# 五边统一 PUBLIC 保持形态一致——契约 acceptance 1 依赖面全建立）。
# project 边本任务不落（卡 §3.1 提及 project 公共头，但 §3.2 CMake 行
# 边表未列＋T02 零 project 公共值类型引用——requirements 卡 T02"runtime
# 边暂不登记"同款处理：实际引用时按卡 §3.2 增登；project 只读修订查询
# 经装配面组装、结果归档经 execution→IResultArchivePort——卡 §2.4）。
# 用统一真名 sdurws_ird_<unit>：集成与独立冒烟两种模式下都可解析（别名
# RWS::ird::* 仅集成模式存在——kinematics/selection 落位时同款取舍）。
# 零 Qt（R-3/NFR-MNT-01——_plugin 目标为唯一 Qt 例外载体）、零其他业务
# 域单元（R-1——trajectory 激励经 ITrajectorySourcePort 注入端口、
# requirements 工况经快照 objectClosure、drivetrain 映射唯一实现经③端口、
# selection 消费经归档结果＋JointSideSeriesPack——卡 §2.4 消费表，
# 零编译期依赖）、零 testkit（T-1——testkit 仅随测试目标消费）、零
# sdurw_proximity（R-5/N3——dynamics 全程零碰撞，AT-19 无本域入口）。
target_link_libraries(sdurws_ird_dynamics PUBLIC
    sdurws_ird_core
    sdurws_ird_evidence
    sdurws_ird_runtime
    sdurws_ird_execution
    sdurws_ird_diagnostics)

# ---- L1 基线库链接（卡 §3.2 L1 基线集——DYN-01/05 经 RobWorkSim 消费）----
# 集成/冒烟判定＝TARGET sdurw_kinematics（框架库目标只在集成配置树存在——
# runtime P-RT-3 同款探测）。集成模式：sdurw_math/sdurw_kinematics/
# sdurw_models 直接 TARGET 链接；sdurwsim 因顶层顺序（RobWorkStudio 先于
# RobWorkSim 配置）尚无目标，按名称链接（CMake 生成期解析）——DTB §4.6
# L1 基线表明文登记 dynamics（WP-17-T02）为 sdurwsim 消费方（DynamicWorkCell/
# 刚体动力学——T03 RNEA 输入参数面，本任务仅链接不消费：P-RPT-9 先例）。
# Eigen3（卡 §3.2 L1 基线"参照 kinematics O-40 先例 PRIVATE 链"）＝RNEA
# 矩阵运算预备面：T02 源码零 Eigen 消费，随 T03 逐码路径启用——PRIVATE
# 仅本计算库实现消费（公共头契约面零 Eigen 类型，不向下游传播）。
# 冒烟模式：T02 源码集（DiagCodes.cpp）零 rw/rwsim/Eigen 消费，零框架
# 链接（L1 清单空）——T03 引入 rw 头消费时照 runtime RT-T03 先例补冒烟
# 侧 rw 源码树头路径。
if(TARGET sdurw_kinematics)
    target_link_libraries(sdurws_ird_dynamics PUBLIC
        sdurw_math
        sdurw_kinematics
        sdurw_models
        sdurwsim)
    find_package(Eigen3 CONFIG REQUIRED)
    target_link_libraries(sdurws_ird_dynamics PRIVATE Eigen3::Eigen)
endif()

# 别名（集成模式）：供其余单元以 RWS::ird::dynamics 消费；冒烟模式无
# 别名（与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::dynamics ALIAS sdurws_ird_dynamics)
endif()

# ---- 插件界面目标 sdurws_ird_dynamics_plugin（WP-17-T02 落位——契约
#      acceptance 1 明文创建；卡 §3.2 目标布局）----
# 最小可注册实现（零业务计算逻辑——卡 §9.5）：DynamicsPluginAssembly
# ＝自持描述符装配门面（assembly/ 公共头——pluginId/titleKey 均有
# ui.md §11.1/§3.5 登记出处；selection WP-19-T02 同款先例）。源码布局＝
# plugin/ 目录（门禁 R-3 产品面扫描域为 include/**＋src/**，plugin/ 不在
# 扫描域——Qt 红线的文件域隔离，selection/modeling 同款先例）；装配
# 契约头＝assembly/（plugin 目标的 PUBLIC include 面——宿主装配层只见
# 门面头，零插件私有头依赖——R-2；workflow O-45 教训前移，落位即建）。
# 本批次零 Q_OBJECT 类（界面控件随 WP-17-T09 落位时引入），故不开
# AUTOMOC；首个 Q_OBJECT 类落位任务按 modeling/requirements 先例置
# AUTOMOC ON（DTB §5.1 v0.17 行口径）。
add_library(sdurws_ird_dynamics_plugin STATIC
    plugin/DynamicsPluginAssembly.cpp)   # 装配门面实现（assembly/
                                         #   公共头的私有实现）
set_target_properties(sdurws_ird_dynamics_plugin PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_dynamics_plugin PRIVATE /utf-8)
endif()

# 插件私有头解析（plugin/*.hpp——PRIVATE，不外溢；selection 同款先例；
# T02 仅单实现 TU，仍按先例登记以稳定后续批次形态）。
target_include_directories(sdurws_ird_dynamics_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
# 装配层（宿主插件）消费 <sdurws/ird/dynamics/DynamicsPluginAssembly.hpp>
# ——PUBLIC include 面（非 include/ 产品扫描域——零 Qt 红线文件域隔离同
# plugin/ 口径）。
target_include_directories(sdurws_ird_dynamics_plugin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/assembly>)

# 链接面（卡 §3.2 二分结构——"Qt Widgets 插件"例外载体面；selection
# 同款两行形态）：
#   - sdurws_ird_dynamics＝本计算库（门面/后续面板消费域设施的唯一来源；
#     自边：ird_gates 引擎按"插件目标链接本单元产品目标"装配自边豁免
#     放行〔ird_gates.cmake 2a 形态二 _self_edge 分支〕，与 selection/
#     modeling/requirements/kinematics/workflow 五 plugin 同款形态）；
#   - Qt 三件套＝plugin 分类面（ird_gates R-3 仅查 product/worker 形态；
#     T02 源码零 Qt 类消费，链接面先行建立——WP-17-T09 面板批次直接
#     增列源文件即可）；
#   - 零 sdurws_ird_ui（与 modeling/requirements/kinematics 插件的
#     "→本计算库＋sdurws_ird_ui"三边形态不同：selection T02 已立"自持
#     描述符、零 ui 编译边"先例——dynamics 插件界面随 WP-17-T09 以
#     真实 ui 类型落位，届时若需 ui 编译边，先走单元卡增量修订＋白名单
#     增登）。
target_link_libraries(sdurws_ird_dynamics_plugin PUBLIC
    sdurws_ird_dynamics
    Qt6::Core Qt6::Gui Qt6::Widgets)

# 别名（集成模式）：供其余单元以 RWS::ird::dynamics_plugin 消费；冒烟
# 模式无别名（DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::dynamics_plugin ALIAS sdurws_ird_dynamics_plugin)
endif()

# ---- 测试目标 sdurws_ird_dynamics_test（随 WP-17-T02 登记）----
# 用例面（任务契约 acceptance 逐条具名自证）：
#   test/DiagCodesTest.cpp        DYN-* 15 码登记表全表清单＋逐字段登记值
#                                 ＋真实 StableCodeRegistry 注册行为＋重复
#                                 注册拒绝（acceptance 2——DiagCodes.hpp
#                                 行；kinematics P-KIN-7 同款真实注册表
#                                 验证——DYN 前缀已在册无前缀阻塞）
#   test/BuildRedLineTest.cpp     产品面源码红线运行期扫描（零 Qt＋五边
#                                 include 面＋R-4 字面量＋文件头注释——
#                                 acceptance 1/2，与文件末尾配置期守卫
#                                 互为两道防线；selection/drivetrain 同款
#                                 四用例）
# 测试 main 自持＝testkit 报告设施（TestMainReport.cpp 安装 TestRecord
# Listener——ird-test-report.json 用例级明细与 gtest XML 并存；IO-T07/
# PRJ-T15/selection 同款先例；IRD_TEST_INFO 携带需求/AT 追溯字段）。零
# QCoreApplication 构造（selection T02 同款诚实登记）：_test 目标零 Qt
# 消费（不链 _plugin），测试面无 Qt 类型。
enable_testing()

add_executable(sdurws_ird_dynamics_test
    test/TestMainReport.cpp
    test/DiagCodesTest.cpp
    test/BuildRedLineTest.cpp
    test/RneaTest.cpp                # WP-17-T03：RNEA 评估器用例组（DynRnea——
                                     #   二连杆解析算例/静态重力矩含倒挂 AT-37/
                                     #   分项恒等式与 M·q̈ 科氏解析对照/摩擦
                                     #   sgn₀ 约定与 MDL-16 缺失降级/负载事件
                                     #   变体/外力恒零/fail-fast 前置/非有限
                                     #   标记/DYN-04 传动无关边界/取消语义）；
                                     #   夹具 test/TwoLinkFixture.hpp（自持
                                     #   CanonicalModel 直构——R-2 不跨单元
                                     #   include 他单元测试私有头）
    test/StatisticsTest.cpp)         # WP-17-T04：序列与统计用例组（DynSeries/
                                     #   DynEnvelope/DynPowerEnergy——峰值窗与
                                     #   所在段黄金算例/时间加权 RMS 含驻留解析
                                     #   对照（禁样本平均对照）/E⁺E⁻ 分项与
                                     #   制动段符号/序列身份冻结与内容身份确定
                                     #   性/类型化不混算/Empty 与无效显式 NaN/
                                     #   fail-fast 边界/RNEA→序列→统计端到端）

target_compile_features(sdurws_ird_dynamics_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_dynamics_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（/utf-8 说明见产品目标处；gtest 头为
# ASCII 不受影响）。显式再设一次以自洽——测试目标应独立可读，不依赖
# 被测目标的编译选项细节。
if(MSVC)
    target_compile_options(sdurws_ird_dynamics_test PRIVATE /utf-8)
endif()

# 测试链接产品库 sdurws_ird_dynamics 同时验证"公共头可被正常 include
# ＋库可链接"（五登记边 PUBLIC 传染链随目标自动到达——core/evidence/
# runtime/execution/diagnostics 的链接语义经本链路在链接器层面自证）。
# sdurws_ird_testkit＝报告/追溯设施消费面（RecordListener/IRD_TEST_INFO
# ——T-1 允许形态：仅测试目标链 testkit，产品目标不链）。单行链接语句
# ＝契约测试 T-1 扫描的登记形态约束（testkit 与测试目标名同行可见——
# kinematics/selection/drivetrain 同款）。本目标零 Qt（红线扫描用例面
# ——不链 _plugin）。
target_link_libraries(sdurws_ird_dynamics_test PRIVATE sdurws_ird_dynamics sdurws_ird_testkit GTest::gtest)

# 源码树位置注入：红线扫描用例在运行期扫描 dynamics 源码核对红线
# （零 Qt/零表外 include——卡 §3.2）。经编译定义传 industrialrobot 根
# （kinematics/io/selection 同款——值内以单元名作子目录前缀，扫描面随
# 单元自包含）。
target_compile_definitions(sdurws_ird_dynamics_test PRIVATE
    IRD_DYNAMICS_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# vcpkg 的 gtest:x64-windows 是动态库（gtest.dll/gtest_main.dll）：把测试
# 可执行文件的运行期 DLL 拷到其输出目录，保证不经 PATH 配置即可直接运行
# （留痕与验收都要直接执行该可执行文件）。TARGET_RUNTIME_DLLS 需 CMake
# ≥3.21，本仓库工具链为 VS2022 自带的更新版本，满足。
add_custom_command(TARGET sdurws_ird_dynamics_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_dynamics_test>
            $<TARGET_FILE_DIR:sdurws_ird_dynamics_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
# 框架原宏见 RobWork/RobWork/gtest/CMakeLists.txt：add_test ＋ <target>_
# report（运行测试并落 gtest XML）。本宏按 DTB §5.5 在本单元 CMake 内
# 定义（定义先于全部调用——CMake 顺序解析；core/io/kinematics/selection
# 同款形态，不做跨单元共享——dynamics 是业务域单元，不提供构建工具
# 设施，NFR-MNT-04）。报告输出于构建树本目录 gtest_reports/<target>.xml；
# 任务留痕时拷贝入 doc/industrial-robot-design/traceability/（AGENTS.md
# §4.2 验证留痕）。报告目标不进默认构建（EXCLUDE_FROM_ALL）：跑测试是
# 显式动作，与框架"不随 ordinary build 执行测试"的约定一致。
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

ird_add_gtest(sdurws_ird_dynamics_test)

# ---- 契约测试目标 sdurws_ird_dynamics_contract_test（随 WP-17-T02
#      登记——契约 acceptance 1 明文创建）----
# 落位期契约面＝构建图边界契约（selection/drivetrain BuildGraphContract-
# Test 同款扫描形态——五条登记边的存在性/封闭性/无业务域互链/无表外
# 平台边/testkit 仅测试目标/_worker 形态不存在/include 面同界；与
# drivetrain 相反，_plugin 目标落位为正向断言）＋插件可注册契约
# （DynamicsPluginAssemblyContractTest——描述符登记出处自证＋卡 §9.5
# 插件零计算红线词表扫描〔RNEA/仿真/统计符号＋直接文件/项目访问零命中〕
# ——acceptance 1"最小可注册实现"的执行证明）；业务对端面契约（runtime
# 视图/evidence 评估器注册/execution 任务通道 fake 套件）随 WP-17-T03+
# 落地（T02 登记面不触对端契约面，不预建占位用例）。
# 自有 main 同 _test 目标（报告设施接入）。
add_executable(sdurws_ird_dynamics_contract_test
    contract_test/ContractTestMain.cpp
    contract_test/BuildGraphContractTest.cpp
    contract_test/DynamicsPluginAssemblyContractTest.cpp
    contract_test/RneaBoundaryContractTest.cpp)  # WP-17-T03：RNEA 面单元边界
                                     #   契约（DynRneaBoundary——DYN-04 关节侧
                                     #   零传动映射消费符号词表扫描〔V-28/V-31
                                     #   静态精神〕＋产品面 include 白名单
                                     #   〔R-1/R-2——五登记边＋本单元〕）

target_compile_features(sdurws_ird_dynamics_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_dynamics_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（同 _test 目标口径）。
if(MSVC)
    target_compile_options(sdurws_ird_dynamics_contract_test PRIVATE /utf-8)
endif()

# 链接面与 _test 同款（testkit＝报告设施——T-1 允许形态；单行链接语句
# ＝契约测试 T-1 扫描的登记形态约束，同 _test 目标口径）。
# sdurws_ird_dynamics_plugin＝同单元插件目标（装配门面——可注册契约
# 用例的被测面；"测试目标仅允许同单元产品目标"纪律下 plugin face 亦属
# 同单元许可集——selection/workflow 先例同款；其 PUBLIC 面携带 Qt 传染，
# 供后续批次注册端口符号解析预备）。
target_link_libraries(sdurws_ird_dynamics_contract_test PRIVATE sdurws_ird_dynamics sdurws_ird_dynamics_plugin sdurws_ird_testkit GTest::gtest)

# 源码树位置注入（构建图/插件面扫描用例同 _test 口径）。
target_compile_definitions(sdurws_ird_dynamics_contract_test PRIVATE
    IRD_DYNAMICS_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# 运行期 DLL 拷贝（vcpkg gtest 动态库＋插件目标 Qt 传染面——同 _test
# 目标的 POST_BUILD 口径）。
add_custom_command(TARGET sdurws_ird_dynamics_contract_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_dynamics_contract_test>
            $<TARGET_FILE_DIR:sdurws_ird_dynamics_contract_test>
    COMMAND_EXPAND_LISTS)

ird_add_gtest(sdurws_ird_dynamics_contract_test)

# ---- ctest LABELS "ird"（modeling/ui/requirements/kinematics/selection
#      落位同款）----
# 验收过滤口径：ctest -L "^ird$"——两测试（模型层、进程内、确定性）同
# 标签；_gui_test 形态本单元无（GUI 手动验证通道随 WP-17-T09——卡 §11.5，
# 本批次无 GUI 载体用例）。
set_tests_properties(sdurws_ird_dynamics_test sdurws_ird_dynamics_contract_test
    PROPERTIES LABELS "ird")

# ---- 依赖红线守卫（配置期断言——卡 §3.2，selection/drivetrain foreach
#      同款）----
# dynamics 的 industrialrobot 单元依赖仅限五条登记边：core＋evidence＋
# runtime＋execution＋diagnostics（卡 §3.2 CMake 行边表——ARCH §3.5 业务
# 域→L2/L3 公共接口许可方向的实例化，白名单 "dynamics->…" 五行同源）；
# 链接到任何其他 sdurws_ird_* 目标（业务域单元＝R-1 命中面；表外平台
# 单元＝SUB 命中面——含卡 §3.1 提及但 §3.2 CMake 行未列的 project〔T02
# 零公共值类型引用不落边，实际引用时按卡 §3.2 增登〕）即配置失败。零
# Qt（R-3/NFR-MNT-01——Qt 例外载体仅为 _plugin 目标，不在本遍历范围）。
# 产品库本体零 gtest/testkit（D-06/T-1——断言宏只在消费方测试目标展开；
# 本守卫遍历的是产品目标 sdurws_ird_dynamics 的链接面，测试目标的 gtest/
# testkit 边不在其内）。两面读取（LINK_LIBRARIES 含 PRIVATE 边＋
# INTERFACE_LINK_LIBRARIES 含 PUBLIC 边）防漏。属性未设置时
# get_target_property 返回 "<var>-NOTFOUND" 字面串，不匹配任何模式，不
# 会误报。testkit 不在第一条模式中逐字列出（selection 同款先例）：
# sdurws_ird_testkit 落在通配 "^sdurws_ird_" 的拦截范围（≠五条登记边即
# FATAL），避免守卫正则字样干扰契约测试的 T-1 链接语句扫描
# （BuildGraphContractTest 的 NoTestkitEdge 用例以"逐行只认测试目标链接
# 语句"钉住产品目标零 testkit）。正式全仓门禁（依赖图白名单）归
# ird_gates（WP-01-T01），本守卫不替代它。
get_target_property(_ird_dynamics_link_libs sdurws_ird_dynamics LINK_LIBRARIES)
get_target_property(_ird_dynamics_iface_libs sdurws_ird_dynamics INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_dynamics_link_libs _ird_dynamics_iface_libs)
    if(_lib MATCHES "^sdurws_ird_"
       AND NOT _lib STREQUAL "sdurws_ird_core"
       AND NOT _lib STREQUAL "sdurws_ird_evidence"
       AND NOT _lib STREQUAL "sdurws_ird_runtime"
       AND NOT _lib STREQUAL "sdurws_ird_execution"
       AND NOT _lib STREQUAL "sdurws_ird_diagnostics")
        message(FATAL_ERROR
            "dynamics 依赖红线（R-1/卡 §3.2）：sdurws_ird_dynamics "
            "仅可链接五条登记边 core/evidence/runtime/execution/"
            "diagnostics（units/dynamics.md §3.2、ARCH §3.5——project "
            "T02 零公共值引用不落边，含 testkit——T-1 产品目标禁链），"
            "发现 ${_lib}")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "dynamics 依赖红线（R-3/NFR-MNT-01，计算库零 Qt——卡 §3.2 "
            "二分结构，Qt 例外载体仅 _plugin 目标）：发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "dynamics 依赖红线（D-06）：产品库本体不链接 gtest，发现 ${_lib}")
    endif()
endforeach()
