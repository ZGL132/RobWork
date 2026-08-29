# WP-13 机械臂建模实施计划

> 阶段/发布：阶段 B / R1（仅实现 OPT-B 所需模型能力）；负责 WP：WP-13。
> 实施语义唯一来源：`module-design/robot-modeling.md` v0.3（需求基线 v0.8；检查点 `IRD-D2-20260829`）。
> 前置（总纲 §5.3，保持不变）：WP-06、WP-10、WP-11；共同构建/门禁入口 WP-01、黄金数据框架 WP-02。人周：8～12。
> 治理状态：Planned（D6 深化重写；需求、架构契约与模块详设契约与详设已于 IRD-D10-20260829 联合评审 Accepted；实现启动按总纲依赖顺序与任务状态账本）。

**需求与契约：** MDL-01～14、AT-01/16/18（阶段 B 链路）；架构契约与模块方案清单见 §2。  
**拥有目录：** `industrialrobot/plugins/modeling/` 及其测试（文件树见 §3）。  
**输入/输出：** 输入＝DH/显式/URDF/网格/材料（经 WP-11 安全读取）＋EditDraft；输出＝`RobotDesign`＋编译请求→WP-06 `CompiledRobotArtifacts`（见 §4）。

## 1. 目标与非目标

**目标**

- 成为 `RobotDesign`（SYM-DOM-002）、`JointDefinition`（SYM-DOM-003）、`ToolDefinition`（SYM-DOM-004）、`EnvironmentModel`（SYM-DOM-005）的唯一编辑与导入所有者：模板创建、DH/显式关节权威参数化编辑、`StandardDH`↔`ExplicitJoint` 转换判定（SYM-KIN-001～003）、URDF 导入语义映射、物性解析估算与权威覆盖、工具/TCP/环境编辑（robot-modeling.md §1）。
- 应用修改时经 WP-06 端口原子编译运行时工件（MDL-06）；一切应用经 WP-04 领域命令产生恰好一个新修订。
- 完成定义：MDL-01～14 全部有测试与证据；任意导入/转换/编译失败不产生修订；同输入下导入报告条目序、转换判定、估算值与序列化字节一致（确定性，robot-modeling.md §5）。

**非目标**

- FK/IK 计算（WP-15）、碰撞策略（WP-07）、项目持久化实现（WP-04）、GUI 会话状态权威（WP-10）。
- **不拥有 `CompiledCandidateArtifact`（归 WP-20）**：本 WP 输出＝`RobotDesign`＋编译请求（`ModelingCompileRequest` 调 WP-06 端口，返回 `CompiledRobotArtifacts` 全成全败）；symbol-registry §4 裁决 5（`CompiledRobotArtifacts` 与 `CompiledCandidateArtifact` 不得混用）。
- 变换链、q-zero、轴与四元数规则以 `architecture/canonical-kinematics.md` §1～§8 为准；DH↔显式转换语义与五状态判定以需求 §7.3.2/§8.1.1 为准——本计划一律引用，不复述。

## 2. 需求、契约与发布切片

- 需求锚点（robot-modeling.md §0）：§7.1～7.3、§8.1（含 §8.1.1/§8.1.2）、§15.3；场景 AT-01、AT-15～18。
- 架构契约：`architecture/canonical-kinematics.md`（最高权威）、`architecture/domain-model.md`、`architecture/persistence-schema.md`、`architecture/public-interfaces.md`（§1 命令端口、§2 编译/名称端口）、`architecture/symbol-registry.md`。
- 代码前置：WP-06（`CanonicalModelCompiler`/`CompiledRobotArtifacts`/`IRuntimeNameResolver`）、WP-10（`EditDraft`/公共组件）、WP-11（URDF/网格安全读取）；WP-04 命令端口经公共头合法可用（端口契约不构成代码前置，总纲 §5.2 注记）；构建/门禁入口 WP-01。
- 发布切片：八项任务全部属阶段 B / R1；阶段 C/D 无本 WP 新增范围。

## 3. 拥有目录、CMake 目标与依赖边界

拥有目录（robot-modeling.md §2 文件树，唯一允许修改范围）：

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/
  include/sdurws/ird/modeling/
    RobotDesignEditor.hpp   DhConversionService.hpp   UrdfImportAdapter.hpp
    MaterialInertiaEstimator.hpp   ToolEnvironmentEditor.hpp
    ModelingCommands.hpp   ModelingCompileRequest.hpp   ModelingDiagnostics.hpp
  src/RobotDesignEditor.cpp   DhConversionService.cpp   UrdfImportAdapter.cpp
      MaterialInertiaEstimator.cpp   ToolEnvironmentEditor.cpp
      ModelingCommands.cpp   ModelingCompileRequest.cpp
  gui/ModelingPlugin.hpp   gui/ModelingPlugin.cpp   gui/panels/
  test/ModelFixtureTest.cpp   DomainEditorTest.cpp   UrdfImportTest.cpp
      DhConversionTest.cpp   MaterialToolTest.cpp   RuntimeCompileTest.cpp
      IdentityRegressionTest.cpp   ModelingGuiTest.cpp
  testdata/modeling/{dh,explicit,urdf,axes,branches,materials,failpoints}/
  # 证据 → out/test-evidence/wp-13/<run-id>/（AGENTS §3，不入源码树）
```

CMake 目标（与模块详设 v0.3 完全一致，不得增删改名）：`sdurws_ird_modeling`（计算核心，无 Qt Widgets）、`sdurws_ird_modeling_plugin`（薄插件）、`sdurws_ird_modeling_test`、`sdurws_ird_modeling_contract_test`、`sdurws_ird_modeling_gui_test`。

允许依赖：WP-03 core、WP-04 命令/查询公共头、WP-06 runtime、WP-09 diagnostics、WP-11 io、Qt Core；GUI 层另加 Qt Widgets 与 WP-10 ui。
禁止：RobWork 头进入计算核心（编译一律经 WP-06）、其他业务插件私有头、自行拼接/剥离运行时名称、直写项目目录、第二套 XML/URDF 解析。不得修改 WP-06 名称解析实现、WP-04 持久化实现或其他业务插件 Widget。

## 4. 输入、输出与固定时序

| 方向 | 工件 |
| --- | --- |
| 输入 | 模板/编辑草稿（WP-10 `EditDraft`）、URDF/网格/材料文件（WP-11 安全读取：解析预算内、禁 DOCTYPE/外部实体/网络）、当前修订（WP-04 查询端口） |
| 输出 | `RobotDesign`＋`ModelingCompileRequest`（`RobotDesign` 内容身份＋目标工件集）→ WP-06 端口 → `CompiledRobotArtifacts`（全成全败）；`UrdfImportReport`/`DhConversionVerdict`/`SectionEstimate` 等模块私有报告类型；结构化诊断；恰好一个新修订（仅成功应用时） |

编辑/导入固定时序（robot-modeling.md §4，不得重排）：WP-11 安全读取 → 语义映射（`<origin>`/`<axis>`、continuous/prismatic、分支拓扑）→ 草稿＋导入报告 → 用户确认 → `DomainCommand.validate/buildMutations`（纯函数）→ **预编译**（WP-06 WorkCell＋DWC＋`RuntimeNameMap`，任一失败整体为空）→ 成功才 `IProjectCommandService.apply`（恰好一个新修订）；失败时修订号不变并返回诊断。

权威切换与重命名同走命令：`SetKinematicAuthority` 只产生一个新修订、旧修订保留转换证据；`RenameObject` 不改 `objectId`，触发 WP-06 名称表重编译（sliceHash 不变）。

失败分类与错误码（robot-modeling.md §4 矩阵；新码待 diagnostics.md 登记后启用，恢复动作以矩阵为准）：`IRD-MDL-IMPORT-BLOCKED`（Input/Error）、`IRD-MDL-CONVERSION-INEXACT`（Engineering/Warning）、`IRD-MDL-CONVERSION-FAILED`（System/Error）、`IRD-MDL-PROPERTIES-INSUFFICIENT`（Engineering/Warning）、`IRD-MDL-TOOL-REF-UNRESOLVED`（Input/Error）。

## 5. 任务 DAG

```text
T01 黄金夹具
  └→ T02 领域编辑器 ─┬→ T03 URDF 导入 ──┐
                     ├→ T04 DH 转换 ────┼→ T06 运行时编译 → T07 身份回归
                     └→ T05 物性与工具 ─┘        └──────────→ T08 插件与 GUI
```

| 任务 | WP 内前置 | 外部门禁 |
| --- | --- | --- |
| T01 | — | WP-01、WP-02 |
| T02 | T01 | 同上 |
| T03 | T02 | WP-11 安全读取端口 |
| T04 | T02 | — |
| T05 | T02 | WP-11 资源校验 |
| T06 | T02、T03、T04、T05 | WP-06 编译端口 |
| T07 | T06 | — |
| T08 | T02、T06 | WP-10 公共组件 |

每任务一张任务卡、一个 worktree/分支/提交（总纲 §4.3）。

## 6. 逐任务计划

### 6.1 WP-13-T01 模型类型与失败夹具（0.5～1 人周）

- 代码范围：`testdata/modeling/`（dh、explicit、urdf、axes、branches、materials、failpoints 七类夹具）＋ `test/ModelFixtureTest.cpp`。
- 前置：无 WP 内前置；WP-01 构建入口与 WP-02 黄金数据框架可用。
- 输出工件：黄金输入夹具集（含夹具哈希清单）＋失败断言先行。
- 验收断言：robot-modeling.md §6「ModelFixtureTest」——DH/显式/URDF 任意轴、缺失/零/非单位轴、4/7 轴边界、不支持拓扑（失败断言先行）；夹具与 `schemas/examples/robot-design.example.json` 同构并通过 `validate-schemas.ps1`。

### 6.2 WP-13-T02 领域编辑器（1.5～2 人周）

- 代码范围：`src/RobotDesignEditor.cpp`＋`include/.../RobotDesignEditor.hpp`；`src/ModelingCommands.cpp`＋`ModelingCommands.hpp`（编辑应用命令构建）；`ModelingDiagnostics.hpp` 首批诊断映射。
- 前置：T01。
- 输出工件：`RobotDesignDraft`（对接 WP-10 `EditDraft`；非法草稿不产生命令、不触发失效）。
- 验收断言：§6「DomainEditorTest」——草稿校验、唯一 robotId、目标主链、关节限制、来源保留；非法草稿零修订；`ExplicitJoint` 下 Axis 一等可编辑、`StandardDH` 下只读派生（MDL-09，robot-modeling.md §5）。

### 6.3 WP-13-T03 URDF 导入（1.5～2 人周）

- 代码范围：`src/UrdfImportAdapter.cpp`＋`include/.../UrdfImportAdapter.hpp`；`test/UrdfImportTest.cpp`；`testdata/modeling/urdf/`。
- 前置：T02；WP-11 安全读取端口。
- 输出工件：`UrdfImportReport`（Error/Warning/Info 条目、字段路径、源值/采用值、原因、建议动作、被排除分支清单；确认记录持久化前禁止 Verified）。
- 验收断言：§6「UrdfImportTest」——需求 §8.1.2 全规则＋AT-15/AT-17，导入报告逐项可观察。边界要点（引用 robot-modeling.md §5，不复述全文）：缺失 `<axis>` → 局部 +X＋`Defaulted` 待确认草稿；零/非法轴仅生成报告并阻止新修订；`continuous` 保留语义并要求用户确认工程工作范围；planar/floating/Mimic/闭环在目标链上阻止成模（不得转 FixedFrame 或绕过选择）、在被排除分支完整报告；URDF→DH 必须走可表达性验证，自动投影不具权威性。

### 6.4 WP-13-T04 DH 转换（1～1.5 人周）

- 代码范围：`src/DhConversionService.cpp`＋`include/.../DhConversionService.hpp`；`ModelingCommands.cpp` 的 `SetKinematicAuthority` 命令；`test/DhConversionTest.cpp`；`testdata/modeling/{dh,explicit}/`。
- 前置：T02。
- 输出工件：`DhConversionVerdict`（status＝§7.3.2 五状态 Exact/ExactNonUnique/Approximate/NotRepresentable/AnalysisFailed；逐项残差、规范化方案、算法版本）。
- 验收断言：§6「DhConversionTest」——四类黄金模型判定＋`AnalysisFailed` 故障注入（系统诊断不得伪装成 `NotRepresentable`）；往返满足 §15.3 转换容差（AT-16；Zero/Home/边界/固定种子 100 姿态 FK＋世界轴线＋附属 Frame）。语义引用：DH→显式按 §8.1.1 统一 schilling 约定 `Rz(theta)·Tz(d)·Tx(a)·Rx(alpha)` 确定性生成；DH `offset` 只参与生成 q=0 的 `OriginPose`（canonical-kinematics §3）；显式→DH 先可表达性分析、再预览、后切换；`Approximate` 只读、禁止设为权威（MDL-10）；反算不承诺逐项恢复 DH 数值。

### 6.5 WP-13-T05 物性与工具（1～1.5 人周）

- 代码范围：`src/MaterialInertiaEstimator.cpp`＋`src/ToolEnvironmentEditor.cpp`（含同名公共头）；`test/MaterialToolTest.cpp`；`testdata/modeling/materials/`。
- 前置：T02；WP-11 路径/资源校验。
- 输出工件：`SectionEstimate`（截面类型与参数、mass、COM、inertia 参考系显式；`ValueProvenance=Estimated`；权威覆盖保留原来源与备注，§7.1）；工具/TCP/环境编辑结果。
- 验收断言：§6「MaterialToolTest」——三类截面（实心圆/空心圆/矩形）解析算例（§15.3 动力学相对容差）、权威覆盖保留原来源、工具 TCP 不复制几何（MDL-13）。口径裁决（robot-modeling.md §5，MDL-05）：估算公式以半径表达，持久化截面尺寸为直径制（`schemas/robot-design.schema.json` 的 `section.dimensions`，单位 m），转换固定在本估算器边界（r=d/2），不产生第二套持久化语义；每个估算张量执行正定性与三角不等式校验；该公式表是全产品解析估算的唯一语义源（WP-20 派生重算引用，optimization.md §5）。

### 6.6 WP-13-T06 运行时编译（1～1.5 人周）

- 代码范围：`src/ModelingCompileRequest.cpp`＋`include/.../ModelingCompileRequest.hpp`；`test/RuntimeCompileTest.cpp`。
- 前置：T02、T03、T04、T05；WP-06 编译端口。
- 输出工件：`ModelingCompileRequest` → WP-06 `CanonicalModelCompiler`，产出 `CompiledRobotArtifacts`（canonical＋names＋WorkCell＋DWC＋诊断，全成全败）；应用时序中"预编译成功才 apply"的衔接逻辑。
- 验收断言：§6「RuntimeCompileTest」——MDL-06/14：双编译 failpoint 全败零修订、名称映射双向一致、交叉校验清单。禁止在本 WP 拼接/剥离运行时名称；RobWork 指针只存在于 WP-06 builder 内。

### 6.7 WP-13-T07 单机械臂与身份回归（0.5～1 人周）

- 代码范围：`test/IdentityRegressionTest.cpp`；回归所需夹具入 `testdata/modeling/`。
- 前置：T06。
- 输出工件：身份回归矩阵（复制/导入/删除/重命名/目标链切换；objectId 稳定；重命名触发名称表重编译且 sliceHash 不变）。
- 验收断言：§6「IdentityRegressionTest」——objectId 稳定、AT-18 阶段 B 子链路；多可动分支仅保留导入证据，不进入首版计算模型。

### 6.8 WP-13-T08 插件与 GUI（1～1.5 人周）

- 代码范围：`gui/ModelingPlugin.hpp`、`gui/ModelingPlugin.cpp`、`gui/panels/`；`test/ModelingGuiTest.cpp`；CMake 目标 `sdurws_ird_modeling_plugin`、`sdurws_ird_modeling_gui_test`。
- 前置：T02、T06；WP-10 公共组件。
- 输出工件：最薄建模入口（错误定位、应用确认、未应用草稿不失效）。
- 验收断言：§6「ModelingGuiTest」——薄插件：错误定位、应用确认、未应用草稿不失效（QT_QPA_PLATFORM=windows）。GUI 不直接写文件或领域对象。

## 7. 测试矩阵

以 robot-modeling.md §6 为唯一基准（本 WP 不自行扩大或放宽）：

| 测试文件 | 覆盖要点 | 归属任务 |
| --- | --- | --- |
| ModelFixtureTest | T01 黄金输入：任意轴/缺失/零/非单位轴、4/7 轴边界、不支持拓扑（失败断言先行） | T01 |
| DomainEditorTest | 草稿校验、唯一 robotId、目标主链、关节限制、来源保留；非法草稿零修订 | T02 |
| UrdfImportTest | §8.1.2 全规则＋AT-15/AT-17；导入报告逐项可观察 | T03 |
| DhConversionTest | 四类黄金模型判定＋`AnalysisFailed` 故障注入；往返满足 §15.3 转换容差（AT-16） | T04 |
| MaterialToolTest | 三类截面解析算例、权威覆盖保留原来源、工具 TCP 不复制几何（MDL-13） | T05 |
| RuntimeCompileTest | MDL-06/14：双编译 failpoint 全败零修订、名称映射双向一致、交叉校验清单 | T06 |
| IdentityRegressionTest | 复制/导入/删除/重命名/目标链切换、objectId 稳定、AT-18 阶段 B 子链路 | T07 |
| ModelingGuiTest | 薄插件：错误定位、应用确认、未应用草稿不失效（QT_QPA_PLATFORM=windows） | T08 |

## 验证命令（双形式，仓库根执行）

往返夹具先过 Schema 校验：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\schemas\validate-schemas.ps1`（`testdata/modeling/` 样本与 `robot-design.example.json` 同构）。

脚本形式：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_modeling(_contract)?_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_modeling_gui_test$'
```

原生回退（PowerShell 5.1，禁 pwsh）：

```powershell
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_modeling_test sdurws_ird_modeling_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_modeling(_contract)?_test$"
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_modeling_gui_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_modeling_gui_test$"
```

GUI 约束：Visual Studio x64 环境设置 `$env:QT_QPA_PLATFORM='windows'`，一次只启动一个 GUI 测试可执行文件。

## 9. 独立验证与评审

- 独立验证者（黑盒）：黄金夹具矩阵、失败诊断样例、导入报告可观察性、往返容差、GUI 回归；不得依实现者口头说明降低断言。
- 独立评审者：需求符合性（MDL-01～14）、架构边界（无 RobWork 头入计算核心、无名称拼接）、代码质量。
- 产品/机械评审人：确认物性解析估算公式适用性（MDL-05 唯一语义源）、URDF 边界工程语义、`ExactNonUnique` 规范化方案初版。
- 角色分离：实现者不得担任同任务最终评审者（总纲 §4.1）。

## 10. 迁移与删除表

| 旧资产 | 处置（requirements §13） | 门禁 |
| --- | --- | --- |
| `JointTransformSpec`（RPY+Pos+隐式 Z 轴）建模链路 | Rewrite 为 `JointDefinition`＋显式 Axis | §13.3 消除项；三入口黄金模型通过 |
| 旧 URDF 导入缺失 axis 用局部 +Z 的行为 | Rewrite 为 +X 待确认草稿（§8.1.2 明确禁止沿用） | AT-15 断言 |
| 从浮点变换反推覆盖原生 DH（30710f8 前行为） | 删除；原生 DH 权威只读往返 | 回归测试固定 |
| `sdurws_robotmodelbuilder` 等旧目标 | 不作依赖；阶段 B 验收后退出构建与安装包 | 安装包审计 |

## 退出条件

- MDL-01～14、AT-01、AT-15～17 及 AT-18 阶段 B 子链路通过（阶段 B 门禁，总纲 §8.2）。
- 任意编译失败不产生修订；所有对象引用使用 objectId；往返夹具通过 `validate-schemas.ps1`；错误码已登记入 diagnostics 目录。
- 排序/导入报告/转换判定/估算值/序列化字节在同输入下确定一致。
- 证据写入 `out/test-evidence/wp-13/<run-id>/` 并签署：夹具哈希、导入报告、转换判定与残差、编译日志、身份矩阵、GUI 录屏与独立评审签名。
- 旧建模目标不进入 R1 安装包。

## 12. 人周与追踪

| 任务 | 人周 |
| --- | ---: |
| T01 | 0.5～1 |
| T02 | 1.5～2 |
| T03 | 1.5～2 |
| T04 | 1～1.5 |
| T05 | 1～1.5 |
| T06 | 1～1.5 |
| T07 | 0.5～1 |
| T08 | 1～1.5 |
| 合计 | 8～12（总纲 §5.3，保持不变） |

需求追踪：`requirement-traceability.csv` 中 MDL-01～14 主实现＝WP-13（ARC/CON/NFR 相关条目为支持工作包）。

## 任务卡索引

- [WP-13-T01 模型类型与失败夹具](../agent-tasks/WP-13-T01-model-fixtures.md)
- [WP-13-T02 领域编辑器](../agent-tasks/WP-13-T02-domain-editor.md)
- [WP-13-T03 URDF 导入](../agent-tasks/WP-13-T03-urdf-import.md)
- [WP-13-T04 DH 转换](../agent-tasks/WP-13-T04-dh-conversion.md)
- [WP-13-T05 物性与工具](../agent-tasks/WP-13-T05-material-tool.md)
- [WP-13-T06 运行时编译](../agent-tasks/WP-13-T06-runtime-compile.md)
- [WP-13-T07 单机械臂与身份回归](../agent-tasks/WP-13-T07-identity-regression.md)
- [WP-13-T08 插件与 GUI](../agent-tasks/WP-13-T08-modeling-ui.md)
