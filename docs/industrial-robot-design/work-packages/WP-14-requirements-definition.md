# WP-14 需求定义实施计划

> 阶段/发布：阶段 B / R1（轨迹和动力字段只保存并校验，不在本 WP 执行完整动态求值）；负责 WP：WP-14。
> 实施语义唯一来源：`module-design/requirements-definition.md` v0.3（需求基线 v0.7；检查点 `IRD-D2-20260829`）。
> 前置（总纲 §5.3，保持不变）：WP-03～05、WP-10、WP-11、WP-13。人周：5～8。
> 模块详设补充（不改总纲口径）：代码前置为 WP-03、04、09、11（WP-10 为 GUI 层前置）；WP-05、WP-13 为交付/契约前置——快照与模型作用域经修订查询获得，无业务插件代码依赖。
> 治理状态：Planned（D6 深化重写；需求、架构契约与模块详设均处 Proposed 时不得进入实现）。

**需求与契约：** REQ-01～08、AT-02/18（阶段 B 链路）；清单见 §2。  
**拥有目录：** `industrialrobot/plugins/requirements/` 及其测试（文件树见 §3）。  
**输入/输出：** 输入＝CSV 任务点表/工艺段/负载（经 WP-11）＋模型引用；输出＝`EngineeringRequirements`＋`LoadCase`（见 §4）。

## 1. 目标与非目标

**目标**

- 成为 `EngineeringRequirements`（SYM-DOM-006）与 `LoadCase`（SYM-DOM-007）的唯一编辑所有者：任务点（参考坐标系、TCP 引用、位姿与受约束分量、容差、Must/Should）、区域（Box、采样密度、最低覆盖率）、工艺段与负载事件建模，CSV 安全导入导出与就绪校验状态机（requirements-definition.md §1）。
- 输出经 WP-04 领域命令产生修订，供 WP-15/16/17/20 经输入切片消费。
- 完成定义：REQ-01～08 全部有测试与证据；非法输入不产生修订；固定输入下列表序、诊断顺序与序列化字节一致。

**非目标**

- 运动学/轨迹/动力学求值（WP-15～17）、碰撞判定（WP-07）、执行调度（WP-08）、快照构建（WP-05）、GUI 会话状态权威（WP-10）。
- 轨迹/动力字段只保存并校验，不在本 WP 执行完整动态求值。
- 持久化字段不复述：以 `schemas/engineering-requirements.schema.json` 与 `schemas/examples/engineering-requirements.example.json` 为准。

## 2. 需求、契约与发布切片

- 需求锚点（requirements-definition.md §0）：§7.1～7.2、§8.2（REQ-01～08）、§15.3；场景 AT-02、AT-03、AT-18。
- 架构契约：`architecture/domain-model.md`、`architecture/persistence-schema.md`、`architecture/public-interfaces.md`、`architecture/symbol-registry.md`。
- 代码前置：WP-03 core、WP-04 命令/查询公共头、WP-09 diagnostics、WP-11 io（CSV 安全读写）；GUI 层前置 WP-10；构建/门禁入口 WP-01。
- 发布切片：七项任务全部属阶段 B / R1；阶段 C/D 无本 WP 新增范围。

## 3. 拥有目录、CMake 目标与依赖边界

拥有目录（requirements-definition.md §2 文件树，唯一允许修改范围）：

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

CMake 目标（与模块详设 v0.3 完全一致，不得增删改名）：`sdurws_ird_requirements`（计算核心，无 Qt Widgets）、`sdurws_ird_requirements_plugin`（薄插件）、`sdurws_ird_requirements_test`、`sdurws_ird_requirements_contract_test`、`sdurws_ird_requirements_gui_test`。

允许依赖：WP-03 core、WP-04 命令/查询公共头、WP-09 diagnostics、WP-11 io、Qt Core；GUI 层另加 Qt Widgets 与 WP-10 ui。
禁止：其他业务插件私有头、插件内第二套 CSV/路径解析、直接执行公式、直写项目目录、修改领域公共枚举。通过 WP-04 命令服务提交，不直接写项目文件。

## 4. 输入、输出与固定时序

| 方向 | 工件 |
| --- | --- |
| 输入 | CSV 文件（按三列字典）、手工编辑草稿（WP-10 `EditDraft`）、模型作用域（frameId/tcpRef 解析，经 WP-04 修订查询） |
| 输出 | `EngineeringRequirements`（SYM-DOM-006）与 `LoadCase`（SYM-DOM-007）修订、就绪判定结果、结构化诊断；供 WP-15 消费的冻结分母定义数据 |

固定时序（requirements-definition.md §4，不得重排）：CSV 导入/手工编辑（`EditDraft`）→ 校验（有限性→单位→容差>0→引用存在→采样预算）→ 条目级与聚合就绪判定 → 用户应用 → `IProjectCommandService.apply`（恰好一个新修订；未应用草稿不失效下游，CON-05）。
预览（部分预览）只取 Valid 条目、定位全部错误行、不产生正式 Pass/Verified/报告证据（REQ-06/AT-02）；正式运行前聚合就绪必须为"可计算"。

失败分类与错误码（requirements-definition.md §4 矩阵；新码待 diagnostics.md 登记后启用）：`IRD-REQ-ROW-INVALID`（Input/Error，按源行号/字段定位，正确行不受影响 AT-02）、`IRD-REQ-REFERENCE-UNRESOLVED`（Input/Error）、`IRD-REQ-NOT-READY`（Input/Error，阻止调度）、`IRD-REQ-SAMPLING-BUDGET`（Engineering/Error）。

## 5. 任务 DAG

```text
T01 数据模型与单位校验 ─┬→ T02 CSV 导入导出
                        ├→ T03 姿态/区域语义 ─┐
                        └→ T04 负载与工艺事件 ┴→ T05 就绪状态机 → T06 项目命令集成 → T07 阶段 B GUI
```

| 任务 | WP 内前置 | 外部门禁 |
| --- | --- | --- |
| T01 | — | WP-03/04/09/11 |
| T02 | T01 | WP-11 CSV 端口 |
| T03 | T01 | — |
| T04 | T01 | WP-13-T05（工具/负载引用语义，交付前置） |
| T05 | T01、T03、T04 | WP-10 `StageStatusModel` 契约 |
| T06 | T05 | WP-04 命令端口 |
| T07 | T06 | WP-10 公共组件 |

每任务一张任务卡、一个 worktree/分支/提交（总纲 §4.3）。

## 6. 逐任务计划

### 6.1 WP-14-T01 数据模型与单位校验（1～1.5 人周）

- 代码范围：`include/sdurws/ird/requirements/` 全部模型头＋`src/RequirementsModel.cpp`、`src/TaskPoint.cpp` 骨架；`test/RequirementsModelTest.cpp`。
- 前置：无 WP 内前置。
- 输出工件：冻结字段模型（模块详设 §3 表）——任务点 `taskId/localName`、`priority(Must/Should)`、`frameId`（解析为 objectId）、`tcpRef`（`ToolDefinition` objectId 或 `RobotDesign.defaultTcp`）、`targetPose`（四元数＋位置）、`constrainedComponents ⊆ {X,Y,Z,Roll,Pitch,Yaw}`、`positionTolerance(>0,m)`/`orientationTolerance(rad,测地角)`、`approach/retract`。
- 验收断言：requirements-definition.md §6「RequirementsModelTest」——字段矩阵、单位/有限性/引用校验、JSON 往返（§15.3 参数容差 1e-12）。规则：单位 SI；姿态一律单位四元数＋显式参考坐标系，RPY 仅界面输入/显示换算 helper，不进持久化结构；容差必须有限且 >0；不得依赖"当前选中坐标系"；`tcpRef` 必须可解析。

### 6.2 WP-14-T02 CSV 导入导出（1～1.5 人周）

- 代码范围：`src/RequirementsCsvAdapter.cpp`＋`include/.../RequirementsCsvAdapter.hpp`；`resources/requirements/`（三份列字典＋`templates/`）；`test/CsvIoTest.cpp`；`testdata/requirements/{csv-valid,csv-malicious}/`。
- 前置：T01；WP-11 安全读写端口。
- 输出工件：CSV 三列字典（模块冻结，风格对齐 `schemas/catalog/column-dictionary.schema.json`）：任务点表列 `task_id(string,必填)`、`task_name(string,必填)`、`priority(Must|Should,必填)`、`frame_id(string,必填)`、`tcp_ref(string,可选→defaultTcp)`、`pos_x/pos_y/pos_z(decimal,m,必填)`、`quat_x/quat_y/quat_z/quat_w(decimal,无量纲,必填)`、`position_tolerance(decimal,m,必填,>0)`、`orientation_tolerance(decimal,rad,必填,>0)`、`constrained_components(string,逗号分隔,可选→全集)`、`approach_*/retract_*`（可选列组）；区域表与负载表各持独立列字典。
- 验收断言：§6「CsvIoTest」——恶意 CSV（公式注入）、错误行定位、正确行保留、模板往返（AT-02）。CSV 安全全部经 WP-11 reader/writer（`sourceLine/fieldName/rawText/normalizedValue` 逐行诊断；公式样式文本不执行、导出统一转义，NFR-SEC-03）；往返稳定（值、顺序、来源），转义规则单一实现。

### 6.3 WP-14-T03 姿态/区域语义（0.5～1 人周）

- 代码范围：`src/PoseRegion.cpp`＋`include/.../PoseRegion.hpp`；`test/PoseRegionTest.cpp`；`testdata/requirements/regions/`。
- 前置：T01。
- 输出工件：区域冻结字段——`regionId`、`priority`、`frameId`、Box min/max、`positionSampleCount/orientationSampleCount`、`minCoverageRatio`；部分位姿约束（受约束分量集合声明）。
- 验收断言：§6「PoseRegionTest」——部分位姿约束、4/5 轴子空间声明、边界包含、分母定义、采样预算拒绝。冻结口径：4/5 轴任务语义只声明分量集合（REQ-01），子空间 Jacobian 构造归 WP-15（§15.3）；网格默认含边界；分母＝计划的位置—姿态组合＝`positionSampleCount×orientationSampleCount`（供 WP-15 消费的冻结定义）；**单区域计划组合上限 1,000,000（模块冻结，超出拒绝→`IRD-REQ-SAMPLING-BUDGET`）**；覆盖率定义权在 WP-15，本模块只持久化采样密度与最低覆盖率。

### 6.4 WP-14-T04 负载与工艺事件（0.5～1 人周）

- 代码范围：`src/LoadEvent.cpp`＋`include/.../LoadEvent.hpp`；`test/LoadEventTest.cpp`；`testdata/requirements/loads/`。
- 前置：T01；WP-13-T05（交付前置）。
- 输出工件：`LoadCase` 冻结事件字段——`loadCaseId`、`payloadMass`、`payloadCenterOfMass`（显式 frameId）、`externalWrench`（force N/torque N·m，显式 frameId）、`dwellTime`、`events[]`（grip/release，挂接 taskId 与序列）。
- 验收断言：§6「LoadEventTest」——事件时间线、缺失关键数据 → 就绪 DataInsufficient 语义、循环口径稳定。语义：接近/撤离沿工具轴或参考轴表达方向与距离（REQ-02）；夹取/释放/驻留构成完整任务循环（REQ-04）；缺关键负载数据按 REQ-06 语义阻止正式运行（DataInsufficient 通道），不静默默认。

### 6.5 WP-14-T05 就绪状态机（0.5～1 人周）

- 代码范围：`src/ReadinessChecker.cpp`＋`include/.../ReadinessChecker.hpp`；`test/ReadinessTest.cpp`；`testdata/requirements/ready-matrix/`。
- 前置：T01、T03、T04。
- 输出工件：就绪状态机——条目级 `{Valid, Invalid(行/列定位)}`；聚合 `{输入未完成, 可计算}`。
- 验收断言：§6「ReadinessTest」——就绪状态机全矩阵（非法 Must/仅 Should/预览/正式），预览不产生正式证据。规则：任一启用 Must 非法 → 输入未完成，阻止正式运行；仅 Should 非法 → 可计算＋警告可见（REQ-06）；聚合状态映射 WP-10 `StageStatusModel` 八值。

### 6.6 WP-14-T06 项目命令集成（0.5～1 人周）

- 代码范围：`src/RequirementsCommands.cpp`＋`include/.../RequirementsCommands.hpp`；`test/CommandIntegrationTest.cpp`。
- 前置：T05；WP-04 命令端口。
- 输出工件：需求编辑领域命令（`DomainCommand.validate/buildMutations` 纯函数）。
- 验收断言：§6「CommandIntegrationTest」——应用＝一个新修订＋按字段失效；未应用不失效（CON-05）；undo/redo 经 WP-04（AT-05 相关面）。确定性：未应用草稿不产生修订、不改变任何输入切片。

### 6.7 WP-14-T07 阶段 B GUI（1 人周）

- 代码范围：`gui/RequirementsPlugin.hpp`、`gui/RequirementsPlugin.cpp`、`gui/panels/`；`test/RequirementsGuiTest.cpp`；CMake 目标 `sdurws_ird_requirements_plugin`、`sdurws_ird_requirements_gui_test`。
- 前置：T06；WP-10 公共组件。
- 输出工件：批量编辑、筛选、单位显示、错误定位、就绪摘要的薄界面。
- 验收断言：§6「RequirementsGuiTest」——批量编辑、筛选、单位显示、错误定位、就绪摘要（QT_QPA_PLATFORM=windows）；不显示内部 Schema/哈希作为主操作。

## 7. 测试矩阵

以 requirements-definition.md §6 为唯一基准（本 WP 不自行扩大或放宽）：

| 测试文件 | 覆盖要点 | 归属任务 |
| --- | --- | --- |
| RequirementsModelTest | 字段矩阵、单位/有限性/引用校验、JSON 往返（§15.3 参数容差 1e-12） | T01 |
| CsvIoTest | 恶意 CSV（公式注入）、错误行定位、正确行保留、模板往返（AT-02） | T02 |
| PoseRegionTest | 部分位姿约束、4/5 轴子空间声明、边界包含、分母定义、采样预算拒绝 | T03 |
| LoadEventTest | 事件时间线、缺失关键数据 → 就绪 DataInsufficient 语义、循环口径稳定 | T04 |
| ReadinessTest | 就绪状态机全矩阵（非法 Must/仅 Should/预览/正式），预览不产生正式证据 | T05 |
| CommandIntegrationTest | 应用＝一个新修订＋按字段失效；未应用不失效；undo/redo 经 WP-04（AT-05 相关面） | T06 |
| RequirementsGuiTest | 批量编辑、筛选、单位显示、错误定位、就绪摘要（QT_QPA_PLATFORM=windows） | T07 |

## 验证命令（双形式，仓库根执行）

往返夹具先过 Schema 校验：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\schemas\validate-schemas.ps1`。

脚本形式：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_requirements(_contract)?_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_requirements_gui_test$'
```

原生回退（PowerShell 5.1，禁 pwsh）：

```powershell
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_requirements_test sdurws_ird_requirements_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_requirements(_contract)?_test$"
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_requirements_gui_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_requirements_gui_test$"
```

GUI 约束：Visual Studio x64 环境设置 `$env:QT_QPA_PLATFORM='windows'`，一次只启动一个 GUI 测试可执行文件。

## 9. 独立验证与评审

- 独立验证者（黑盒）：字段/单位/引用校验矩阵、CSV 恶意样本与错误行定位、就绪判定矩阵、命令修订/失效矩阵、GUI 报告。
- 独立评审者：需求符合性（REQ-01～08）、架构边界（无第二套 CSV 解析、无直写项目目录）、代码质量。
- 产品/机械评审人：确认任务点/区域/负载事件工程语义、容差与 Must/Should 规则、采样密度上限的可用性。
- 角色分离：实现者不得担任同任务最终评审者（总纲 §4.1）。

## 10. 迁移与删除表

| 旧资产 | 处置（requirements §13） | 门禁 |
| --- | --- | --- |
| 旧 `sdurws_engineeringrequirements` 表格作权威数据的链路 | Rewrite：UI 表格退为编辑视图，权威归 `EngineeringRequirements` | 字段/单位/就绪矩阵闭合 |
| 插件内自有 CSV/路径解析与公式处理 | 删除，统一经 WP-11 安全端口 | 边界扫描零命中 |
| 旧需求格式文件 | 只读迁移输入；不建兼容层 | 迁移证据归档后删除适配器 |
| 旧目标 `sdurws_engineeringrequirements` | 不作依赖；阶段 B 验收后退出构建与安装包 | 安装包审计 |

## 退出条件

- REQ-01～08、AT-02、AT-03、AT-18 阶段 B 子链路通过（阶段 B 门禁，总纲 §8.2）。
- 所有 Must/Should 状态可观察；非法输入不产生修订；区域计划组合上限 1,000,000 生效；CSV 往返稳定（值、顺序、来源）。
- WP-15 可消费冻结分母定义（`positionSampleCount×orientationSampleCount`，含边界样本）；执行工件版本化且可追溯。
- 证据写入 `evidence/WP-14/` 并签署：CSV 往返样例、非法行报告、就绪判定矩阵、修订/失效矩阵、GUI 报告与独立评审签名。

## 12. 人周与追踪

| 任务 | 人周 |
| --- | ---: |
| T01 | 1～1.5 |
| T02 | 1～1.5 |
| T03 | 0.5～1 |
| T04 | 0.5～1 |
| T05 | 0.5～1 |
| T06 | 0.5～1 |
| T07 | 1 |
| 合计 | 5～8（总纲 §5.3，保持不变） |

需求追踪：`requirement-traceability.csv` 中 REQ-01～08 主实现＝WP-14。

## 任务卡索引

- [WP-14-T01 数据模型与单位校验](../agent-tasks/WP-14-T01-requirements-model.md)
- [WP-14-T02 CSV 导入导出](../agent-tasks/WP-14-T02-csv-io.md)
- [WP-14-T03 姿态/区域语义](../agent-tasks/WP-14-T03-pose-region.md)
- [WP-14-T04 负载与工艺事件](../agent-tasks/WP-14-T04-load-events.md)
- [WP-14-T05 就绪状态机](../agent-tasks/WP-14-T05-readiness.md)
- [WP-14-T06 项目命令集成](../agent-tasks/WP-14-T06-command-integration.md)
- [WP-14-T07 阶段 B GUI](../agent-tasks/WP-14-T07-requirements-ui.md)
