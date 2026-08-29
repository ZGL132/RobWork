# 需求定义模块详细方案（requirements-definition）

- 方案版本：v0.3；需求基线：v0.7；架构检查点：`IRD-D2-20260829`；治理状态：Proposed（D5 重写，待消费者评审）
- 负责 WP：WP-14；阶段/发布：阶段 B / R1；任务卡：`agent-tasks/WP-14-T01～T07`
- 架构契约：`architecture/domain-model.md`、`architecture/persistence-schema.md`、`architecture/public-interfaces.md`、`architecture/symbol-registry.md`
- 代码前置：WP-03、04、09、11（WP-10 为 GUI 层前置）；WP-05、WP-13 为交付/契约前置（总纲 §5.3：快照与模型作用域经修订查询获得，无业务插件代码依赖）；构建/门禁入口 WP-01
- 需求锚点：§7.1～7.2、§8.2（REQ-01～08）、§15.3；场景 AT-02、AT-03、AT-18

## 1. 模块职责

`EngineeringRequirements`（SYM-DOM-006）与 `LoadCase`（SYM-DOM-007）的唯一编辑所有者：任务点（参考坐标系、TCP 引用、位姿与受约束分量、容差、Must/Should）、区域（Box、采样密度、最低覆盖率）、工艺段与负载事件建模，CSV 安全导入导出与就绪校验状态机；输出经 WP-04 领域命令产生修订，供 WP-15/16/17/20 经输入切片消费。持久化字段以 `schemas/engineering-requirements.schema.json` 与 `schemas/examples/engineering-requirements.example.json` 为准（引用，不复述）。非目标：运动学/轨迹/动力学求值、碰撞判定、执行调度、快照构建、GUI 会话状态权威；轨迹/动力字段只保存并校验，不在本 WP 执行完整动态求值。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/requirements/
  include/sdurws/ird/requirements/
    RequirementsModel.hpp   TaskPoint.hpp   PoseRegion.hpp   LoadEvent.hpp
    RequirementsCsvAdapter.hpp   ReadinessChecker.hpp
    RequirementsCommands.hpp   RequirementsDiagnostics.hpp
  src/RequirementsModel.cpp   TaskPoint.cpp   PoseRegion.cpp   LoadEvent.cpp
      RequirementsCsvAdapter.cpp   ReadinessChecker.cpp   RequirementsCommands.cpp
  resources/requirements/{task-points.column-dictionary.json,regions.column-dictionary.json,
      load-cases.column-dictionary.json,templates/}
  gui/RequirementsPlugin.hpp   gui/RequirementsPlugin.cpp   gui/panels/
  test/RequirementsModelTest.cpp   CsvIoTest.cpp   PoseRegionTest.cpp
      LoadEventTest.cpp   ReadinessTest.cpp   CommandIntegrationTest.cpp   RequirementsGuiTest.cpp
  testdata/requirements/{csv-valid,csv-malicious,regions,loads,ready-matrix}/
  evidence/WP-14/
```

CMake target：`sdurws_ird_requirements`（计算核心，无 Qt Widgets）、`sdurws_ird_requirements_plugin`（薄插件）、`sdurws_ird_requirements_test`、`sdurws_ird_requirements_contract_test`、`sdurws_ird_requirements_gui_test`。允许依赖：WP-03 core、WP-04 命令/查询公共头、WP-09 diagnostics、WP-11 io（CSV 安全读写）、Qt Core；GUI 层另加 Qt Widgets 与 WP-10 ui。禁止：其他业务插件私有头、插件内第二套 CSV/路径解析、直接执行公式、直写项目目录、修改领域公共枚举。

## 3. 数据与接口

冻结字段（C++ 模型；JSON 形态以 schema 为准，`tcpRef` 与夹取/释放事件的 Schema 落位为 D5 提名，见报告）：

| 对象（模块冻结） | 字段 | 规则 |
| --- | --- | --- |
| 任务点 TaskPoint | taskId/localName、priority(Must/Should)、frameId（参考坐标系，解析为 objectId）、tcpRef（`ToolDefinition` objectId 或 `RobotDesign.defaultTcp`）、targetPose（四元数＋位置）、constrainedComponents ⊆ {X,Y,Z,Roll,Pitch,Yaw}、positionTolerance(>0,m)/orientationTolerance(rad,测地角)、approach/retract | 不得依赖"当前选中坐标系"；受约束分量未列出即自由（4/5 轴任务子空间来源，REQ-01）；tcpRef 必须可解析 |
| 区域 PoseRegion | regionId、priority、frameId、Box min/max、positionSampleCount/orientationSampleCount、minCoverageRatio | 网格默认含边界；分母＝计划的位置—姿态组合＝positionSampleCount×orientationSampleCount（供 WP-15 消费的冻结定义）；单区域计划组合上限 1,000,000（模块冻结，超出拒绝） |
| 负载事件 LoadCase | loadCaseId、payloadMass、payloadCenterOfMass（显式 frameId）、externalWrench（force N/torque N·m，显式 frameId）、dwellTime、events[]（grip/release，挂接 taskId 与序列） | 完整任务循环含驻留与工况切换（§7.2）；缺关键数据 → 就绪判定报 DataInsufficient 语义 |
| 就绪状态机 | 条目级 {Valid, Invalid(行/列定位)}；聚合 {输入未完成, 可计算} | 任一启用 Must 非法 → 输入未完成，阻止正式运行；仅 Should 非法 → 可计算＋警告可见（REQ-06）；聚合状态映射 WP-10 `StageStatusModel` 八值 |

CSV 列字典（模块冻结，风格对齐 `schemas/catalog/column-dictionary.schema.json`）：任务点表列 `task_id(string,必填)`、`task_name(string,必填)`、`priority(Must|Should,必填)`、`frame_id(string,必填)`、`tcp_ref(string,可选→defaultTcp)`、`pos_x/pos_y/pos_z(decimal,m,必填)`、`quat_x/quat_y/quat_z/quat_w(decimal,无量纲,必填)`、`position_tolerance(decimal,m,必填,>0)`、`orientation_tolerance(decimal,rad,必填,>0)`、`constrained_components(string,逗号分隔,可选→全集)`、`approach_* / retract_*`（可选列组）；区域表与负载表各持独立列字典（`resources/requirements/*.column-dictionary.json`）。

## 4. 调用与状态

时序（固定）：CSV 导入/手工编辑（`EditDraft`）→ 校验（有限性→单位→容差>0→引用存在→采样预算）→ 条目级与聚合就绪判定 → 用户应用 → `IProjectCommandService.apply`（恰好一个新修订；未应用草稿不失效下游，CON-05）。预览（部分预览）只取 Valid 条目、定位全部错误行、不产生正式 Pass/Verified/报告证据（REQ-06/AT-02）；正式运行前聚合就绪必须为"可计算"。错误矩阵（新码待 diagnostics.md 登记）：

| 错误码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| `IRD-REQ-ROW-INVALID` | CSV 行/字段非法（单位不明、非有限、枚举外、容差≤0） | Input | Error | 按源行号/字段定位；正确行不受影响（AT-02） |
| `IRD-REQ-REFERENCE-UNRESOLVED` | frameId/TCP/环境/工具引用不存在 | Input | Error | 补齐被引对象或解除引用后重新应用 |
| `IRD-REQ-NOT-READY` | 存在非法 Must 条目时请求正式运行 | Input | Error | 阻止调度并返回逐项诊断；先修正或停用条目 |
| `IRD-REQ-SAMPLING-BUDGET` | 区域计划组合数超上限 | Engineering | Error | 调整采样密度后重新应用 |

## 5. 关键实现约定

- 单位 SI；姿态一律单位四元数＋显式参考坐标系；RPY 仅界面输入/显示的换算 helper，不进持久化结构；容差必须有限且 >0。
- 4/5 轴任务语义：受约束分量集合构成任务子空间（REQ-01），本模块只声明分量集合，不默认要求完整六维位姿；子空间 Jacobian 的构造归 WP-15（§15.3）。
- 覆盖口径冻结：分母＝计划的位置—姿态组合（含边界样本）；位置覆盖率与姿态覆盖率分别计算的定义权在 WP-15，本模块只持久化采样密度与最低覆盖率。
- 负载与工艺事件：接近/撤离沿工具轴或参考轴表达方向与距离（REQ-02）；夹取/释放/驻留构成完整任务循环（REQ-04）；就绪校验发现缺关键负载数据时按 REQ-06 语义阻止正式运行（DataInsufficient 通道），不静默默认。
- CSV 安全：全部经 WP-11 reader/writer（`sourceLine/fieldName/rawText/normalizedValue` 逐行诊断；公式样式文本不执行、导出统一转义，NFR-SEC-03）；往返稳定（值、顺序、来源），转义规则单一实现。
- 确定性：固定输入下列表序、诊断顺序与序列化字节一致；未应用草稿不产生修订、不改变任何输入切片。

## 6. 测试与证据

| 测试文件 | 覆盖 |
| --- | --- |
| RequirementsModelTest | 字段矩阵、单位/有限性/引用校验、JSON 往返（§15.3 参数容差 1e-12） |
| CsvIoTest | 恶意 CSV（公式注入）、错误行定位、正确行保留、模板往返（AT-02） |
| PoseRegionTest | 部分位姿约束、4/5 轴子空间声明、边界包含、分母定义、采样预算拒绝 |
| LoadEventTest | 事件时间线、缺失关键数据 → 就绪 DataInsufficient 语义、循环口径稳定 |
| ReadinessTest | 就绪状态机全矩阵（非法 Must/仅 Should/预览/正式），预览不产生正式证据 |
| CommandIntegrationTest | 应用＝一个新修订＋按字段失效；未应用不失效；undo/redo 经 WP-04（AT-05 相关面） |
| RequirementsGuiTest | 批量编辑、筛选、单位显示、错误定位、就绪摘要（QT_QPA_PLATFORM=windows） |

往返夹具先过 `powershell -NoProfile -ExecutionPolicy Bypass -File .\schemas\validate-schemas.ps1`。证据写入 `evidence/WP-14/`：CSV 往返样例、非法行报告、就绪判定矩阵、修订/失效矩阵、GUI 报告与独立评审签名。验证命令（双形式，仓库根执行）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_requirements(_contract)?_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_requirements_gui_test$'
```

原生回退：

```powershell
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_requirements_test sdurws_ird_requirements_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_requirements(_contract)?_test$"
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_requirements_gui_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_requirements_gui_test$"
```

GUI 约束：Visual Studio x64 环境设置 `$env:QT_QPA_PLATFORM='windows'`，一次只启动一个 GUI 测试可执行文件。

## 7. 迁移与删除表

| 旧资产 | 处置（requirements §13） | 门禁 |
| --- | --- | --- |
| 旧 `sdurws_engineeringrequirements` 表格作权威数据的链路 | Rewrite：UI 表格退为编辑视图，权威归 `EngineeringRequirements` | 字段/单位/就绪矩阵闭合 |
| 插件内自有 CSV/路径解析与公式处理 | 删除，统一经 WP-11 安全端口 | 边界扫描零命中 |
| 旧需求格式文件 | 只读迁移输入；不建兼容层 | 迁移证据归档后删除适配器 |
| 旧目标 `sdurws_engineeringrequirements` | 不作依赖；阶段 B 验收后退出构建与安装包 | 安装包审计 |
