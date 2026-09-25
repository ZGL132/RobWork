# =====================================================================
# modeling 单元（业务域：机器人方案业务建模）构建脚本
#
# 设计依据：
#   - units/modeling.md §3.1（单元组成——计算库 STATIC 零 Qt＋两测试目标）、
#     §3.2（目标升级与门禁边登记——WP-13-T02 执行清单：六条接口依赖边/
#     配置期红线守卫/_plugin 随 WP-13-T15 落位）、§3.3（公共头布局——
#     T02 行：ObjectTypes/Errors/DiagCodes 三头）、§3.4（纯函数服务确定性
#     与 SI 总约定）
#   - development-task-breakdown.md §5.1（双模式/目标命名/C++17 不用
#     C++20）、§5.3（全局禁止项）、§5.5（gtest 接入定稿）
#   - ARCHITECTURE.md §3.3（业务域单元二分结构）、§3.5（依赖表——
#     各业务域单元→L2/L3 公共接口的许可方向；六边为其实例化，登记于
#     cmake/ird_gates_whitelist.cmake 的 IRD_ALLOWED_UNIT_EDGES）
#   - 需求 NFR-MNT-01（计算内核零 Qt）、ARC-02（架构可维护）
#   - 任务契约 tasks/foundation/WP-13-T02.json（本文件即其 acceptance 1/2
#     的落地面；acceptance 3 的白名单数据面同批登记）
#
# 本文件随 WP-13-T02 落地（modeling 由上级 INTERFACE 占位升级为真实库）：
#   1. sdurws_ird_modeling STATIC（目标名与别名 RWS::ird::modeling 不变，
#      DTB §5.1）；T02 公共面＝ObjectTypes/Errors/DiagCodes 三头＋对应
#      实现翻译单元（§3.3 表 T02 行；RobotDesign/Codec 等随 T03+ 增列）；
#   2. 测试目标 sdurws_ird_modeling_test 与 sdurws_ird_modeling_contract_
#      test（自持 ird_add_gtest 宏：add_test＋<target>_report XML——
#      core/io 同款；ctest LABELS "ird"）；
#   3. 配置期依赖红线守卫（R-1/R-3 的 modeling 侧断言——卡 §3.2；全仓
#      正式门禁归 ird_gates/WP-01-T01，本守卫不替代它）。
#   _plugin 目标（Qt Widgets）不在本任务：卡 §3.2 明文"随 WP-13-T15 落位"
#   （DTB §2.14 T02 产出列"_plugin 注册"与卡的偏差按单元卡执行，偏差
#   登记见契约 note ②——DTB §5.4 增量修订消账归治理侧）。
# =====================================================================

# ---- gtest 接入（DTB §5.5 定稿，唯一机制）----
# gtest 经 vcpkg 安装（gtest:x64-windows，经典模式）；REQUIRED 意味着找不到
# 即配置失败（失败即停，不静默跳过测试目标）。不消费框架的 USE_gtest/
# RW::gtest（从未生成），也不源码 vendor——两份 gtest 混用会破坏 ODR。
find_package(GTest CONFIG REQUIRED)

# ---- pugixml 接入（O-40——DTB §4 依赖登记，2026-09-22 所有者批次授权）----
# URDF/Xacro DOM 解析（io 不产出 DOM、expat 为其 PRIVATE——P-MDL-5）：经
# vcpkg 经典模式安装 pugixml 1.15→1.16/x64-windows（版本以安装时 vcpkg
# ports 清单为准，登记于任务契约 note 与单元卡 §14.6 实施记录）。
# PRIVATE 链接＝仅 sdurws_ird_modeling 自身编译消费（Import.cpp 内 include
# <pugixml.hpp>，公共头不暴露 DOM 类型——消费者零感知）；DOM 有界性由 io
# BudgetGuard 前置保证（进入 modeling 的字节已经预算入账——O-40 登记语）。
# REQUIRED 语义同 gtest：找不到即配置失败（不静默跳过）。
find_package(pugixml CONFIG REQUIRED)

# ---- 产品库目标：INTERFACE 占位 → STATIC（目标名不变，DTB §5.1）----
# T02 源码集＝三公共头的实现翻译单元（头内 token 常量零运行时成本，
# 无需 TU；函数面按头分文件——io 同款布局）：
#   src/ObjectTypes.cpp  SceneObjectRole 词表转发表（§4.5）
#   src/Errors.cpp       错误码 token 表＋域错误→稳定码映射数据（§9.5 尾段）
#   src/DiagCodes.cpp    MDL-* 码描述符清单＋注册函数（§9.5 T02 行）
# T03 增列（tasks/foundation/WP-13-T03.json，§3.3 表 T03 行）：
#   src/RobotDesign.cpp  根对象值模型实现：枚举 token 表/值相等/双权威
#                        编辑守卫（C-1/C-2）/不变量核查 I-MDL-1～10
#   src/Parts.cpp        四部件值模型实现＋部件级不变量（工具 I-MDL-5、
#                        传动 I-MDL-11/12）
#   src/Codec.cpp        IRobotDesignCodec canonical 编解码（§4.8 序列化
#                        登记——小端/UTF-8/无填充/集合字典序；D-MDL-5
#                        派生字段排除的编码侧声明点）
#   src/InertiaMath.hpp  单元私有头（R-2：不入 include/——惯量 SPD＋三角
#                        不等式单一实现，连杆/工具共用）
# T04 增列（tasks/foundation/WP-13-T04.json，§3.3 表 T04 行）：
#   src/PropertyEstimation.cpp  物性估算唯一公式表 mdl-property-formula/1
#                        （段元闭式公式/平行轴合成/输出自检/密度默认表/
#                        质心确认流域级判定——§5.3/§9.4.6）
# T05 增列（tasks/foundation/WP-13-T05.json，§3.3 表 T05 行）：
#   src/Import.cpp       URDF 导入字段映射与链型判定（IModelImportMapper；
#                        pugixml DOM 解析——O-40 PRIVATE 依赖，§6.1～§6.7/
#                        §9.4.3；mapWorkCellXml R1 边界响应）
# T06 增列（tasks/foundation/WP-13-T06.json，§3.3 表 T06 行）：
#   src/XacroExpand.cpp  Xacro 受控展开语义（IXacroExpandService——属性/宏/
#                        参数声明式替换；护栏复核与 IO-* 码透传，§6.5/
#                        §9.4.9；pugixml 复用 T05 已登记依赖——O-40 不二
#                        次引入渠道）
# T07 增列（tasks/foundation/WP-13-T07.json，§3.3 表 T07 行）：
#   src/Template.cpp     模板创建与参数化编辑（IRobotDesignTemplateFactory
#                        ＋T-MDL-1 六轴默认参数表＋ModelingWorkingSet v1
#                        ＋创建入口守卫〔§6.4 判定复用〕＋逐轴编辑流＋
#                        几何生成辅助——§5.1/§5.2/§9.4.2；七轴模板仅登记
#                        不启用——P-03/O-27）
# T08 增列（tasks/foundation/WP-13-T08.json，§3.3 表 T08 行）：
#   src/Readiness.cpp        模型就绪校验器（IModelReadinessChecker——
#                        §8.2 分层检查表 L0→L11 编排＋短路优先级＋结果
#                        三组稳定排序；判定唯一委托 AssertionSuite 与
#                        既有不变量层——NFR-MNT-04 无重复判定）
#   src/CommandHandlers.cpp  建模命令处理器族（AssertionSuite 断言分域
#                        ＋命令载荷编解码＋prepare 公共管线＋五命令钩子
#                        ＋L5 装配注册——§9.3/§9.4.4/§9.4.8/§9.5 T08 行）
# T09 增列（tasks/foundation/WP-13-T09.json，§3.3 表 T09 行）：
#   src/DhConvert.cpp     DH↔显式转换器（IDhExplicitConverter——§7.4 无损
#                        展开＋§7.5 五状态确定性求解〔LM 过程常数与附录 D
#                        第 4/5 项判定阈值分属数值程序参数/工程阈值两类，
#                        见实现注〕＋§7.6 编译链 FK 对照等价验证＋权威切换
#                        域门 prepareAuthoritySwitch）
# T10 增列（tasks/foundation/WP-13-T10.json，§3.3 表 T10 行——Parts.hpp
#          T10 行"编辑流"）：
#   src/Parts.cpp（增量）  部件编辑流：工具 I-MDL-13（tcpList≥1——§4.4，
#                        经解码门校验链④自动强制）＋命名位姿合并流
#                        mergeNamedPoseEntries（保留键保留——V-27 建模侧；
#                        关节序一一对应——§4.6）
#   src/CommandHandlers.cpp（增量）  载荷 v2 removals 段＋AssertionSuite::
#                        assertDefaultTcp/assertReferenceProtection＋
#                        apply-tool-definition/apply-scene-objects 移除面
#                        ＋apply-named-poses 合并流（§4.8"删除＝引用移除"
#                        ＋V-04/V-05）
# T11 增列（tasks/foundation/WP-13-T11.json——Template.hpp T11 行"编辑流"）：
#   src/Template.cpp（增量）  基座安装姿态编辑流 applyBasePlacementEdit
#                        （§5.2 SetBasePlacement 域内核：三预设＋custom
#                        EAA（rad）＋basePosition（m）整体替换；I-MDL-7
#                        非法组合映射层拒绝——custom 缺 customEaa/preset≠
#                        ground 而 R=I（恒等判定 1×10⁻¹²）/正交性复用值
#                        模型单一判定；P-RT-4——modeling 只存参数不存矩阵，
#                        EAA→R 唯一经 runtime rotationFromCustomEaa 权威
#                        换算，MDL-22/M-11）
# T12 增列（tasks/foundation/WP-13-T12.json，§3.3 表 T12 行）：
#   src/CanonicalBridge.cpp  编译协作桥（§9.1 字段映射表全量唯一实现＋
#                        §9.2 reader 注入形态——RobotDesignReader/L5 装配
#                        注入 runtime S2；ICanonicalModelInputBuilder 纯
#                        函数内核＋ObjectClosureView 闭包域字节源契约；
#                        DH 权威经 §7.4 展开单一实现、零位折叠 T09 同规、
#                        O-36 复核范围裁决落地）
add_library(sdurws_ird_modeling STATIC
    src/ObjectTypes.cpp
    src/Errors.cpp
    src/DiagCodes.cpp
    src/RobotDesign.cpp
    src/Parts.cpp
    src/Codec.cpp
    src/PropertyEstimation.cpp
    src/Import.cpp
    src/XacroExpand.cpp
    src/Template.cpp
    src/Readiness.cpp
    src/CommandHandlers.cpp
    src/DhConvert.cpp
    src/CanonicalBridge.cpp)

# C++17（core.md 决策 D-01 同源）：逐目标显式设定，不设全局标准。
# PUBLIC 传播：modeling 公共头使用 std::string_view/inline constexpr 等
# C++17 设施，消费者编译 modeling 头也必须至少 C++17。
target_compile_features(sdurws_ird_modeling PUBLIC cxx_std_17)
set_target_properties(sdurws_ird_modeling PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)

# 源码编码：本单元源文件全部为 UTF-8（无 BOM）且含中文注释（AGENTS.md §2
# 注释规范）。MSVC 默认按系统代码页（GBK）解读无 BOM 源文件，会把 UTF-8
# 中文误读成乱码 token（core 落位时曾出现 C2001"常量中有换行符"），必须
# 显式 /utf-8。PUBLIC 传播：公共头同为 UTF-8，消费者编译含 modeling 头的
# 翻译单元也需要。
if(MSVC)
    target_compile_options(sdurws_ird_modeling PUBLIC /utf-8)
endif()

# 公共头根：仅 modeling/include（内部含 sdurws/ird/modeling/）。
# R-2 红线：私有实现头不得置于 include/——本目录是 modeling 唯一对外的
# 头暴露位，实现细节头（若有）只能放 src/ 并以相对路径被实现文件包含。
target_include_directories(sdurws_ird_modeling PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)

# 编译依赖：core＋diagnostics＋project＋runtime＋policy＋io（卡 §3.2 边表
# 六条接口依赖边——ARCH §3.5"各业务域单元→L2/L3 公共接口"许可方向的
# 实例化，ird_gates_whitelist.cmake 的 "modeling->…" 六行同批登记）。
# PUBLIC 形态：ObjectTypes.hpp 内嵌 runtime::kRobotDesignObjectType 常量、
# DiagCodes.hpp 内嵌 diagnostics::CodeDescriptor/IDiagnosticRegistry 契约
# 类型（其头又内嵌 core::DiagCode），消费方必须随链到达；project/policy/io
# 三边本任务"仅链接目标不消费上游公共头"（P-RPT-9 同款先例——边随落位
# 登记，消费随 T03+ 任务回填），六边统一 PUBLIC 保持形态一致。用统一真名
# sdurws_ird_<unit>：集成与独立冒烟两种模式下都可解析（别名 RWS::ird::*
# 仅集成模式存在——core/io 落位时同款取舍）。零 Qt（R-3/NFR-MNT-01——
# 插件目标随 T15 落位时方为 Qt 例外载体）、零其他业务域单元（R-1——
# modeling 不直链 requirements/kinematics/trajectory/dynamics/drivetrain/
# selection/optimization，跨单元协作走六类端口）、零 testkit（T-1——
# testkit 仅随测试目标消费）。
target_link_libraries(sdurws_ird_modeling PUBLIC
    sdurws_ird_core
    sdurws_ird_diagnostics
    sdurws_ird_project
    sdurws_ird_runtime
    sdurws_ird_policy
    sdurws_ird_io)

# pugixml：O-40 登记的 PRIVATE 第三方依赖（单独语句——不混入六条登记边的
# PUBLIC 语句，保持登记形态可扫描；PRIVATE＝不向消费者传播，公共头零
# pugixml 类型暴露）。本语句不在六边白名单守卫的 sdurws_ird_* 拦截范围
# （pugixml::pugixml 不匹配 ^sdurws_ird_/Qt/GTest 模式——配置期守卫不受影响）。
target_link_libraries(sdurws_ird_modeling PRIVATE pugixml::pugixml)

# 别名（集成模式）：供其余单元以 RWS::ird::modeling 消费；冒烟模式无别名
# （与骨架期行为一致，DTB §5.1）。
if(NOT CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    add_library(RWS::ird::modeling ALIAS sdurws_ird_modeling)
endif()

# ---- 测试目标 sdurws_ird_modeling_test（随 WP-13-T02 登记）----
# 用例面（任务契约 acceptance 逐条具名自证）：
#   test/ObjectTypesTest.cpp       五对象 token＋SceneObjectRole 词表
#                                  （acceptance 4——robot-design 实体同一
#                                  性/四 token 字面/角色五值与 policy 侧
#                                  token 一致性/往返）
#   test/ErrorsTest.cpp            ModelingErrorCode 全表 token＋
#                                  ModelingError 值语义＋映射数据
#                                  （acceptance 4——Errors.hpp 行）
#   test/DiagCodesTest.cpp         MDL-* 工厂登记值＋真实注册表行为
#                                  （acceptance 4——DiagCodes.hpp 行，
#                                  P-MDL-8 处置：以真实 StableCodeRegistry
#                                  验证全部通过）
#   test/BuildRedLineTest.cpp      产品面源码红线运行期扫描（零 Qt＋
#                                  include 面限于六边——acceptance 1/2，
#                                  与文件末尾配置期守卫互为两道防线）
# T03 增列（任务契约 acceptance 3/4/5 具名自证）：
#   test/RobotDesignTest.cpp       不变量 I-MDL-1～10 逐条注册（用例名绑定
#                                  编号）＋双权威守卫 C-1/C-2（V-14 场景）
#                                  ＋Axis 编辑权限随权威模式（MDL-09）
#   test/PartsTest.cpp             工具物性断言①②③（§4.4 同连杆）＋传动
#                                  I-MDL-11/12（R1 coupling 拒绝——§4.7）
#   test/CodecTest.cpp             canonical 确定性（NFR-COR-02 同对象同
#                                  字节）＋encode/decode 往返逐字段一致＋
#                                  D-MDL-5 派生字段不入编码身份＋未知主
#                                  版本 SchemaVersionUnsupported（NFR-
#                                  DEP-04）＋MalformedPayload 破损面
# T04 增列（任务契约 acceptance 1~5 具名自证）：
#   test/PropertyEstimationTest.cpp  三段元闭式公式手算对照＋多段平行轴
#                                  合成（M-2）＋输出自检反例拒收＋质心
#                                  确认流三分支（V-17）＋密度默认表/
#                                  NotProvided 不断言（DYN-06/M-8）＋
#                                  传动 schema 字段约束与来源标记（MDL-16）
# T05 增列（任务契约 acceptance 1~5 具名自证）：
#   test/ImportTest.cpp    接口落位/R1 边界（ACC1）＋§6.3 字段映射表逐行
#                          四清单/包 URI/缺失资源（ACC2，AT-15）＋MDL-11
#                          轴语义三分支（ACC3）＋链型判定两维度（ACC4，
#                          AT-17，随链型判定提交）＋确定性/诊断行序稳定
#                          排序（ACC5，NFR-COR-02）——inline fixture
#                          （V-06 全链黄金数据集随 WP-13-T16 收口）
# T06 增列（任务契约 acceptance 1~5 具名自证）：
#   test/XacroExpandTest.cpp  接口落位/受控子集与绝不执行（ACC1）＋未定义
#                          宏/参数行列定位＋护栏码透传（循环 V-09/依赖缺失
#                          /深度护栏）（ACC2，AT-31）＋展开产物经
#                          mapXacroExpanded 走 mapUrdf 同一映射与 external-
#                          Refs 来源留痕＋失败无草稿（ACC3）＋确定性/可
#                          重入（ACC5，NFR-COR-02）——inline fixture
#                          （Xacro 黄金样例随 WP-13-T16 收口）
# T07 增列（任务契约 acceptance 1~5 具名自证）：
#   test/TemplateTest.cpp  接口落位/三模板清单/createDraft 满足 I-MDL-1~
#                          12（ACC1）＋七轴登记不启用 TemplateDisabled＋
#                          MDL-TEMPLATE-DISABLED（ACC2，V-02/O-27/P-03）＋
#                          T-MDL-1 表值逐格/J6 行程 4π 边界（ACC3，D-MDL-7）
#                          ＋创建→编辑链路（字段级/批量单条摘要/
#                          buildChangeSummary/4·5 轴与 prismatic 创建入口
#                          阻止）（ACC4，MDL-01/AT-20）＋几何辅助两条
#                          确定性/来源标记（ACC5，§5.2 v0.2）
# T08 增列（tasks/foundation/WP-13-T08.json，acceptance 1~5 具名自证）：
#   test/CommandFixtures.hpp        测试域共享夹具（值模型构造＋policy/
#                          project 替身——project/test/StubHandlers.hpp
#                          先例：测试域替身不入公共面）
#   test/ReadinessTest.cpp         就绪校验分层（ACC1：check 纯函数/短路
#                          优先级/L11 策略不可解析/稳定排序）＋断言分域
#                          反例 V-15（ACC2，MDL-06/AT-01）＋行程确认
#                          V-16（ACC3，SA-15）
#   test/CommandHandlersTest.cpp   五命令 token 表/载荷编解码/注册表
#                          （ACC4，§9.3/O-35）＋prepare 管线（decode 失败/
#                          防御性复核/取号回填/inverse 快照/中文摘要/
#                          RejectedHardAssert/Confirmable——ACC1/3/4）
# T09 增列（tasks/foundation/WP-13-T09.json，acceptance 1~3 具名自证）：
#   test/DhConvertTest.cpp         DH↔显式转换（ACC1 无损展开/零位对齐/
#                          基座混入拒绝＋ACC2 五状态各含样例/结构检查先行
#                          ＋ACC3 退化族字典序定值/确定性——NFR-COR-02；
#                          两模式均编译——被测面零框架符号）
# T11 增列（tasks/foundation/WP-13-T11.json，acceptance 1~4 具名自证；
#          两模式均编译——命令路径用例选移动关节设计不消费④端口）：
#   test/BasePlacementEditTest.cpp  基座安装姿态编辑（ACC1 V-13 三预设＋
#                          custom＋位置＋词表一致性＋apply-robot-design
#                          原子持久化单命令单写入；ACC2 V15-04 模板/导入
#                          默认地面＋I-MDL-7 非法组合映射层拒绝；ACC3
#                          P-RT-4 权威产出点交叉核对（modeling 只存参数
#                          不存矩阵——V-12 输入面）；ACC4 编译单一字段
#                          R_world_base 手算对照＋字节面单字段证据）
# T12 增列（tasks/foundation/WP-13-T12.json，acceptance 1/2/3/4/5 具名
#          自证；两模式均编译——被测面为纯函数服务，自持夹具零命令/策略
#          依赖；共享夹具 test/BridgeFixtures.hpp 同批登记）：
#   test/CanonicalBridgeTest.cpp  编译协作桥（ACC1 §9.1 字段映射表逐行——
#                          契约版本同源/robotLocalName/joints Explicit＋
#                          DH 展开＋零位折叠/links 物性几何/tools 默认
#                          TCP 居首与 tcpOffset 合成/scene 世界系固连不
#                          预乘安装旋转/base 编辑表示/drivetrain＋friction/
#                          resourceRefs 两态＋Fixed 中性轴；ACC2 确定性＋
#                          无内容身份重算；ACC3 单根字节输入/闭包域解析/
#                          O-36 复核范围；ACC4 四族错误定位到对象/字段＋
#                          三层防线边界；ACC5 R-4 名称透传不消歧＋V-26/
#                          AT-18 输入质量面）
# 测试 main 自持＝testkit 报告设施（TestMainReport.cpp 安装 TestRecord
# Listener——ird-test-report.json 用例级明细与 gtest XML 并存；IO-T07/
# PRJ-T15 同款先例；IRD_TEST_INFO 携带需求/AT 追溯字段）。
enable_testing()

add_executable(sdurws_ird_modeling_test
    test/TestMainReport.cpp
    test/ObjectTypesTest.cpp
    test/ErrorsTest.cpp
    test/DiagCodesTest.cpp
    test/BuildRedLineTest.cpp
    test/RobotDesignTest.cpp
    test/PartsTest.cpp
    test/CodecTest.cpp
    test/PropertyEstimationTest.cpp
    test/ImportTest.cpp
    test/XacroExpandTest.cpp
    test/TemplateTest.cpp
    test/BasePlacementEditTest.cpp
    test/DhConvertTest.cpp
    test/CanonicalBridgeTest.cpp)

# T08 测试 TU（集成模式专属 gating——POL-T06/T07/T08 同款：ReadinessTest/
# CommandHandlersTest 消费 policy 行程评估器〔makeJointLimitEvaluator〕，
# 该符号的 TU（policy/JointLimits.cpp）仅在集成模式编译——TARGET
# sdurw_kinematics 判定同源；冒烟模式两 TU 不编入，"目标注册＋include
# 路径"冒烟口径不受影响）。
if(TARGET sdurw_kinematics)
    target_sources(sdurws_ird_modeling_test PRIVATE
        test/ReadinessTest.cpp)            # T08 ACC1~3：就绪分层/断言分域
                                           # 反例 V-15/行程确认 V-16
    target_sources(sdurws_ird_modeling_test PRIVATE
        test/CommandHandlersTest.cpp)      # T08 ACC4＋1/3：五命令 token 表/
                                           # 载荷编解码/prepare 管线/inverse
                                           # 快照/中文摘要
    target_sources(sdurws_ird_modeling_test PRIVATE
        test/DhConvertEquivalenceTest.cpp) # T09 ACC4/5：编译链 FK 对照等价
                                           # 验证（真实 runtime 编译器探针）
                                           # ＋权威切换命令 prepare 协作面
                                           # （消费 runtime 产品编译器与
                                           # policy 评估器——冒烟不编入）
    target_sources(sdurws_ird_modeling_test PRIVATE
        test/PartObjectCommandTest.cpp)    # T10 ACC1~5：工具/场景/命名位姿
                                           # 对象命令（引用保护 V-04/V-05、
                                           # defaultTcp 校验、worldPose 固
                                           # 连、保留键合并流、载荷 v2——
                                           # 消费 policy 评估器与 Command-
                                           # Fixtures 夹具——冒烟不编入）
endif()

target_compile_features(sdurws_ird_modeling_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_modeling_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（/utf-8 说明见产品目标处；gtest 头为
# ASCII 不受影响）。显式再设一次以自洽——测试目标应独立可读，不依赖
# 被测目标的编译选项细节。
if(MSVC)
    target_compile_options(sdurws_ird_modeling_test PRIVATE /utf-8)
endif()

# 测试链接产品库 sdurws_ird_modeling 同时验证"公共头可被正常 include＋库
# 可链接"（六边 PUBLIC 传染链随目标自动到达）。sdurws_ird_testkit＝报告/
# 追溯设施消费面（RecordListener/IRD_TEST_INFO——T-1 允许形态：仅测试
# 目标链 testkit，产品目标不链）。单行链接语句＝契约测试 T-1 扫描的
# 登记形态约束（testkit 与测试目标名同行可见——reporting 同款）。
target_link_libraries(sdurws_ird_modeling_test PRIVATE sdurws_ird_modeling sdurws_ird_testkit GTest::gtest)

# 源码树位置注入：红线扫描用例在运行期扫描 modeling 源码核对红线（零
# Qt/零表外 include——§3.2/§14.5）。经编译定义传 industrialrobot 根
# （io 同款——值内以单元名作子目录前缀，扫描面随单元自包含）。
target_compile_definitions(sdurws_ird_modeling_test PRIVATE
    IRD_MODELING_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# vcpkg 的 gtest:x64-windows 是动态库（gtest.dll/gtest_main.dll）：把测试
# 可执行文件的运行期 DLL 拷到其输出目录，保证不经 PATH 配置即可直接运行
# （留痕与验收都要直接执行该可执行文件）。TARGET_RUNTIME_DLLS 需 CMake
# ≥3.21，本仓库工具链为 VS2022 自带的更新版本，满足。
add_custom_command(TARGET sdurws_ird_modeling_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_modeling_test>
            $<TARGET_FILE_DIR:sdurws_ird_modeling_test>
    COMMAND_EXPAND_LISTS)

# ---- ird_add_gtest：add_test＋XML 报告目标（DTB §5.5，宏各单元自持）----
# 框架原宏见 RobWork/RobWork/gtest/CMakeLists.txt：add_test ＋ <target>_
# report（运行测试并落 gtest XML）。本宏按 DTB §5.5 在本单元 CMake 内
# 定义（定义先于全部调用——CMake 顺序解析；core/io 同款形态，不做跨
# 单元共享——modeling 是业务域单元，不提供构建工具设施，NFR-MNT-04）。
# 报告输出于构建树本目录 gtest_reports/<target>.xml；任务留痕时拷贝入
# doc/industrial-robot-design/traceability/（AGENTS.md §4.2 验证留痕）。
# 报告目标不进默认构建（EXCLUDE_FROM_ALL）：跑测试是显式动作，与框架
# "不随 ordinary build 执行测试"的约定一致。
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

ird_add_gtest(sdurws_ird_modeling_test)

# ---- 契约测试目标 sdurws_ird_modeling_contract_test（随 WP-13-T02 登记）----
# 落位期契约面＝构建图边界契约（reporting LinkageContractTest 同款扫描
# 形态——六条登记边的存在性/封闭性/无业务域互链/testkit 仅测试目标）；
# WP-13-T08 增列 CommandPipelineContractTest.cpp（acceptance 5：V-19 未
# 就绪不编译＋V-20 双编译失败面〔modeling 侧可执行范围〕＋V-22 stale
# 防御面＋§9.5 T08 行九码注册不预建——AT-01）。
# WP-13-T12 增列 DescriptionBridgeContractTest.cpp（acceptance 3：Description
# 契约面字段集与 runtime.md §4.2 对齐＋schemaVersion↔
# descriptionContractVersion 同源〔初值 1〕＋reader 失败轨值面契约——
# 两模式均编译，被测面零框架符号）。自有 main 同 _test 目标（报告设施
# 接入）。
add_executable(sdurws_ird_modeling_contract_test
    contract_test/ContractTestMain.cpp
    contract_test/BuildGraphContractTest.cpp
    contract_test/DescriptionBridgeContractTest.cpp)

# T08/T12 契约测试 TU（集成模式专属 gating——同上：T08 消费 policy 行程
# 评估器与 project 命令契约替身；T12 CompileBridgeContractTest 消费 runtime
# 公共 CanonicalModelBuilder 装配 S5（T09 RuntimeCompileProbe 同款装配纪律
# ——产品编译器私有头 R-2 不跨单元 include），冒烟模式均不编入）。
if(TARGET sdurw_kinematics)
    target_sources(sdurws_ird_modeling_contract_test PRIVATE
        contract_test/CommandPipelineContractTest.cpp)  # ACC5：V-19/V-20/V-22
                                                        # ＋T08 行九码注册
    target_sources(sdurws_ird_modeling_contract_test PRIVATE
        contract_test/CompileBridgeContractTest.cpp)    # ACC3：reader→S5
                                                        # 端到端对齐＋失败面
endif()

target_compile_features(sdurws_ird_modeling_contract_test PRIVATE cxx_std_17)
set_target_properties(sdurws_ird_modeling_contract_test PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF)
# 测试源文件同为 UTF-8＋中文注释（同 _test 目标口径）。
if(MSVC)
    target_compile_options(sdurws_ird_modeling_contract_test PRIVATE /utf-8)
endif()

# 链接面与 _test 同款（testkit＝报告设施——T-1 允许形态；单行链接语句
# ＝契约测试 T-1 扫描的登记形态约束，同 _test 目标口径）。
target_link_libraries(sdurws_ird_modeling_contract_test PRIVATE sdurws_ird_modeling sdurws_ird_testkit GTest::gtest)

# 源码树位置注入（红线/构建图扫描用例同 _test 口径）。
target_compile_definitions(sdurws_ird_modeling_contract_test PRIVATE
    IRD_MODELING_UNIT_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")

# 运行期 DLL 拷贝（vcpkg gtest 动态库——同 _test 目标的 POST_BUILD 口径）。
add_custom_command(TARGET sdurws_ird_modeling_contract_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_RUNTIME_DLLS:sdurws_ird_modeling_contract_test>
            $<TARGET_FILE_DIR:sdurws_ird_modeling_contract_test>
    COMMAND_EXPAND_LISTS)

ird_add_gtest(sdurws_ird_modeling_contract_test)

# ---- ctest LABELS "ird"（ui 落位同款 set_tests_properties）----
# 验收过滤口径：ctest -L "^ird$"（任务契约 verify 第 4 条）——两测试
# （模型层、进程内、确定性）同标签；_gui_test 形态本单元无（GUI 呈现随
# T15 插件目标另登记）。
set_tests_properties(sdurws_ird_modeling_test sdurws_ird_modeling_contract_test
    PROPERTIES LABELS "ird")

# ---- 依赖红线守卫（配置期断言——卡 §3.2，core/io foreach 同款）----
# modeling 的 industrialrobot 单元依赖仅限六条登记边：core＋diagnostics＋
# project＋runtime＋policy＋io（卡 §3.2 边表——ARCH §3.5 业务域→L2/L3
# 公共接口许可方向的实例化，白名单 "modeling->…" 六行同源）；链接到任何
# 其他 sdurws_ird_* 目标（业务域单元＝R-1 命中面；表外平台单元＝SUB
# 命中面）即配置失败。零 Qt（R-3/NFR-MNT-01）。产品库本体零 gtest/testkit
# （D-06/T-1——断言宏只在消费方测试目标展开；本守卫遍历的是产品目标
# sdurws_ird_modeling 的链接面，测试目标的 gtest/testkit 边不在其内）。
# 两面读取（LINK_LIBRARIES 含 PRIVATE 边＋INTERFACE_LINK_LIBRARIES 含
# PUBLIC 边）防漏。属性未设置时 get_target_property 返回 "<var>-NOTFOUND"
# 字面串，不匹配任何模式，不会误报。testkit 不在第三条模式中逐字列出
# （reporting 同款先例）：sdurws_ird_testkit 落在第一条通配 "^sdurws_ird_"
# 的拦截范围（≠六条登记边即 FATAL），避免守卫正则字样干扰契约测试的
# T-1 链接语句扫描（BuildGraphContractTest 的 NoTestkitEdge 用例以"逐行
# 只认测试目标链接语句"钉住产品目标零 testkit）。正式全仓门禁（依赖图
# 白名单）归 ird_gates（WP-01-T01），本守卫不替代它。
get_target_property(_ird_modeling_link_libs sdurws_ird_modeling LINK_LIBRARIES)
get_target_property(_ird_modeling_iface_libs sdurws_ird_modeling INTERFACE_LINK_LIBRARIES)
foreach(_lib IN LISTS _ird_modeling_link_libs _ird_modeling_iface_libs)
    if(_lib MATCHES "^sdurws_ird_"
       AND NOT _lib STREQUAL "sdurws_ird_core"
       AND NOT _lib STREQUAL "sdurws_ird_diagnostics"
       AND NOT _lib STREQUAL "sdurws_ird_project"
       AND NOT _lib STREQUAL "sdurws_ird_runtime"
       AND NOT _lib STREQUAL "sdurws_ird_policy"
       AND NOT _lib STREQUAL "sdurws_ird_io")
        message(FATAL_ERROR
            "modeling 依赖红线（R-1/卡 §3.2）：sdurws_ird_modeling 仅可链接"
            "六条登记边 core/diagnostics/project/runtime/policy/io"
            "（units/modeling.md §3.2、ARCH §3.5；含 testkit——T-1 产品目标"
            "禁链），发现 ${_lib}")
    endif()
    if(_lib MATCHES "^Qt[0-9]?::|^Qt::|Qt5::|Qt6::")
        message(FATAL_ERROR
            "modeling 依赖红线（R-3/NFR-MNT-01，计算库零 Qt——卡 §3.1）："
            "发现 ${_lib}")
    endif()
    if(_lib MATCHES "^GTest::|^gtest$")
        message(FATAL_ERROR
            "modeling 依赖红线（D-06）：产品库本体不链接 gtest，发现 ${_lib}")
    endif()
endforeach()
