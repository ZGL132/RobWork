# =====================================================================
# selection 单元（L4 业务域：电机—减速器选型——目录/硬筛选/组合校核/
# 可行集/回填）构建脚本
#
# 设计依据：
#   - units/selection.md §2.3（单元定位——L4 业务域二分结构：计算库零 Qt
#     ＋Qt Widgets 插件）、§3.2（依赖关系表——编译链接边仅 core＋evidence
#     两条；policy/io/project/diagnostics/ui/runtime/drivetrain 列"运行时
#     注入/端口"列明文不落编译链接边；插件目标→本计算库〔单元内〕＋Qt
#     Widgets；R-1 业务域互链自查行）、§3.4（插件零计算红线——机器可
#     断言）、§3.5（目标布局——sdurws_ird_selection STATIC 零 Qt＋
#     _plugin 插件目标＋_test/_contract_test；_worker 不默认创建——
#     D-SEL-13）、§16（WP-19-T02 行——零 Qt 计算库＋_plugin；公共头与
#     私有实现分离；注册 _test/_contract_test；README 文案修正）
#   - development-task-breakdown.md §5.1（双模式/目标命名/C++17 不用
#     C++20）、§5.3（全局禁止项）、§5.5（gtest 接入定稿）
#   - ARCHITECTURE.md §3.3（业务域单元二分结构）、§3.5（依赖表——各业务
#     域单元→L2/L3 公共接口的许可方向；两边为其实例化，登记于
#     cmake/ird_gates_whitelist.cmake 的 "selection->…" 两行并同步刷新
#     dependency-graph.json）
#   - 需求 NFR-MNT-01（计算内核零 Qt——selection 计算库为 L4 业务计算库；
#     模型测试直调）
#   - 任务契约 tasks/foundation/WP-19-T02.json（本文件即其 acceptance
#     1/2 的落地面：INTERFACE 占位→STATIC＋_plugin/_test/_contract_test
#     同批创建＋SEL-* 域诊断码登记表＋二分结构扫描）
#
# 本文件随 WP-19-T02 落地（selection 由上级 INTERFACE 占位升级为真实库）：
#   1. sdurws_ird_selection STATIC（目标名与别名 RWS::ird::selection
#      不变，DTB §5.1）；T02 源码集＝src/DiagCodes.cpp——SEL-* 码值常量
#      的登记清单实现（17 码：卡 §2.2 一码＋§5.3 八码＋§6.2 一码＋§6.3
#      四码＋§9.3 三码；§10.3 淘汰原因词表的逐 token 稳定码映射随
#      WP-19-T06——见 DiagCodes.hpp 文件头注"登记范围口径"）。T03+ 源码
#      集（CatalogTypes/CatalogValidation/CapabilityCurve/Screening/
#      CombinationCheck/FeasibleSet/CatalogDiff/Backfill——卡 §3.1 组成
#      表＋§3.5 布局表）随各自任务增列，本任务不预建占位（NFR-MNT-04）；
#   2. 插件界面目标 sdurws_ird_selection_plugin（T02 即落位——契约
#      acceptance 1 明文创建）：最小可注册实现＝assembly/ 自持描述符
#      门面（pluginId/titleKey 两字段均有 ui.md §11.1/§3.5 登记出处），
#      零业务计算逻辑（卡 §3.4）。与 modeling/requirements/kinematics/
#      workflow 插件门面的差异（诚实登记，非遗漏）：selection.md §3.2
#      插件依赖仅"本计算库＋Qt Widgets"——零 ui 编译边（树/属性页协议
#      列运行时注入列），故门面不实现 ui::IPluginUiModule、不携
#      PluginUiDescriptor；插件域供给协议（IUiTreeNodesProvider/
#      IUiPropertyPagesProvider——卡 §3.1 点名 UI-T21/T22 协议）与面板
#      （目录管理/筛选条件/候选表/淘汰原因/回填入口）随 WP-19-T10 以
#      真实 ui 类型落位；
#   3. 测试目标 sdurws_ird_selection_test 与
#      sdurws_ird_selection_contract_test（自持 ird_add_gtest 宏：
#      add_test＋<target>_report XML——core/io/drivetrain/workflow 同款；
#      ctest LABELS "ird"）；
#   4. 配置期依赖红线守卫（R-1/R-3 的 selection 侧断言——卡 §3.2；全仓
#      正式门禁归 ird_gates/WP-01-T01，本守卫不替代它）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制；随本任务首个测试目标引入）----
# gtest 经 vcpkg 安装（gtest:x64-windows，经典模式）；REQUIRED＝找不到即
# 配置失败（失败即停，不静默跳过测试目标）。不消费框架的 USE_gtest/RW::
# gtest（从未生成），也不源码 vendor——两份 gtest 混用会破坏 ODR。
# 独立冒烟模式必须带 vcpkg toolchain 参数方可解析（CORE-T01 G-2 口径）。
find_package(GTest CONFIG REQUIRED)

# ---- Qt6 接入（WP-19-T02——插件目标的 Qt 载体面，R-3 唯一例外面）----
# 仅插件目标消费 Qt（卡 §3.2"selection_plugin→Qt Widgets 允许（L4 插件
# 界面目标）——R-3 例外边界＝插件目标；计算库零 Qt"；workflow WP-22-T02
# 同款先例）。组件＝Core/Gui/Widgets（Widgets 是 WP-19-T10 目录管理页/
# 筛选条件/候选表/淘汰原因视图的面板本体、Gui/Core 为 Qt 基面；不取
# Test——本单元无 GUI 测试目标，GUI 用例随 WP-19-T10/T11——卡 §15.3
# Windows GUI 约束）。REQUIRED 同 gtest：找不到即配置失败（不静默跳过
# 插件目标）。独立冒烟模式必须带 CMAKE_PREFIX_PATH 指向 Qt 安装（编排
# 口径：Qt6 位于 D:/software/Qt/6.11.1/msvc2022_64）。计算库
# sdurws_ird_selection 保持零 Qt——R-3/NFR-MNT-01：产品面（include/＋
# src/）源码零 Qt 包含、链接面零 Qt 目标（BuildRedLineTest 与配置期
# 守卫双面钉住）。
find_package(Qt6 COMPONENTS Core Gui Widgets REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC（目标名不变，DTB §5.1）----
# T02 源码集＝src/DiagCodes.cpp（SEL-* 17 码登记清单——码值常量的唯一
# 实现翻译单元；逐码落值依据见该文件逐码注释）。T03 增列四翻译单元（卡
# §3.5 布局表："目录包业务模型与版本身份/业务校验/分段线性插值/快照
# 供给"——WP-19-T03 交付面）：
#   src/CatalogTypes.cpp      §4.1 数据模型＋§4.2 规范序列化与内容身份
#   src/CatalogValidation.cpp §5.3 业务校验全表＋§6.3 曲线校验＋快照装配
#   src/CapabilityCurve.cpp   §6.2 分段线性插值（默认禁止外推）
#   src/CatalogProvider.cpp   §14.1 锁定供给（内存形态）＋P-IO-7 注册面
# T04 增列筛选实现翻译单元（卡 §3.1 组成表 Screening 行——WP-19-T04
# 交付面"电机/减速器硬筛选（§7/§8 全维度）"）：
#   src/Screening.cpp         §7 电机九维＋§8 减速器九维＋§10.1 记录类型
#                             ＋§10.3 词表映射＋§10.4 稳定排序（公共头
#                             include/sdurws/ird/selection/Screening.hpp）
# 组合校核/可行集/目录差异/回填等实现翻译单元随 WP-19-T05~T09 按卡
# §3.5 布局表增列（本任务不预建占位实现，NFR-MNT-04 不建无边界价值
# 包装器）。
add_library(sdurws_ird_selection STATIC
    src/DiagCodes.cpp
    src/CatalogTypes.cpp
    src/CatalogValidation.cpp
    src/CapabilityCurve.cpp
    src/CatalogProvider.cpp
    src/Screening.cpp
# T05 增列组合校核实现翻译单元（卡 §3.1 组成表 CombinationCheck 行——
# WP-19-T05 交付面"组合校核：兼容＋负载惯量＋每轴工作点（共享
# DriveTrainMappingEvaluator）＋多工况＋峰值/RMS＋质量成本＋惯量比配置"；
# 公共头 include/sdurws/ird/selection/Combination.hpp——§14.5 组合构造
# ＋§9.2 管线②评估器＋P-SEL-1 提议契约 v1 承载）：
#   src/CombinationCheck.cpp  §9 组合校核（组合键/构造/传动参数构造/
#                             canonical 编解码/校核核心/③端口评估器）
# T06 增列可行集实现翻译单元（卡 §3.1 组成表 FeasibleSet 行——WP-19-T06
# 交付面"可行集与淘汰原因输出（§10；EVI-01 选型 Profile 注册）"；公共头
# include/sdurws/ird/selection/FeasibleSet.hpp——§14.6 可行集组装＋淘汰
# 原因构造＋§10.1 结果类型＋SEL-06 指标＋sel 域 Profile 实例）：
#   src/FeasibleSet.cpp       §10 可行集组装（分层资格/EVI-02 覆盖复核/
#                             稳定排序/diagRef 回填）＋§14.6 淘汰原因
#                             构造＋§17.3 裕量计算＋Profile 实例化
    src/CombinationCheck.cpp
    src/FeasibleSet.cpp
# T07 增列企业偏好分轨与目录差异比较实现翻译单元（卡 §3.1 组成表
# CatalogDiff 行＋§13.6/§10.4 分轨——WP-19-T07 交付面"优选品牌/供应状态/
# 系列限制＋目录差异比较＋项目锁定版本"；公共头 include/sdurws/ird/selection/
# Preference.hpp 与 CatalogDiff.hpp——SEL-07 偏好过滤器＋SEL-08 结构化差异）：
#   src/Preference.cpp        §10.2/§10.4 分轨（SEL-07——企业偏好配置/候选
#                             企业标注/偏好过滤输出/PreferenceFilter 五步语义；
#                             user-preference-filtered 独立 token 分轨，零改写
#                             硬筛选记录）
#   src/CatalogDiff.cpp       §13.6 目录差异比较（SEL-08——型号三态＋字段级
#                             增量＋曲线点集增量＋兼容关系增量；D-SEL-14 会话
#                             工具，不进③端口注册表）
# T09 增列回填实现翻译单元（卡 §3.1 组成表 Backfill 行——WP-19-T09 交付
# 面"回填命令与合成（§12；ProjectCommandService；壳体/转子区分不重复；
# 复算提示；StaleRevisionRejected）"；公共头 include/sdurws/ird/selection/
# Backfill.hpp——§14.8 处理器＋§3.1 回填数据组装＋§12.4 合成内核）：
#   src/Backfill.cpp          §12.4 物性合成（平行轴五步＋MDL-06①~③同语义
#                             断言——壳体进合成/转子独立登记不重复计入）＋
#                             命令/记录载荷 canonical 编解码（IRDSBFP1/
#                             IRDSBFV1）＋回填数据组装＋§14.8 处理器
#                             （planFromEnvelope 内核＋prepare 直通壳——
#                             include project 公共头实现 ICommandHandler
#                             接口、零 project 实现符号引用，链接面不变；
#                             卡 §3.2 增量修订与两红线测试同步——见
#                             Backfill.hpp 文件头"依赖形态登记"）
    src/Preference.cpp
    src/CatalogDiff.cpp
    src/Backfill.cpp)

# C++17（core.md 决策 D-01 同源）：逐目标显式设定，不设全局标准。
# PUBLIC 传播：selection 公共头使用 C++17 设施（inline constexpr 变量
# 等），消费者编译 selection 头也必须至少 C++17。
target_compile_features(sdurws_ird_selection PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_selection PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：本单元源文件全部为 UTF-8（无 BOM）且含中文注释（AGENTS.md §2
# 注释规范）。MSVC 默认按系统代码页（GBK）解读无 BOM 源文件，会把 UTF-8
# 中文误读成乱码 token（core 落位时曾出现 C2001"常量中有换行符"），必须
# 显式 /utf-8。PUBLIC 传播：公共头同为 UTF-8，消费者编译含 selection
# 头的翻译单元也需要。
if(MSVC)
    target_compile_options(sdurws_ird_selection PUBLIC /utf-8)
endif()

# 公共头根：仅 selection/include（内部含 sdurws/ird/selection/）。T02
# 公共面＝DiagCodes.hpp（SEL-* 17 码登记表——契约 acceptance 2）；卡
# §3.5 布局表八头（CatalogTypes/CatalogProvider/Curve/Screening/
# Combination/FeasibleSet/ResultFacts/Backfill）随 T03+ 任务增列。R-2
# 红线：私有实现头不得置于 include/——本目录是 selection 唯一对外
# 的头暴露位，实现细节头（若有）只能放 src/ 并以相对路径被实现文件包含。
# T09 增列：project 公共头 include 路径（PUBLIC——Backfill.hpp 的
# #include <sdurws/ird/project/CommandService.hpp> 是公共头的传递依赖，
# 消费者必须可解析）。注意边界：这是 **include 路径不是链接边**——
# 卡 §3.2 零 project 编译链接边不变（下方依赖红线守卫与 ird_gates SUB
# 双面钉住链接面；ird_gates 第 3 步只拒私有面〔src/test〕路径，公共面
# include 路径合规）。零实现符号引用纪律见 Backfill.hpp 文件头
# "依赖形态登记"。
target_include_directories(sdurws_ird_selection PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../project/include>)

# 编译依赖：core＋evidence（卡 §3.2 边表两条编译链接边——ARCH §3.5"各
# 业务域单元→L2/L3 公共接口"许可方向的实例化，ird_gates_whitelist.cmake
# 的 "selection->core"/"selection->evidence" 两行随本任务登记并同步刷新
# dependency-graph.json——双面留痕）。
# PUBLIC 形态：两边本任务"仅链接目标不消费上游公共头"（P-RPT-9/kinematics/
# drivetrain/workflow T02 同款先例——边随落位登记，消费随 T03+ 任务回填：
# core ObjectId/ContentIdentity/DiagCode 句法/单位换算/比较工具→T03 目录
# 模型与导入校验输入面、evidence IEngineeringEvaluator/EvaluationRequest/
# ResultEnvelope 契约→T05 sel.combination-check 评估器适配面；两边统一
# PUBLIC 保持形态一致——契约 acceptance 1 依赖面全建立）。
# 用统一真名 sdurws_ird_<unit>：集成与独立冒烟两种模式下都可解析（别名
# RWS::ird::* 仅集成模式存在——kinematics/drivetrain 落位时同款取舍）。
# 零 Qt（R-3/NFR-MNT-01——_plugin 目标为唯一 Qt 例外载体）、零其他业务
# 域单元（R-1——selection 不直链 modeling/requirements/kinematics/
# trajectory/dynamics/optimization；drivetrain 映射经③评估器端口消费、
# dynamics 关节侧事实经上游切片值传递〔卡 §3.3/§9.1——P-SEL-2 推荐端口
# 形态〕）、零表外平台单元（卡 §3.2"运行时注入/端口"列点名的 policy/
# io/project/diagnostics/ui/runtime/drivetrain 七单元＝零编译链接边——
# 诊断注册面经 L5 装配收编、命令经①端口、io 交接经注入；diagnostics 边
# 尤须注意：kinematics 六边含 diagnostics 而本卡 §3.2 明文不落——SEL-*
# 登记表以 core 句法承载，DiagCodes.hpp 文件头注有论证）、零 testkit
# （T-1——testkit 仅随测试目标消费）、零 sdurw_proximity（R-5——碰撞
# 唯一实现归 policy，与本单元无交集）。
target_link_libraries(sdurws_ird_selection PUBLIC
    sdurws_ird_core
    sdurws_ird_evidence)

# 别名（集成模式）：供其余单元以 RWS::ird::selection 消费；冒烟模式无
# 别名（与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::selection ALIAS sdurws_ird_selection)
endif()

# ---- 插件界面目标 sdurws_ird_selection_plugin（WP-19-T02 落位——契约
#      acceptance 1 明文创建；卡 §3.5 目标表）----
# 最小可注册实现（零业务计算逻辑——卡 §3.4）：SelectionPluginAssembly
# ＝自持描述符装配门面（assembly/ 公共头——pluginId/titleKey 均有
# ui.md §11.1/§3.5 登记出处；差异论证见 assembly 头文件注与本文头部
# 第 2 点）。源码布局＝plugin/ 目录（门禁 R-3 产品面扫描域为 include/**＋
# src/**，plugin/ 不在扫描域——Qt 红线的文件域隔离，modeling/requirements/
# kinematics/workflow 同款先例）；装配契约头＝assembly/（plugin 目标的
# PUBLIC include 面——宿主装配层只见门面头，零插件私有头依赖——R-2；
# workflow O-45 教训前移，落位即建）。本批次零 Q_OBJECT 类（界面控件随
# WP-19-T10 落位时引入），故不开 AUTOMOC；首个 Q_OBJECT 类落位任务按
# modeling/requirements 先例置 AUTOMOC ON（DTB §5.1 v0.17 行口径）。
add_library(sdurws_ird_selection_plugin STATIC
    plugin/SelectionPluginAssembly.cpp   # 装配门面实现（assembly/
                                         #   公共头的私有实现——WP-19-T10
                                         #   扩展：描述符现产＋模块装配 API）
# T10 增列插件界面源（卡 §16 WP-19-T10 行——工作流页/目录管理页/候选表
# 页的模型半区＋呈现本体；全部零 Q_OBJECT，AUTOMOC 保持 OFF——DTB §5.1
# v0.17 行口径）：
#   plugin/SelPanelTypes.hpp        零 Qt 值类型半区（服务缝聚合/会话态/
#                                   呈现 DTO——头文件-only）
#   plugin/SelPanelModel.hpp/.cpp   零 Qt 模型层流 L-S1~L-S5（就绪投影/
#                                   行集归一/回填提交/文案守卫）
#   plugin/SelPanelCommandCatalog.hpp/.cpp  域命令目录（回填入口一条——
#                                   token 自持常量，契约测试对账钉住）
#   plugin/SelPanelModule.hpp       模块（缝/会话态/面板引用——头文件-
#                                   only；createPanel 实现在门面 TU）
#   plugin/SelCatalogPanelWidget.hpp/.cpp  主面板（三页合一 Tab——零业务
#                                   计算，数据消费全经模型层与缝）
    plugin/SelPanelModel.cpp
    plugin/SelPanelCommandCatalog.cpp
    plugin/SelCatalogPanelWidget.cpp)
set_target_properties(sdurws_ird_selection_plugin PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_selection_plugin PRIVATE /utf-8)
endif()

# 插件私有头解析（plugin/*.hpp——PRIVATE，不外溢；modeling/ui 同款先例；
# T02 仅单实现 TU，仍按先例登记以稳定后续批次形态）。
target_include_directories(sdurws_ird_selection_plugin PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})
# 装配层（宿主插件）消费 <sdurws/ird/selection/SelectionPluginAssembly.hpp>
# ——PUBLIC include 面（非 include/ 产品扫描域——零 Qt 红线文件域隔离同
# plugin/ 口径）。
target_include_directories(sdurws_ird_selection_plugin PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/assembly>)

# 链接面（卡 §3.2 插件两行——"插件目标→本计算库〔单元内依赖，非跨单元
# 边〕"＋"→Qt Widgets 允许〔R-3 例外边界＝插件目标〕"）：
#   - sdurws_ird_selection＝本计算库（门面/后续面板消费域设施的唯一来源；
#     自边 selection->selection 不进 §3.5 单元级依赖表（表语义＝单元间
#     边）——ird_gates 引擎按"插件目标链接本单元产品目标"装配自边豁免
#     放行〔ird_gates.cmake 2a 形态二 _self_edge 分支〕，与 modeling/
#     requirements/kinematics/workflow 四 plugin 同款形态）；
#   - Qt 三件套＝plugin 分类面（ird_gates R-3 仅查 product/worker 形态；
#     T02 源码零 Qt 类消费，链接面先行建立——WP-19-T10 面板批次直接
#     增列源文件即可）；
#   - 零 sdurws_ird_ui（卡 §3.2 无 ui 编译边——与 modeling/requirements/
#     kinematics 插件的"→本计算库＋sdurws_ird_ui"三边形态不同：selection
#     树/属性页协议归运行时注入列，宿主注册随 WP-19-T10 以 UI-T21/T22
#     协议落位；届时若需 ui 编译边，先走单元卡增量修订＋白名单增登）。
target_link_libraries(sdurws_ird_selection_plugin PUBLIC
    sdurws_ird_selection
    Qt6::Core Qt6::Gui Qt6::Widgets)

# 别名（集成模式）：供其余单元以 RWS::ird::selection_plugin 消费；冒烟
# 模式无别名（DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::selection_plugin ALIAS sdurws_ird_selection_plugin)
endif()

# ---- 测试目标 sdurws_ird_selection_test（随 WP-19-T02 登记）----
# 用例面（任务契约 acceptance 逐条具名自证）：
#   test/DiagCodesTest.cpp        SEL-* 17 码登记表全表清单＋码值句法
#                                 （经 core::DiagnosticRecord 句法权威
#                                 校验）＋登记出处完整性（acceptance 2
#                                 ——DiagCodes.hpp 行；以 core 句法面
#                                 验证，真实 StableCodeRegistry 注册执行
#                                 面归 L5 装配收编——SEL 前缀已在册无
#                                 前缀阻塞，见 DiagCodes.hpp 文件头注）
#   test/BuildRedLineTest.cpp     产品面源码红线运行期扫描（零 Qt＋两登记
#                                 边 include 面＋R-4 字面量＋文件头注释
#                                 ——acceptance 1/2，与文件末尾配置期守卫
#                                 互为两道防线；drivetrain/workflow 同款
#                                 四用例）
# 测试 main 自持＝testkit 报告设施（TestMainReport.cpp 安装 TestRecord
# Listener——ird-test-report.json 用例级明细与 gtest XML 并存；IO-T07/
# PRJ-T15/drivetrain/workflow 同款先例；IRD_TEST_INFO 携带需求/AT
# 追溯字段）。零 QCoreApplication 构造（drivetrain T02 同款诚实登记）：
# _test 目标零 Qt 消费（不链 _plugin），测试面无 Qt 类型。
enable_testing()

add_executable(sdurws_ird_selection_test
    test/TestMainReport.cpp
    test/DiagCodesTest.cpp
    test/BuildRedLineTest.cpp
    test/CatalogImportTest.cpp
    test/CapabilityCurveTest.cpp
    test/CatalogLockTest.cpp
    test/ScreeningMotorTest.cpp
    test/ScreeningGearboxTest.cpp
    test/CombinationBuilderTest.cpp
    test/CombinationCheckTest.cpp
    test/CombinationCheckCodecTest.cpp
    test/CombinationCheckEvaluatorTest.cpp
# T06 增列可行集/淘汰原因用例组（WP-19-T06 acceptance 1/2——SEL-06
# 输出面＋EVI-02 多工况资格；接口路径钉扎：经 IFeasibleSetBuilder/
# IRejectionReasonProvider 引用消费，不留只测自由函数的盲区）：
#   test/FeasibleSetTest.cpp
# T07 增列企业偏好/目录差异用例组（WP-19-T07 acceptance 1/2——SEL-07
# 分轨语义〔含"不改变硬能力判定"核心用例：经真实 HardConstraintSelector
# 产出记录验证 apply 零改写〕＋SEL-08 三态差异/字段级/曲线点集/兼容增量
# ＋锁定×比较历史隔离组合用例；接口路径钉扎：经 IRejectionReasonProvider
# 注入计数 Provider 证明偏好原因的接口构造路径）：
#   test/PreferenceFilterTest.cpp
#   test/CatalogDiffTest.cpp
    test/FeasibleSetTest.cpp
    test/PreferenceFilterTest.cpp
    test/CatalogDiffTest.cpp
# T08 增列移动关节范围外诊断用例组（WP-19-T08 acceptance 1/2——SEL-09
# 范围纪律：移动关节轴阻断〔DataInsufficient＋SEL-INPUT-AXIS-OUT-OF-SCOPE〕
# ＋不静默套用旋转传动＋混合链逐轴独立＋同轴矛盾 fail-fast；接口路径
# 钉扎：经 IHardConstraintSelector 接口引用消费）：
#   test/AxisOutOfScopeTest.cpp
    test/AxisOutOfScopeTest.cpp
# T09 增列回填用例组（WP-19-T09 acceptance 1/2——SEL-10/MDL-16/AT-30：
# 合成黄金数值＋壳体/转子分轨不重复计入＋MDL-06①~③断言失败拒绝＋载荷
# 往返＋组装拒绝族＋计划内核三态＋复算提示＋token 无点词形；接口路径
# 钉扎：经 IDeviceBackfillCommandHandler 接口引用消费 planFromEnvelope/
# commandType/currentPayloadVersion——prepare 壳需 HandlerContext〔构造
# 符号在 project 库〕，单元内经内核方法全覆盖，见 BackfillTest 文件头注）：
#   test/BackfillTest.cpp
    test/BackfillTest.cpp
# T10 增列界面链路模型层用例组（WP-19-T10 acceptance 1/2——工作流页/
# 目录管理页/候选表页的 L-S1~L-S5 流＋装配登记面值断言＋回填提交流
# 三态＋文案守卫；接口路径钉扎：经 SelectionPluginAssembly 门面引用
# 消费 setServices/bind*/session/module——不留只测自由函数的盲区；
# GUI 呈现归 sdurws_ird_selection_app 通道，envUnavailable 如实登记）：
#   test/SelPluginPanelTest.cpp
# T11 增列黄金数据集消费用例组（WP-19-T11 acceptance 1/2——AT-08/AT-30
# 主链路面：目录包正/误样例＋硬筛选黄金表＋组合校核黄金＋回填合成黄金；
# 期望值全部经 testkit GoldenFixture 从 testdata/golden/sel-* 数据文件
# 装载（独立参考实现产物——与产品实现零共享），本测试只做"装载→驱动
# 产品入口→黄金对照"；接口路径钉扎：经 ICatalogValidator/ICatalogProvider/
# IHardConstraintSelector/checkCombinations/IFeasibleSetBuilder/
# assembleDeviceBackfill/IDeviceBackfillCommandHandler 公共类消费）：
#   test/SelGoldenDatasetTest.cpp
    test/SelPluginPanelTest.cpp
    test/SelGoldenDatasetTest.cpp)

target_compile_features(sdurws_ird_selection_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_selection_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（/utf-8 说明见产品目标处；gtest 头为
# ASCII 不受影响）。显式再设一次以自洽——测试目标应独立可读，不依赖
# 被测目标的编译选项细节。
if(MSVC)
    target_compile_options(sdurws_ird_selection_test PRIVATE /utf-8)
endif()

# 测试链接产品库 sdurws_ird_selection 同时验证"公共头可被正常 include
# ＋库可链接"（两登记边 PUBLIC 传染链随目标自动到达——core/evidence 的
# 链接语义经本链路在链接器层面自证）。
# T10 增链 sdurws_ird_selection_plugin＋Qt6::Core/Gui（dynamics WP-17-T09
# 同款——界面链路用例 SelPluginPanelTest 消费 plugin/ 呈现模型半区与
# 装配门面；模型层流零 Qt 消费，Qt 链接为 _plugin 符号解析的 DLL 面；
# 产品面红线扫描用例面不受影响——扫描域为 include/＋src/ 产品源码）。
# sdurws_ird_testkit＝报告/追溯设施消费面（RecordListener/IRD_TEST_INFO
# ——T-1 允许形态：仅测试目标链 testkit，产品目标不链）。单行链接语句
# ＝契约测试 T-1 扫描的登记形态约束（testkit 与测试目标名同行可见——
# kinematics/reporting/drivetrain/workflow 同款）。
target_link_libraries(sdurws_ird_selection_test PRIVATE sdurws_ird_selection sdurws_ird_selection_plugin sdurws_ird_testkit GTest::gtest Qt6::Core Qt6::Gui)

# 源码树位置注入：红线扫描用例在运行期扫描 selection 源码核对红线
# （零 Qt/零表外 include——卡 §3.2/§14.0）。经编译定义传 industrialrobot
# 根（kinematics/io/workflow/drivetrain 同款——值内以单元名作子目录
# 前缀，扫描面随单元自包含）。
target_compile_definitions(sdurws_ird_selection_test PRIVATE
    IRD_SELECTION_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# 插件私有头解析（T10：测试消费 plugin/ 呈现模型头 SelPanelModel 等——
# 同单元 PRIVATE 面；plugin/ 子目录直指——裸名 include 解析；dynamics
# WP-17-T09 先例同款）。
target_include_directories(sdurws_ird_selection_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/plugin)

# vcpkg 的 gtest:x64-windows 是动态库（gtest.dll/gtest_main.dll）：把测试
# 可执行文件的运行期 DLL 拷到其输出目录，保证不经 PATH 配置即可直接运行
# （留痕与验收都要直接执行该可执行文件）。TARGET_RUNTIME_DLLS 需 CMake
# ≥3.21，本仓库工具链为 VS2022 自带的更新版本，满足。
add_custom_command(TARGET sdurws_ird_selection_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_selection_test>
            $<TARGET_FILE_DIR:sdurws_ird_selection_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
# 框架原宏见 RobWork/RobWork/gtest/CMakeLists.txt：add_test ＋ <target>_
# report（运行测试并落 gtest XML）。本宏按 DTB §5.5 在本单元 CMake 内
# 定义（定义先于全部调用——CMake 顺序解析；core/io/kinematics/workflow/
# drivetrain 同款形态，不做跨单元共享——selection 是业务域单元，不提供
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

ird_add_gtest(sdurws_ird_selection_test)

# ---- 契约测试目标 sdurws_ird_selection_contract_test（随 WP-19-T02
#      登记——契约 acceptance 1 明文创建）----
# 落位期契约面＝构建图边界契约（drivetrain/workflow BuildGraphContract-
# Test 同款扫描形态——两条登记边的存在性/封闭性/无业务域互链/无表外
# 平台边/testkit 仅测试目标/_worker 形态不存在/include 面同界；与
# drivetrain 相反，_plugin 目标落位为正向断言）＋插件可注册契约
# （SelectionPluginAssemblyContractTest——描述符登记出处自证＋卡 §3.4
# 插件零计算红线词表扫描〔筛选/插值/校核/排序符号＋直接文件/项目访问
# 零命中〕——acceptance 1"最小可注册实现"的执行证明；workflow 真实
# IPluginUiRegistrar 校验序形态在此不适用——卡 §3.2 无 ui 编译边，
# 差异论证见该测试文件头注）；业务对端面契约（evidence 评估器注册/
# 切片依赖声明 fake 套件——卡 §15）随 WP-19-T03+ 落地（T02 登记面不触
# 对端契约面，不预建占位用例）。
# 自有 main 同 _test 目标（报告设施接入）。
add_executable(sdurws_ird_selection_contract_test
    contract_test/ContractTestMain.cpp
    contract_test/BuildGraphContractTest.cpp
    contract_test/SelectionPluginAssemblyContractTest.cpp
    contract_test/CatalogIoBoundaryContractTest.cpp
    contract_test/HardScreeningContractTest.cpp
    contract_test/CombinationCheckContractTest.cpp
# T06 增列可行集/淘汰原因契约用例组（WP-19-T06 acceptance 1/3——词表→
# 稳定码映射封闭性＋sel 域 Profile 经真实 EvidenceProfileRegistry 注册
# 闭环＋err-01 字段面契约）：
#   contract_test/FeasibleSetContractTest.cpp
# T07 增列偏好/差异契约用例组（WP-19-T07 acceptance 1/2/3——SEL-07 分轨
# token 词表/稳定码/登记表三方同源＋偏好零侵入硬记录＋SEL-08 会话工具
# static_assert 非评估器（D-SEL-14 不进③端口）＋CON-05 失效链＋锁定表
# 只增历史隔离）：
#   contract_test/PreferenceDiffContractTest.cpp
# T09 增列回填契约用例组（WP-19-T09 acceptance 1/3——P-SEL-3 token 无点
# 词形钉扎〔project §4.4.4 冻结语法 ^[a-z0-9-]{3,64}〕＋T09 批 9 码登记表
# 表尾追加与句法权威校验＋处理器契约面（commandType/payload 版本唯一性）
# ＋记录对象编解码契约（确定性/严格解码）＋AT-30 复算提示与不沿用标志
# 的记录面契约）：
#   contract_test/BackfillContractTest.cpp
# T11 增列黄金数据集登记契约用例组（WP-19-T11 acceptance 2/3——sel-* 四
# 数据集 DatasetManifest 装载全量校验＋登记字段/AT 锚点/独立性声明＋
# integrity 覆盖关系＋sel-golden 容差档案通道与保守纪律；drivetrain
# GoldenDatasetContractTest 同款形态）：
#   contract_test/SelGoldenDatasetContractTest.cpp
    contract_test/FeasibleSetContractTest.cpp
    contract_test/PreferenceDiffContractTest.cpp
    contract_test/BackfillContractTest.cpp
    contract_test/SelGoldenDatasetContractTest.cpp)

target_compile_features(sdurws_ird_selection_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_selection_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（同 _test 目标口径）。
if(MSVC)
    target_compile_options(sdurws_ird_selection_contract_test PRIVATE /utf-8)
endif()

# 链接面与 _test 同款（testkit＝报告设施——T-1 允许形态；单行链接语句
# ＝契约测试 T-1 扫描的登记形态约束，同 _test 目标口径）。
# sdurws_ird_selection_plugin＝同单元插件目标（装配门面——可注册契约
# 用例的被测面；"测试目标仅允许同单元产品目标"纪律下 plugin face 亦属
# 同单元许可集——modeling WP-13-T15/workflow WP-22-T02 先例同款；其
# PUBLIC 面携带 Qt 传染，供后续批次注册端口符号解析预备）。
target_link_libraries(sdurws_ird_selection_contract_test PRIVATE sdurws_ird_selection sdurws_ird_selection_plugin sdurws_ird_testkit GTest::gtest)

# 源码树位置注入（构建图/插件面扫描用例同 _test 口径）。
target_compile_definitions(sdurws_ird_selection_contract_test PRIVATE
    IRD_SELECTION_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# 运行期 DLL 拷贝（vcpkg gtest 动态库＋插件目标 Qt 传染面——同 _test
# 目标的 POST_BUILD 口径）。
add_custom_command(TARGET sdurws_ird_selection_contract_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_selection_contract_test>
            $<TARGET_FILE_DIR:sdurws_ird_selection_contract_test>
    COMMAND_EXPAND_LISTS)

ird_add_gtest(sdurws_ird_selection_contract_test)

# ---- 开发验证 harness：sdurws_ird_selection_app（WP-19-T10——单元卡
#      §15.3 GUI 手动验证通道载体）----
# 先例＝dynamics_app（WP-17-T09）/kinematics_app（WP-15-T12）/modeling_app
# （WP-13-T15）；_app 后缀＝ird_gates 分类面 Qt 合法形态；源码在 app/
# 子目录（产品扫描域零触碰）；本目标非测试（不入 ctest 标签）、非产品
# 交付路径（真实宿主装配层挂位归装配批次收口——P-SEL-10 登记缺口，
# 插件面零改动）。无人值守门禁不做 GUI 运行验证：本目标的**构建**属
# 交付留痕，**运行**属手动点验流程（§15.3——一次只启动一个 GUI 可执行
# 文件、不用 offscreen），未启动如实登记不标注通过（AGENTS §4.2）。
add_executable(sdurws_ird_selection_app
    app/HarnessMain.cpp)

target_compile_features(sdurws_ird_selection_app PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_selection_app PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
if(MSVC)
    target_compile_options(sdurws_ird_selection_app PRIVATE /utf-8)
endif()

# app/ 需解析本单元插件私有头（plugin/SelPanelTypes.hpp 等——同单元内
# 消费，R-2 不跨单元；单元根 include 目录 PRIVATE 注入）。
target_include_directories(sdurws_ird_selection_app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

# 单行链接语句＝构建图契约测试的登记形态约束（dynamics_app 同款）。
target_link_libraries(sdurws_ird_selection_app PRIVATE sdurws_ird_selection_plugin)

# 运行期 DLL 就位（dynamics_app 同款）：TARGET_RUNTIME_DLLS 复制 Qt
# 三件套等共享库到 exe 旁——绝对路径启动免 PATH 依赖。
add_custom_command(TARGET sdurws_ird_selection_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_selection_app>
            $<TARGET_FILE_DIR:sdurws_ird_selection_app>
    COMMAND_EXPAND_LISTS)

# ---- Qt 平台插件定位（dynamics_app 同款坑位修复）：Qt6 官方发行版
#      DLL 为可重定位前缀形态，从副本位置推导的插件目录不成立（qwindows
#      找不到即启动失败）——configure 期从 Qt6_DIR 推导真实 Qt 安装前缀
#      生成 qt.conf 复制到 exe 旁（构建期生成物，不入库）。
set(_ird_sel_qt_prefix "${Qt6_DIR}")
get_filename_component(_ird_sel_qt_prefix "${_ird_sel_qt_prefix}/../../.." ABSOLUTE)
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/sel-harness-qt.conf"
     "[Paths]\nPrefix=${_ird_sel_qt_prefix}\n")
add_custom_command(TARGET sdurws_ird_selection_app POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_BINARY_DIR}/sel-harness-qt.conf"
            "$<TARGET_FILE_DIR:sdurws_ird_selection_app>/qt.conf")

# ---- ctest LABELS "ird"（modeling/ui/requirements/kinematics/workflow/
#      drivetrain 落位同款）----
# 验收过滤口径：ctest -L "^ird$"——两测试（模型层、进程内、确定性）同
# 标签；_gui_test 形态本单元无——GUI 呈现归 sdurws_ird_selection_app
# harness 手动验证通道（WP-19-T10 落位；卡 §15.3 流程，本次未启动如实
# 登记）＋模型层链路用例（SelPluginPanelTest）承载界面链路。
set_tests_properties(sdurws_ird_selection_test sdurws_ird_selection_contract_test
    PROPERTIES LABELS "ird")

# ---- 依赖红线守卫（配置期断言——卡 §3.2，drivetrain/workflow foreach
#      同款）----
# selection 的 industrialrobot 单元依赖仅限两条登记边：core＋evidence
# （卡 §3.2 边表编译链接列——ARCH §3.5 业务域→L2/L3 公共接口许可方向的
# 实例化，白名单 "selection->…" 两行同源）；链接到任何其他 sdurws_ird_*
# 目标（业务域单元＝R-1 命中面；"运行时注入/端口"列平台单元＝SUB 命中
# 面——含卡面点名的 policy/io/project/diagnostics/ui/runtime/
# drivetrain）即配置失败。零 Qt（R-3/NFR-MNT-01——Qt 例外载体仅为
# _plugin 目标，不在本遍历范围）。产品库本体零 gtest/testkit（D-06/T-1
# ——断言宏只在消费方测试目标展开；本守卫遍历的是产品目标
# sdurws_ird_selection 的链接面，测试目标的 gtest/testkit 边不在其内）。
# 两面读取（LINK_LIBRARIES 含 PRIVATE 边＋INTERFACE_LINK_LIBRARIES 含
# PUBLIC 边）防漏。属性未设置时 get_target_property 返回
# "<var>-NOTFOUND" 字面串，不匹配任何模式，不会误报。testkit 不在第一
# 条模式中逐字列出（drivetrain 同款先例）：sdurws_ird_testkit 落在通配
# "^sdurws_ird_" 的拦截范围（≠两条登记边即 FATAL），避免守卫正则字样
# 干扰契约测试的 T-1 链接语句扫描（BuildGraphContractTest 的
# NoTestkitEdge 用例以"逐行只认测试目标链接语句"钉住产品目标零
# testkit）。正式全仓门禁（依赖图白名单）归 ird_gates（WP-01-T01），
# 本守卫不替代它。
get_target_property(_ird_selection_link_libs sdurws_ird_selection LINK_LIBRARIES)
get_target_property(_ird_selection_iface_libs sdurws_ird_selection INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_selection_link_libs _ird_selection_iface_libs)
    if(_lib MATCHES "^sdurws_ird_"
       AND NOT _lib STREQUAL "sdurws_ird_core"
       AND NOT _lib STREQUAL "sdurws_ird_evidence")
        message(FATAL_ERROR
            "selection 依赖红线（R-1/卡 §3.2）：sdurws_ird_selection "
            "仅可链接两条登记边 core/evidence（units/selection.md §3.2、"
            "ARCH §3.5——policy/io/project/diagnostics/ui/runtime/"
            "drivetrain 为卡面\"运行时注入/端口\"列零编译边，含 testkit"
            "——T-1 产品目标禁链），发现 ${_lib}")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "selection 依赖红线（R-3/NFR-MNT-01，计算库零 Qt——卡 §2.3/"
            "§3.2，Qt 例外载体仅 _plugin 目标）：发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "selection 依赖红线（D-06）：产品库本体不链接 gtest，发现 ${_lib}")
    endif()
endforeach()
