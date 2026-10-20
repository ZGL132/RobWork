# 建模审计并行修复执行计划（WP-13-T25）

编制日期：2026-10-20。执行会话：ZCode 旁支会话（与主任务 WP-13-T24 并行）。
基线：`f873984b`（= 601ffa0e 审计基线 + 9 提交：WP-14-T12 策略编辑 A-23 ×4、WP-13-T24 B1/B2/B3a/B3b ×5）。
Worktree：`C:/zcode-wt/wp13-t25`（独立 Git 索引）；构建树：`C:/zcode-wt/build-wp13-t25`（独立，vcpkg 工具链＋Qt 前缀，`RWS_BUILD_INDUSTRIALROBOT=ON` 已确认）。
交接文档登记：`modeling-parallel-handoff-20261020.md` 未以文件形态存在于仓库/主 worktree（已检索），内容以会话提供的计划文本为准。

## G0 环境核对结论（2026-10-20）

- 主任务 worktree `C:/Users/zgl18/.codex/worktrees/486b/RobWork`（wp13-t24@f873984b＋未提交 F16 改动）——只读观察确认，未做任何修改/暂存/构建/清理。
- 本 worktree 绝对路径、Git 索引、构建树均独立；分支 `wp13-t25` 自 `f873984b` 新建（分支号/任务号查重：WP-13-T25 未占用）。
- 审计证据只读：`traceability/builds/modeling-audit-20261019-601ffa0e/{REPORT,MATRIX,MATRIX-RESULTS}.md` 中本 4 项（F06=M14、F01=M05、F07=M17、F14=M31）。
- 基线状态（f873984b 相对审计基线的增量已核对）：F02/F03/F05/F04/F15/F13/F17 已由主任务 B1/B2/B3 修复；F11 已由 WP-14-T12（A-23）实现策略编辑宿主。**本批 4 项剩余缺口见下**。

## 各项现状与剩余缺口（代码核对结论）

### F06 外部 WorkCell 导入（MDL-18/AT-33）

- 已有：`WorkCellReverseImport.hpp/.cpp` 提取引擎（`extractWorkCellDevice`，mapWorkCellXml 委托）、`Import.hpp` 通道语义、io `openGenericXml`。
- 缺口：`PanelCommandCatalog.cpp` 11 条命令无 import-workcell；`ModelingCommandFlows.cpp` 无该流程；`isAssembledModelingCommand` 词表无该命令；单元卡 §9.7.3 表无行。
- 复用面：URDF 导入流同型（readFileBytes→ValidatedSource〔bytes+entrySnapshot+依赖树〕→mapWorkCellXml→分支选链→confirmImport→草稿替换保根）。

### F01 逐关节速度/加速度（MDL-01）

- 已有：runtime `JointDescription.maxVelocity/maxAcceleration`（Description.hpp:270）、`CanonicalModel` 同名字段＋`hasJointVelocityLimits`；模板 `SixAxisJointSpec.maxVelocity/maxAcceleration`（值存在但不入模型）。
- 缺口：modeling `JointEntry`（RobotDesign.hpp:421）无字段；`Codec` 无编解码；`CanonicalBridge.cpp:543` 硬写 NotProvided；URDF `<limit velocity>` 报"joint-velocity-limit 无落点"（Import.cpp:1919）；WorkCell `<VelLimit>/<AccLimit>` 报未保留；面板关节编辑无行；模板重种子 summary 明示"不进入建模计算内核"。
- 注意：`§8.2` v0.53 就绪注记（L9 速度/加速度 schema 边界 NotProvided 预告）随本项演进。

### F07 自碰撞配置（MDL-04/ARC-05/AT-19）

- 已有（A-23/WP-14-T12，在我基线内）：`HostEngineeringPolicy`——模态全字段编辑（启用碰撞/四域/安全间距/相邻过滤/mandatory+excluded PairEditor）、project 提交（submit(store,base,RawPolicyInput,allocateNew)）、按修订解析、命令处理器注册。
- 待核对缺口（G1 复现确认）：①正式窗口策略编辑入口可达性；②导入侧 selfCollisionHints/selfCollisionCandidates → RawPolicyInput 策略草稿移交（P-MDL-3：待 policy 冻结后交接——policy 已有 RawPolicyInput，移交是否实现）；③真实评估器（运动学/轨迹/优化入口）按修订消费已编辑策略；④Manual/Auto/Imported 来源呈现；⑤三维覆盖层与计算启用状态正交。
- 红线：selfCollisionHints 不入根对象权威编码；policy 权威不重做。

### F14 耦合矩阵编辑与正式宿主（MDL-21/AT-38）

- 已有：`Parts.hpp` CouplingDesign/CouplingStage/checkCouplingMatrix（I-MDL-11 单一判定）；`ApplyDrivetrainDesignHandler`；MDL-21 R2 解冻登记（2026-10-10 GOV-UNFROZEN，REQUIREMENTS v1.17）。
- 缺口：①modeling/plugin 全域无 coupling 编辑入口（grep 零命中）；②`UiPlugin.cpp:5305` 宿主装配传 `R1Locked`——正式宿主有效矩阵被解码门拒为 MDL-21-COUPLING-STAGE-LOCKED。
- 约束：公共默认 R1Locked 保留；mimic/planar/floating/闭环不放开；矩阵校验复用 runtime/drivetrain 唯一实现不复制 SVD。

## 执行顺序与提交计划

固定顺序 F06→F01→F07→F14（F01 后复跑 F06 映射/报告）。每项一个完整提交（代码+测试+文档+证据），提交前双模式构建＋受影响测试＋ird_gates＋BuildRedLine＋validate-docs/task＋verify-task＋范围检查。证据目录：`traceability/builds/wp13-t25-f06/`、`-f01/`、`-f07/`、`-f14/`（只增不改）。

### F07 状态：已实现验证（2026-10-20）

- 链路核实：审计 M17 主体缺口已由 WP-14-T12（A-23，基线内）闭合——入口/编辑/提交/按修订解析/评估器接线全链在案（A-23 套件复跑 6/6）。
- 本批实现：P-MDL-3 移交半区（policy 已冻结）——导入候选→会话承载→门面取值器→策略编辑预填（名→ObjectId 按修订 robot-design 解码）＋未解析警示；红证 1＋流/GUI 绿证；ird_gates/validate-* PASS。
- 遗留：per-pair 来源标记与基座首段/静态开关＝policy 卡演化面待所有者；三入口数值对照与覆盖层＝B5 统一验收。

### F01 状态：已实现验证（G1～G4 闭环，2026-10-20）

- G1 红证：4 例红（桥 NotProvided／URDF 谎报无落点／面板行缺失／就绪无条件注记）——red-*.{xml,log}。
- G2 实现：JointEntry 表尾两字段（MDL-01 关节一等权威；转动 rad/s、rad/s²｜移动 m/s、m/s²；≥0；Fixed NotApplicable）＋Codec {1,1}（旧 {1,0} 兼容读）＋Bridge 透传＋模板/URDF/WorkCell 三入口映射＋面板两行＋编辑页 14 行＋域规则＋就绪条件化＋黄金期望演化（非掩盖）。
- G3 验证：独立 343/8/48、集成 420/17/62/326 全绿；ird_gates 零命中；validate-docs/task PASS；F06 复跑完成（VelLimit 报告转映射行——流＋GUI 双面）。
- 消费者边界如实登记：描述层 Provided 为本批闭合点；轨迹/动力学端到端数值消费归跨域链复验（统一验收批）。

### F06 状态：已实现验证（G1～G4 闭环，2026-10-20）

- G1 红证：5 流＋1 GUI 全红（red-standalone-modeling.* / red-integration-gui.*）。
- G2 实现：目录第十二号＋命令流（openGenericXml 良构＋摘要对账＋Polytope 缺失容忍树＋显式选链＋报告确认＋恰一根替换）＋UiText 两键＋单元卡 v0.55＋DTB v0.91。
- G3 验证：独立 337/8、集成 413/17/61（GUI）、ui 326 全绿；ird_gates 零命中；validate-docs/validate-task PASS；gate-all 91/99（4 失败逐项基线归因——2 例基线固有〔optimization 契约 OPT-VER-145 断言、project_perf HotStartP95≈3.7s>3s，stash 态复现〕、1 例本批 GUI 测试 smoke 环境自备已修、1 例 CrashCapture gate 时段瞬态〔事后 ctest/直跑双绿〕）。详见 wp13-t25-f06/README.md。
- 遗留：F01 落地后复跑本项（VelLimit/AccLimit 映射随 schema 落点更新——当前报告"未保留"为诚实边界）。

## 逐项八节设计

（每项实施前在该节落定；此处为登记位）

### F06 八节设计

1. 行为：MDL-18/AT-33。正式命令 `modeling.import-workcell`（菜单"建模/导入"）：选文件（*.wc.xml）→io 预算内读真实字节→ValidatedSource 装配（含 mesh 依赖树——复用 assembleImportDependencyTree）→mapWorkCellXml→多设备分支显式选择→逐项提取报告（confirmImport）→确认才替换草稿（保根不变量）；取消/失败/不可表达零修改零修订。
2. 接口：命令目录加行（§9.7.3 表同步）；flow 加路由；isAssembledModelingCommand 词表加词；公共接口零新增（复用 mapWorkCellXml/ValidatedSource/ImportReport）；UiText 键按 §3.5 约定派生（值面确认后登记）。
3. 失败：io 读取/预算失败如实转述；非 WorkCell 根/无设备/多设备未选链/不可表达关节→引擎值面错误＋完整报告（无条件产出）；选链取消零写入。
4. 生命周期：UI 线程；io 句柄 RAII；草稿替换仅在确认后（同 URDF 流 R1 保根）。
5. 线程：全 UI 线程（同既有导入流）。
6. 测试：真实临时 .wc.xml（单设备/两设备/缺资源/非 WC 根/从动关节/多自由度/DH 链/外部碰撞设置/多 Q）；数值用例 90° 姿态＋非零 Q 独立核对 deg→rad；取消×3/只读；GUI 真实文件选择路径。
7. 文档：units/modeling.md §9.7.3 行＋§6.6 通道行注；DTB §2.14 行；契约 acceptance 已列。
8. 决策：无新架构决策；不新增"从 WorkCell 新建项目"来源（PM 明确不做）。

### F01 八节设计

（实施前落定——权威归属/四态/类型语义/旧编码兼容/编码版本与身份/导入映射/模板来源/差异与编辑历史）

### F07 八节设计

（实施前落定——链路图/已有接口与缺口清单/入口与消费复现/来源呈现/评估一致性）

### F14 八节设计

（实施前落定——编辑入口面/矩阵约定/域验证复用/宿主装配/负例保留/消费链）

## G0 基线构建/测试留痕

- 集成配置：`cmake -S C:/zcode-wt/wp13-t25/RobWork -B C:/zcode-wt/build-wp13-t25 -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON -DCMAKE_TOOLCHAIN_FILE=D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64`——完成，缓存 `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON` 已 grep 确认。
- 全量 Release 构建进行中（留痕：`traceability/builds/wp13-t25-g0/`）。
- 基线测试：构建完成后跑 modeling 全套（test/contract_test/gui_test）记录基线通过数（既有失败按用例定位登记）。
