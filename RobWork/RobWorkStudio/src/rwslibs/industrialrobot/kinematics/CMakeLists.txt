# =====================================================================
# kinematics 单元（业务域：运动学可达性事实——Stage 3）构建脚本
#
# 设计依据：
#   - units/kinematics.md §3.1（单元组成——计算库 STATIC 零 Qt＋两测试
#     目标；"_worker 目标：不新建——worker 进程复用本计算库"）、§3.2
#     （目标升级与门禁边登记——WP-15-T02 执行清单：六条接口依赖边/配置
#     期红线守卫自持/_plugin 随 WP-15-T12 落位）、§3.3（公共头布局——
#     T02 行：Errors.hpp/DiagCodes.hpp 两头）、§3.4（线程与确定性总约定
#     ——纯函数服务可重入、数值恒 SI、随机性唯一来源）
#   - development-task-breakdown.md §5.1（双模式/目标命名/C++17 不用
#     C++20）、§5.3（全局禁止项）、§5.5（gtest 接入定稿）
#   - ARCHITECTURE.md §3.3（业务域单元二分结构）、§3.5（依赖表——各业务
#     域单元→L2/L3 公共接口的许可方向；六边为其实例化，登记于
#     cmake/ird_gates_whitelist.cmake 的 "kinematics->…" 六行）
#   - 需求 NFR-MNT-01（计算内核零 Qt——kinematics 为 L2 等效业务计算库）
#   - 任务契约 tasks/foundation/WP-15-T02.json（本文件即其 acceptance
#     1/2/4 的落地面：INTERFACE 占位→STATIC＋_test/_contract_test 随
#     文件注册＋T02 公共面两头）
#
# 本文件随 WP-15-T02 落地（kinematics 由上级 INTERFACE 占位升级为真实
# 库）：
#   1. sdurws_ird_kinematics STATIC（目标名与别名 RWS::ird::kinematics
#      不变，DTB §5.1）；T02 公共面＝Errors/DiagCodes 两头＋对应实现
#      翻译单元（§3.3 表 T02 行；KinTypes/Fk/Ik 等随 T03/T04 增列——
#      §3.3 布局表任务列权威）；
#   2. 测试目标 sdurws_ird_kinematics_test 与
#      sdurws_ird_kinematics_contract_test（自持 ird_add_gtest 宏：
#      add_test＋<target>_report XML——core/io/modeling/requirements
#      同款；ctest LABELS "ird"）；
#   3. 配置期依赖红线守卫（R-1/R-3 的 kinematics 侧断言——卡 §3.2；
#      全仓正式门禁归 ird_gates/WP-01-T01，本守卫不替代它）。
#   _plugin 目标（Qt Widgets）不在本任务：卡 §3.1/§3.2/契约 acceptance 2
#   明文"随 WP-15-T12 落位"（不预建空目标——总体详设 §1.5 原则）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制；随本任务首个测试目标引入）----
# gtest 经 vcpkg 安装（gtest:x64-windows，经典模式）；REQUIRED＝找不到即
# 配置失败（失败即停，不静默跳过测试目标）。不消费框架的 USE_gtest/RW::
# gtest（从未生成），也不源码 vendor——两份 gtest 混用会破坏 ODR。
# 独立冒烟模式必须带 vcpkg toolchain 参数方可解析（CORE-T01 G-2 口径）。
find_package(GTest CONFIG REQUIRED)

# ---- Eigen 接入（治理登记 O-40，DTB §4.2——WP-15-T03 携带）----
# Jacobian SVD/线性代数经 vcpkg 经典模式引入 eigen3（实测安装版本
# 5.0.1）；rw::math 无 SVD——该依赖为 kinematics 计算所必需。PRIVATE
# 仅本计算库实现文件消费（PUBLIC 面零 Eigen 类型——Fk.hpp 契约面纯
# rw::math/标准库），不向下游传播（业务域依赖面最小化）。
find_package(Eigen3 CONFIG REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC（目标名不变，DTB §5.1）----
# T02 源码集＝两公共头的实现翻译单元（头内码值常量为 inline constexpr，
# 零运行时成本无需 TU；函数面按头分文件——modeling/io/requirements 同款
# 布局）：
#   src/Errors.cpp     域错误 token 转发表＋域错误→稳定码映射数据（§9.2/
#                      §9.6；T03 起映射两行——NoDevice/NoTcp 随消费任务）
#   src/DiagCodes.cpp  KIN-* 15 码描述符清单（§9.6 全表逐码落值）＋注册
#                      函数（§9.6 表头"装配期注册"的执行面）
# T03 增列（tasks/foundation/WP-15-T03.json，§3.3 布局表 T03 行）：
#   src/Fk.cpp         FK 与位姿指标实现（KIN-01——链式 FK/基础雅可比/
#                      Eigen SVD 指标链/canonical 载荷编码；全单元唯一
#                      Eigen 消费点——O-40 PRIVATE）
#   src/Evaluators.cpp kin.pose-metrics 评估器形态（IEngineeringEvaluator
#                      实现＋宿主注入工厂＋descriptor——§4.3/§9.3）
# T04 增列（tasks/foundation/WP-15-T04.json，§3.3 布局表 T04 行）：
#   src/KinTypes.cpp   值模型函数面（ConfigurationSignature 构型签名——
#                      §6.1/I-KIN-3；KinTypes.cpp 随 T04 值模型落位增列——
#                      T03 批注释预言的落位）
#   src/Bounds.cpp     解析工作半径界限（§5.4 结局 5 素材面——连杆长度和
#                      上界推导＋证明素材组装）
#   src/Ik.cpp         多初值 IK 求解（KIN-02——初值策略生成/阻尼最小二乘/
#                      解析界限检查/硬过滤①②③/去重/结局组装；Eigen 消费点
#                      之一——O-40 PRIVATE）
#   src/SolutionSet.cpp 解集视图（§6.2——四键稳定排序唯一实现点
#                      sortSolutions＋sorted/filtered/statistics/worstBy）
# T05 增列（tasks/foundation/WP-15-T05.json，§3.3 布局表 Evidence.hpp 行
# T05 半区）：
#   src/Evidence.cpp   批量通道组装面（证据组装器 build——§8.2 表五行产出
#                      的唯一组装点＋批量 canonical 载荷编码
#                      encodeBatchPayloadCanonical——§5.6 绑定；批量执行
#                      主流程（展开/分批/求解/取消/检查点/进度）在
#                      Evaluators.cpp 的 T05 段——kin.task-points-batch
#                      评估器族）
# T06 增列（tasks/foundation/WP-15-T06.json，§3.3 布局表 Sampling.hpp/
# Coverage.hpp 行 T06）：
#   src/Sampling.cpp   区域采样与覆盖率（KIN-04——确定性样本生成（Grid
#                      体心/Random 种子序列/斐波那契螺旋方向集×roll 均分
#                      ——D-KIN-6）、sampleSetIdentity（evidence §4.1.4
#                      公式）、覆盖率计算唯一实现点 computeCoverage、区域
#                      覆盖 canonical 载荷编码；kin.region-coverage 评估
#                      器族在 Evaluators.cpp 的 T06 段）
# T07 增列（tasks/foundation/WP-15-T07.json，§3.3 布局表 Collision.hpp 行
# T07）——集成模式专属（见下方 target_sources 注释）：
#   src/Collision.cpp  ④端口碰撞接入（KIN-05/AT-19——请求方场景装配
#                      assembleCollisionScene＋真实会话适配器
#                      PolicyCollisionSessionAdapter＋会话接线入口
#                      makeCollisionSession；消费 policy 公共头完整类型
#                      面——policy 侧同因 TU 同 gating；冒烟模式无任何
#                      TU include Collision.hpp，冒烟口径不受影响）
add_library(sdurws_ird_kinematics STATIC
    src/Errors.cpp
    src/DiagCodes.cpp
    src/Fk.cpp
    src/Evaluators.cpp
    src/Evidence.cpp
    src/KinTypes.cpp
    src/Bounds.cpp
    src/Ik.cpp
    src/SolutionSet.cpp
    src/Sampling.cpp)

# ---- T07 增列（仅集成模式）：src/Collision.cpp 消费 policy 公共头的
# 完整类型面（CollisionEvaluationSession 评估调用点）——冒烟模式无框架
# 库可链（policy POL-T06 同款 gating 判定：TARGET sdurw_kinematics 只在
# 集成配置树存在）。R-POL-2 红线不破：碰撞实现唯一在 policy（proximity
# 直链的唯一许可方仍是 policy——本 TU 只调其冻结入口）。
if(TARGET sdurw_kinematics)
    target_sources(sdurws_ird_kinematics PRIVATE
        src/Collision.cpp)  # ④端口碰撞接入（KIN-05——场景装配/会话适配/
                            # 接线三态；WP-15-T07）
endif()

# T08 增列（tasks/foundation/WP-15-T08.json，§3.3 布局表 Commands.hpp 行
# T08；两模式编译——纯 std＋core/runtime 头内模板面，零框架库符号）：
#   src/Commands.cpp   会话姿态写入口（KinSessionPose——KIN-06/AT-04，§4.5
#                      身份外元素）＋设默认命令门面（IKinematicsCommand-
#                      Handler——KIN-14/§9.7 D-KIN-5：组装 modeling
#                      apply-robot-design 增量载荷经①端口投影提交；载荷
#                      框架编码＋根字节补丁注入缝＋KIN-ROOT-BYTES-ILLEGAL
#                      产码面）
target_sources(sdurws_ird_kinematics PRIVATE
    src/Commands.cpp)

# T09 增列（tasks/foundation/WP-15-T09.json，§3.3 布局表 Export.hpp/
# Recompute.hpp 行 T09；两模式编译——纯 std＋core 头，零框架库符号、
# 零 io/project 消费——§7.4"写出经 io 通道、导出不产生修订"的结构面）：
#   src/Export.cpp     结果导出值行（KIN-08/AT-04——解行/过滤诊断行/区域
#                      覆盖行整理＋JSON/CSV 文本编码＋导出文件头来源声明；
#                      纯函数面，写出本身归 io 通道由插件侧触发）
#   src/Recompute.cpp  批量复算请求构造（KIN-08/§7.4——按原 slice 重新
#                      提交 execution 新 run 的域侧请求值；字段携带规则
#                      ＋调用方错误 fail-fast；提交动作归 execution）
target_sources(sdurws_ird_kinematics PRIVATE
    src/Export.cpp
    src/Recompute.cpp)

# T10 增列（tasks/foundation/WP-15-T10.json，§3.3 布局表 AnalysisConfig.hpp
# 行 T10；两模式编译——纯 std＋core/evidence 头，零框架库符号、零 io/
# project 消费——P-KIN-4：用户级持久化只交付 schema/编解码与装载出口，
# 存储载体归 ui 侧，不入 .rwdesign 修订）：
#   src/AnalysisConfig.cpp  AnalysisConfiguration canonical v1 编解码＋
#                           configDigest 摘要（唯一经 core ContentDigester）
#                           ＋非法配置 fail-fast 校验＋KIN-CONFIG-ILLEGAL
#                           产码＋配置变更依赖提示（L-K7/AT-27——纯提示
#                           数据）＋显示单位纯投影（KIN-12——换算唯一经
#                           core Units，本 TU 零换算算术）
target_sources(sdurws_ird_kinematics PRIVATE
    src/AnalysisConfig.cpp)

# T11 增列（tasks/foundation/WP-15-T11.json，§3.3 布局表 Render.hpp 行
# T11；两模式编译——纯 std＋core/policy 头内值面，零框架库符号、零 Qt
# ——KIN-07 数据本单元产、呈现归 ui View3D，本 TU 无任何绘图设施）：
#   src/Render.cpp     失败点/薄弱区三维渲染数据组装（四类数据结构＋两
#                      通道组装点——解集通道 worstBy 最差项（T09 设施
#                      复用）/批量通道 T05 per-item 状态消费＋统一状态词
#                      投影（core TaskState/TaskOutcome）＋可视化点会话
#                      回写值（KIN-06——KinSessionPose 唯一写点）＋选中
#                      联动数据（L-K1）；阈值唯一来源 policy JointThresholds
#                      ——P-POL-2 显式不适用不发明数值）
target_sources(sdurws_ird_kinematics PRIVATE
    src/Render.cpp)

# C++17（core.md 决策 D-01 同源）：逐目标显式设定，不设全局标准。
# PUBLIC 传播：kinematics 公共头使用 C++17 设施（std::string_view/
# inline constexpr 变量等），消费者编译 kinematics 头也必须至少 C++17。
target_compile_features(sdurws_ird_kinematics PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_kinematics PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：本单元源文件全部为 UTF-8（无 BOM）且含中文注释（AGENTS.md §2
# 注释规范）。MSVC 默认按系统代码页（GBK）解读无 BOM 源文件，会把 UTF-8
# 中文误读成乱码 token（core 落位时曾出现 C2001"常量中有换行符"），必须
# 显式 /utf-8。PUBLIC 传播：公共头同为 UTF-8，消费者编译含 kinematics
# 头的翻译单元也需要。
if(MSVC)
    target_compile_options(sdurws_ird_kinematics PUBLIC /utf-8)
endif()

# 公共头根：仅 kinematics/include（内部含 sdurws/ird/kinematics/）。
# R-2 红线：私有实现头不得置于 include/——本目录是 kinematics 唯一对外
# 的头暴露位，实现细节头（若有）只能放 src/ 并以相对路径被实现文件包含。
target_include_directories(sdurws_ird_kinematics PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 编译依赖：core＋diagnostics＋runtime＋policy＋evidence＋execution（卡
# §3.2 边表六条接口依赖边——ARCH §3.5"各业务域单元→L2/L3 公共接口"许可
# 方向的实例化，ird_gates_whitelist.cmake 的 "kinematics->…" 六行同批
# 登记）。
# PUBLIC 形态：DiagCodes.hpp 内嵌 diagnostics 承载的 CodeDescriptor/
# IDiagnosticRegistry 契约类型（其头又内嵌 core::DiagCode——消费方必须
# 随链到达）；core/diagnostics 两边本任务真实消费（DiagCodes.hpp include
# 面），runtime/policy/evidence/execution 四边本任务"仅链接目标不消费
# 上游公共头"（P-RPT-9/modeling/requirements T02 同款先例——边随落位
# 登记，消费随 T03+ 任务回填：runtime IRuntimeModelView/RuntimeSnapshot
# →T03 FK 输入面、policy ④端口 ICollisionEvaluator/IPolicyProvider→
# T04/T07 硬过滤面、evidence IEngineeringEvaluator/EvaluationRequest→
# T03~T07 评估器族、execution 任务提交/取消令牌/检查点→T05 批量通道；
# 六边统一 PUBLIC 保持形态一致——契约 acceptance 3 六边全登记）。
# 用统一真名 sdurws_ird_<unit>：集成与独立冒烟两种模式下都可解析（别名
# RWS::ird::* 仅集成模式存在——core/io/requirements 落位时同款取舍）。
# 零 Qt（R-3/NFR-MNT-01——_plugin 目标随 WP-15-T12 落位时方为 Qt 例外
# 载体）、零其他业务域单元（R-1——kinematics 不直链 modeling/
# requirements/trajectory/dynamics/drivetrain/selection/optimization，
# 跨单元协作走六类端口）、零 testkit（T-1——testkit 仅随测试目标消费）、
# 零 sdurw_proximity（R-5——碰撞唯一实现归 policy，卡 §1.4）。
target_link_libraries(sdurws_ird_kinematics PUBLIC
    sdurws_ird_core
    sdurws_ird_diagnostics
    sdurws_ird_runtime
    sdurws_ird_policy
    sdurws_ird_evidence
    sdurws_ird_execution)

# Eigen（O-40）：PRIVATE 仅本计算库消费——公共头零 Eigen 类型（Fk.hpp/
# KinTypes.hpp/Evaluators.hpp 契约面纯 rw::math/标准库），消费面收敛在
# src/Fk.cpp 的 SVD 计算点；不向下游传播（静态库 PRIVATE 不进接口面）。
target_link_libraries(sdurws_ird_kinematics PRIVATE Eigen3::Eigen)

# ---- Qt6 接入（WP-15-T12——插件目标的 Qt 载体面，R-3 唯一例外面）----
# 仅插件目标与 harness 消费 Qt（卡 §3.2"_plugin 落位（Widgets，AUTOMOC，
# 仅插件目标 Qt 例外——计算库零 Qt 红线不变）"；modeling WP-13-T15 同款
# 先例）。REQUIRED＝找不到即配置失败（失败即停，不静默降级）；独立冒烟
# 模式必须带 CMAKE_PREFIX_PATH 指向 Qt 安装（编排口径：Qt6 位于
# D:/software/Qt/6.11.1/msvc2022_64）。
find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

# ---- 插件界面目标 sdurws_ird_kinematics_plugin（WP-15-T12 落位——卡
#      §3.1/§3.2 明文"_plugin 随 WP-15-T12"）----
# 四面板＋域命令注册的插件目标（STATIC——正式装配面；ui 宿主插件经
# assembly/ 装配契约头消费，WP-24 首版装配先例同构；非 RobWorkStudioPlugin
# 派生的开发期验证通道——那是 ui 单元 sdurws_ird_ui_plugin 的职责）。
# 源码布局＝plugin/ 目录（门禁 R-3 产品面扫描域为 include/**＋src/**，
# plugin/ 不在扫描域——Qt 红线的文件域隔离，modeling 同款先例）；装配
# 契约头＝assembly/（plugin 目标的 PUBLIC include 面——O-31 装配器单行
# 适配面，同在扫描域之外）。
add_library(sdurws_ird_kinematics_plugin STATIC
    plugin/KinPanelTypes.hpp           # 零 Qt 值类型（行集/会话态/后台缝——呈现模型半区）
    plugin/KinPanelUnits.hpp           # L-K8 显示单位重投影（零 Qt——KIN-12 唯一出口）
    plugin/KinPanelUnits.cpp
    plugin/KinPanelCommandCatalog.hpp  # §9.8 域命令清单/装配登记/就绪投影/只读门控（零 Qt）
    plugin/KinPanelCommandCatalog.cpp
    plugin/KinPanelModel.hpp           # 四面板投影行集（零 Qt——域设施消费面）
    plugin/KinPanelModel.cpp
    plugin/KinPanelFlows.hpp           # 数据流编排（L-K2~L-K12 模型层可测流——零 Qt）
    plugin/KinPanelFlows.cpp
    plugin/KinematicsPanelWidget.hpp   # 四面板主面板＋求解配置高级面板（Q_OBJECT——AUTOMOC 载体）
    plugin/KinematicsPanelWidget.cpp   #   （头入源列表——AUTOMOC 扫描 Q_OBJECT 声明）
    plugin/KinematicsUiModule.hpp      # ui.md §11.2 模块实现（P-KIN-7 对端冻结——正式继承）
    plugin/KinematicsUiModule.cpp
    plugin/KinematicsPluginAssembly.cpp)  # 装配门面实现（assembly/ 公共头的私有实现）
set_target_properties(sdurws_ird_kinematics_plugin PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    AUTOMOC ON)  # Q_OBJECT 所需——仅本目标显式开启（DTB §5.1 v0.17 行口径；
                 # 集成树全局 AUTOMOC 对既有目标本就生效，零形态变化）
if(MSVC)
    target_compile_options(sdurws_ird_kinematics_plugin PRIVATE /utf-8)
endif()

# 插件私有头解析（plugin/*.hpp——PRIVATE，不外溢；modeling/ui 同款先例）。
target_include_directories(sdurws_ird_kinematics_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
# 装配层（ui 插件）消费 <sdurws/ird/kinematics/KinematicsPluginAssembly.hpp>
# ——PUBLIC include 面（非 include/ 产品扫描域——零 Qt 红线文件域隔离同
# plugin/ 口径）。
target_include_directories(sdurws_ird_kinematics_plugin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/assembly>)

# 链接面（卡 §3.2 明文"插件目标 → 本计算库＋sdurws_ird_ui；禁止链接任何
# 其他业务域单元（R-1）"）：
#   - sdurws_ird_kinematics＝本计算库（面板消费 IFkEvaluator/IIkSolver/
#     IKinematicSolutionSet/Render/Export/AnalysisConfig/Commands——域设施
#     唯一来源；自边：ird_gates 引擎按"两端均属业务单元"粗粒度报 R-1，
#     与 modeling_plugin 同款登记口径——WP-13-T15 比对留痕先例）；
#   - sdurws_ird_ui＝ui 公共面（IPluginUiModule/IPluginUiRegistrar/
#     CommandDescriptor/FormEditCommon/ITaskPresentationModel——装配契约
#     与表单公共件；表外边 kinematics->ui 按 WP-13-T15 modeling->ui 先例
#     逐条具名登记——allowedFiles 不含白名单/DTB，登记册回填归 WP-01-T03）；
#   - Qt 三件套＝plugin 分类面（ird_gates R-3 仅查 product/worker 形态）。
target_link_libraries(sdurws_ird_kinematics_plugin PUBLIC
    sdurws_ird_kinematics sdurws_ird_ui
    Qt6::Core Qt6::Gui Qt6::Widgets)

# 别名（集成模式）：供其余单元以 RWS::ird::kinematics_plugin 消费；冒烟
# 模式无别名（DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::kinematics_plugin ALIAS sdurws_ird_kinematics_plugin)
endif()

# ---- 开发验证 harness：sdurws_ird_kinematics_app（DTB §5.1 v0.20——
#      插件 GUI 手动验证通道；DoD 第 5 条载体）----
# 先例＝ui_app（UI-T15）/modeling_app（WP-13-T15）；_app 后缀＝ird_gates
# 分类面 Qt 合法形态；源码在 app/ 子目录（产品扫描域零触碰）；本目标非
# 测试（不入 ctest 标签）、非产品交付路径（产品装配层落位时按同一契约
# 替换本入口，插件面零改动）。
add_executable(sdurws_ird_kinematics_app
    app/HarnessMain.cpp)

target_compile_features(sdurws_ird_kinematics_app PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_kinematics_app PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_kinematics_app PRIVATE /utf-8)
endif()

# app/ 需解析本单元插件私有头（plugin/KinPanelTypes.hpp 等）——同单元内
# 消费（R-2 不跨单元），单元根 include 目录 PRIVATE 注入。
target_include_directories(sdurws_ird_kinematics_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

# 单行链接语句＝构建图契约测试的登记形态约束（modeling_app 同款）。
target_link_libraries(sdurws_ird_kinematics_app PRIVATE sdurws_ird_kinematics_plugin)

# 运行时 DLL 就位（modeling_app 同款）：TARGET_RUNTIME_DLLS 复制 Qt 三件套
# 等共享库到 exe 旁——绝对路径启动免 PATH 依赖。
add_custom_command(TARGET sdurws_ird_kinematics_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_kinematics_app>
            $<TARGET_FILE_DIR:sdurws_ird_kinematics_app>
    COMMAND_EXPAND_LISTS)

# ---- Qt 平台插件定位（modeling_app 同款坑位修复）：Qt6 官方发行版 DLL 为可
# 重定位前缀形态，从副本位置推导的插件目录不成立（qwindows 找不到即启动
# 失败）——configure 期从 Qt6_DIR 推导真实 Qt 安装前缀生成 qt.conf 复制到
# exe 旁（构建期生成物，不入库）。
set(_ird_kin_qt_prefix "${Qt6_DIR}")
get_filename_component(_ird_kin_qt_prefix "${_ird_kin_qt_prefix}/../../.." ABSOLUTE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/kin-harness-qt.conf"
     "[Paths]\nPrefix=${_ird_kin_qt_prefix}\n")
add_custom_command(TARGET sdurws_ird_kinematics_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/kin-harness-qt.conf"
            "$<TARGET_FILE_DIR:sdurws_ird_kinematics_app>/qt.conf")

# 别名（集成模式）：供其余单元以 RWS::ird::kinematics 消费；冒烟模式无
# 别名（与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::kinematics ALIAS sdurws_ird_kinematics)
endif()

# ---- 测试目标 sdurws_ird_kinematics_test（随 WP-15-T02 登记）----
# 用例面（任务契约 acceptance 逐条具名自证）：
#   test/ErrorsTest.cpp           域错误 token 全表往返＋KinematicsError
#                                 值语义＋映射分批纪律（全表 nullopt——
#                                 acceptance 4，§9.2/§5.2 行）
#   test/DiagCodesTest.cpp        §9.6 全表 15 码清单范围＋逐码级别列＋
#                                 真实 StableCodeRegistry 注册行为＋重复
#                                 注册拒绝（acceptance 4——DiagCodes.hpp
#                                 行；P-KIN-7 处置：以真实注册表验证）
#   test/BuildRedLineTest.cpp     产品面源码红线运行期扫描（零 Qt＋六边
#                                 include 面＋R-4 字面量＋文件头注释——
#                                 acceptance 1/5，与文件末尾配置期守卫
#                                 互为两道防线）
# 测试 main 自持＝testkit 报告设施（TestMainReport.cpp 安装 TestRecord
# Listener——ird-test-report.json 用例级明细与 gtest XML 并存；IO-T07/
# PRJ-T15/modeling/requirements 同款先例；IRD_TEST_INFO 携带需求/AT
# 追溯字段）。
enable_testing()

add_executable(sdurws_ird_kinematics_test
    test/TestMainReport.cpp
    test/GoldenKinTest.cpp  # T13：黄金数据集契约套件（四套黄金＋V-01~V-21 载体）
    test/ErrorsTest.cpp
    test/DiagCodesTest.cpp
    test/BuildRedLineTest.cpp
    test/FkTest.cpp
    test/IkTest.cpp
    test/TaskPointsBatchTest.cpp
    test/RegionCoverageTest.cpp
    test/CommandsTest.cpp
    test/SolutionSetViewTest.cpp
    test/ExportTest.cpp
    test/RecomputeTest.cpp
    test/AnalysisConfigTest.cpp
    test/RenderTest.cpp
    test/PluginPanelTest.cpp)  # T12：L-K1~L-K12 界面链路用例（模型层——QCoreApplication 级）

# T07 增列（仅集成模式）：test/CollisionPortTest.cpp——真实④端口路径
# （真实 WorkCell＋RobWorkCollisionEvaluator＋policy 会话）与运行断言
# （无二次缓存计数策略/三态映射）；消费 rw 非模板类，冒烟模式不编译
# （policy CollisionEvaluationTest 同因同 gating）。
if(TARGET sdurw_kinematics)
    target_sources(sdurws_ird_kinematics_test PRIVATE
        test/CollisionPortTest.cpp)  # WP-15-T07：真实④端口接入＋运行断言
endif()

target_compile_features(sdurws_ird_kinematics_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_kinematics_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（/utf-8 说明见产品目标处；gtest 头为
# ASCII 不受影响）。显式再设一次以自洽——测试目标应独立可读，不依赖
# 被测目标的编译选项细节。
if(MSVC)
    target_compile_options(sdurws_ird_kinematics_test PRIVATE /utf-8)
endif()

# 测试链接产品库 sdurws_ird_kinematics 同时验证"公共头可被正常 include
# ＋库可链接"（六边 PUBLIC 传染链随目标自动到达）。
# sdurws_ird_kinematics_plugin＝同单元插件目标（呈现模型/命令目录/编排
# 函数——T12 界面链路用例的被测面；"测试目标仅允许同单元产品目标"纪律
# 下 plugin face 亦属同单元许可集——modeling WP-13-T15 先例同款）。
# sdurws_ird_testkit＝报告/追溯设施消费面（RecordListener/IRD_TEST_INFO——
# T-1 允许形态：仅测试目标链 testkit，产品目标不链）。单行链接语句＝契约
# 测试 T-1 扫描的登记形态约束（testkit 与测试目标名同行可见——modeling/
# reporting/requirements 同款）。
# Qt6::Core/Gui＝PluginPanelTest 的 QCoreApplication 级 GUI 前提＋
# CommandDescriptor 值面（QKeySequence 在 QtGui——模型层测试不启动 GUI，
# modeling PluginPanelTest 先例同款）。
target_link_libraries(sdurws_ird_kinematics_test PRIVATE sdurws_ird_kinematics sdurws_ird_kinematics_plugin sdurws_ird_testkit GTest::gtest Qt6::Core Qt6::Gui)

# 插件私有头解析（测试消费 plugin/*.hpp 呈现模型头——同单元 PRIVATE 面；
# modeling 先例同款）。
target_include_directories(sdurws_ird_kinematics_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

# 源码树位置注入：红线扫描用例在运行期扫描 kinematics 源码核对红线
# （零 Qt/零表外 include——§3.2/§14.5）。经编译定义传 industrialrobot 根
# （modeling/io/requirements 同款——值内以单元名作子目录前缀，扫描面随
# 单元自包含）。
target_compile_definitions(sdurws_ird_kinematics_test PRIVATE
    IRD_KINEMATICS_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# vcpkg 的 gtest:x64-windows 是动态库（gtest.dll/gtest_main.dll）：把测试
# 可执行文件的运行期 DLL 拷到其输出目录，保证不经 PATH 配置即可直接运行
# （留痕与验收都要直接执行该可执行文件）。TARGET_RUNTIME_DLLS 需 CMake
# ≥3.21，本仓库工具链为 VS2022 自带的更新版本，满足。
add_custom_command(TARGET sdurws_ird_kinematics_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_kinematics_test>
            $<TARGET_FILE_DIR:sdurws_ird_kinematics_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
# 框架原宏见 RobWork/RobWork/gtest/CMakeLists.txt：add_test ＋ <target>_
# report（运行测试并落 gtest XML）。本宏按 DTB §5.5 在本单元 CMake 内
# 定义（定义先于全部调用——CMake 顺序解析；core/io/modeling/requirements
# 同款形态，不做跨单元共享——kinematics 是业务域单元，不提供构建工具
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

ird_add_gtest(sdurws_ird_kinematics_test)

# ---- 契约测试目标 sdurws_ird_kinematics_contract_test（随 WP-15-T02
#      登记——卡 §3.1/§3.3）----
# 落位期契约面＝构建图边界契约（modeling/requirements BuildGraphContract-
# Test 同款扫描形态——六条登记边的存在性/封闭性/无业务域互链/testkit
# 仅测试目标/plugin 不落位/include 面同界）；业务对端面契约（runtime/
# policy/evidence/execution 四对端的 fake 套件）随 T03~T07 落地（卡 §11
# 内部顺序——T02 三头不触对端契约面，不预建占位用例）。
# 自有 main 同 _test 目标（报告设施接入）。
add_executable(sdurws_ird_kinematics_contract_test
    contract_test/ContractTestMain.cpp
    contract_test/BuildGraphContractTest.cpp
    contract_test/PoseMetricsEvaluatorContractTest.cpp
    contract_test/TaskPointIkEvaluatorContractTest.cpp
    contract_test/TaskPointsBatchEvaluatorContractTest.cpp
    contract_test/RegionCoverageEvaluatorContractTest.cpp
    contract_test/CollisionConsistencyContractTest.cpp
    contract_test/CommandFacadeContractTest.cpp
    contract_test/PluginGraphContractTest.cpp)  # T12：插件链接面/命令目录/零计算扫描

target_compile_features(sdurws_ird_kinematics_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_kinematics_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（同 _test 目标口径）。
if(MSVC)
    target_compile_options(sdurws_ird_kinematics_contract_test PRIVATE /utf-8)
endif()

# 链接面与 _test 同款（testkit＝报告设施——T-1 允许形态；单行链接语句
# ＝契约测试 T-1 扫描的登记形态约束，同 _test 目标口径）。
target_link_libraries(sdurws_ird_kinematics_contract_test PRIVATE sdurws_ird_kinematics sdurws_ird_kinematics_plugin sdurws_ird_testkit GTest::gtest Qt6::Core)

# 插件私有头解析（契约测试消费 plugin/assembly 头——同单元 PRIVATE 面）。
target_include_directories(sdurws_ird_kinematics_contract_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

# 源码树位置注入（构建图扫描用例同 _test 口径）。
target_compile_definitions(sdurws_ird_kinematics_contract_test PRIVATE
    IRD_KINEMATICS_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# 运行期 DLL 拷贝（vcpkg gtest 动态库——同 _test 目标的 POST_BUILD 口径）。
add_custom_command(TARGET sdurws_ird_kinematics_contract_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_kinematics_contract_test>
            $<TARGET_FILE_DIR:sdurws_ird_kinematics_contract_test>
    COMMAND_EXPAND_LISTS)

ird_add_gtest(sdurws_ird_kinematics_contract_test)

# ---- ctest LABELS "ird"（modeling/ui/requirements 落位同款）----
# 验收过滤口径：ctest -L "^ird$"——两测试（模型层、进程内、确定性）同
# 标签；_gui_test 形态本单元无（插件目标随 WP-15-T12 另登记）。
set_tests_properties(sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test
    PROPERTIES LABELS "ird")

# ---- 依赖红线守卫（配置期断言——卡 §3.2，core/io/modeling/requirements
#      foreach 同款）----
# kinematics 的 industrialrobot 单元依赖仅限六条登记边：core＋diagnostics
# ＋runtime＋policy＋evidence＋execution（卡 §3.2 边表——ARCH §3.5 业务
# 域→L2/L3 公共接口许可方向的实例化，白名单 "kinematics->…" 六行同源）；
# 链接到任何其他 sdurws_ird_* 目标（业务域单元＝R-1 命中面；表外平台
# 单元＝SUB 命中面）即配置失败。零 Qt（R-3/NFR-MNT-01）。产品库本体零
# gtest/testkit（D-06/T-1——断言宏只在消费方测试目标展开；本守卫遍历的
# 是产品目标 sdurws_ird_kinematics 的链接面，测试目标的 gtest/testkit 边
# 不在其内）。两面读取（LINK_LIBRARIES 含 PRIVATE 边＋
# INTERFACE_LINK_LIBRARIES 含 PUBLIC 边）防漏。属性未设置时
# get_target_property 返回 "<var>-NOTFOUND" 字面串，不匹配任何模式，不
# 会误报。testkit 不在第一条模式中逐字列出（modeling 同款先例）：
# sdurws_ird_testkit 落在通配 "^sdurws_ird_" 的拦截范围（≠六条登记边即
# FATAL），避免守卫正则字样干扰契约测试的 T-1 链接语句扫描
# （BuildGraphContractTest 的 NoTestkitEdge 用例以"逐行只认测试目标链接
# 语句"钉住产品目标零 testkit）。正式全仓门禁（依赖图白名单）归
# ird_gates（WP-01-T01），本守卫不替代它。
get_target_property(_ird_kinematics_link_libs sdurws_ird_kinematics LINK_LIBRARIES)
get_target_property(_ird_kinematics_iface_libs sdurws_ird_kinematics INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_kinematics_link_libs _ird_kinematics_iface_libs)
    if(_lib MATCHES "^sdurws_ird_"
       AND NOT _lib STREQUAL "sdurws_ird_core"
       AND NOT _lib STREQUAL "sdurws_ird_diagnostics"
       AND NOT _lib STREQUAL "sdurws_ird_runtime"
       AND NOT _lib STREQUAL "sdurws_ird_policy"
       AND NOT _lib STREQUAL "sdurws_ird_evidence"
       AND NOT _lib STREQUAL "sdurws_ird_execution")
        message(FATAL_ERROR
            "kinematics 依赖红线（R-1/卡 §3.2）：sdurws_ird_kinematics "
            "仅可链接六条登记边 core/diagnostics/runtime/policy/evidence/"
            "execution（units/kinematics.md §3.2、ARCH §3.5；含 testkit——"
            "T-1 产品目标禁链），发现 ${_lib}")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "kinematics 依赖红线（R-3/NFR-MNT-01，计算库零 Qt——卡 §3.1）："
            "发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "kinematics 依赖红线（D-06）：产品库本体不链接 gtest，发现 ${_lib}")
    endif()
endforeach()
