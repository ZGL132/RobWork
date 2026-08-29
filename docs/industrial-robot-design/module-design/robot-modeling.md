# 机械臂建模模块详细方案（robot-modeling）

- 方案版本：v0.3；需求基线：v0.8；架构检查点：`IRD-D2-20260829`；治理状态：Proposed（IRD-D10-20260829 联合评审通过，待签署）
- 负责 WP：WP-13；阶段/发布：阶段 B / R1；任务卡：`agent-tasks/WP-13-T01～T08`
- 架构契约：`architecture/canonical-kinematics.md`（最高权威）、`architecture/domain-model.md`、`architecture/persistence-schema.md`、`architecture/public-interfaces.md`、`architecture/symbol-registry.md`
- 代码前置：WP-06（编译端口）、WP-10（EditDraft/公共组件）、WP-11（安全读取）；WP-04 命令端口经公共头合法可用；构建/门禁入口 WP-01
- 需求锚点：§7.1～7.3、§8.1（含 §8.1.1/§8.1.2）、§15.3；场景 AT-01、AT-15～18

## 1. 模块职责

`RobotDesign`（SYM-DOM-002）、`JointDefinition`（SYM-DOM-003）、`ToolDefinition`（SYM-DOM-004）、`EnvironmentModel`（SYM-DOM-005）的唯一编辑与导入所有者：模板创建、DH/显式关节权威参数化编辑、`StandardDH`↔`ExplicitJoint` 转换判定（SYM-KIN-001～003）、URDF 导入语义映射、物性解析估算与权威覆盖、工具/TCP/环境编辑；应用修改时经 WP-06 端口原子编译运行时工件（MDL-06），一切应用经 WP-04 领域命令产生修订。**本模块输出为 `RobotDesign`＋运行时编译请求，不拥有 `CompiledCandidateArtifact`（归 WP-20；symbol-registry §4.5 两者不得混用）**。变换链、q-zero、轴与四元数规则以 canonical-kinematics §1～§8 为准；DH↔显式转换语义与五状态判定以需求 §7.3.2/§8.1.1 为准（引用，不复述）。非目标：FK/IK 计算、碰撞策略、候选优化编译、项目持久化实现、GUI 会话状态权威。

## 2. 目录与构建

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

CMake target：`sdurws_ird_modeling`（计算核心，无 Qt Widgets）、`sdurws_ird_modeling_plugin`（薄插件）、`sdurws_ird_modeling_test`、`sdurws_ird_modeling_contract_test`、`sdurws_ird_modeling_gui_test`。允许依赖：WP-03 core、WP-04 命令/查询公共头、WP-06 runtime（`CanonicalModelCompiler`/`CompiledRobotArtifacts`/`IRuntimeNameResolver`）、WP-09 diagnostics、WP-11 io（URDF/网格安全读取）、Qt Core；GUI 层另加 Qt Widgets 与 WP-10 ui。禁止：RobWork 头进入计算核心（编译一律经 WP-06）、其他业务插件私有头、自行拼接/剥离运行时名称、直写项目目录、第二套 XML/URDF 解析。

## 3. 数据与接口

公共符号只用注册名（§1 所列）；持久化 JSON 以 `schemas/robot-design.schema.json` 与 `schemas/examples/robot-design.example.json` 为准，本文不复述字段。模块私有类型：

| 类型（模块私有） | 字段 | 规则 |
| --- | --- | --- |
| `RobotDesignDraft` | base revision、编辑操作序列、逐项诊断 | 对接 WP-10 `EditDraft`；非法草稿不产生命令、不触发失效 |
| `UrdfImportReport` | Error/Warning/Info 条目、字段路径、源值/采用值、原因、建议动作、被排除分支清单 | §8.1.2 全量可观察；确认记录持久化前禁止 Verified |
| `DhConversionVerdict` | status（§7.3.2 五状态）、逐项残差、规范化方案、算法版本 | `Approximate` 只读；`AnalysisFailed` 是系统诊断，不得伪装成 `NotRepresentable` |
| `SectionEstimate` | 截面类型与参数、mass、COM、inertia（参考系显式） | `ValueProvenance=Estimated`；权威覆盖保留原来源与备注（§7.1） |
| `ModelingCompileRequest` | `RobotDesign` 内容身份、目标工件集 | 调 WP-06 编译端口；结果 `CompiledRobotArtifacts` 全成全败 |

## 4. 调用与状态

编辑/导入时序（固定）：WP-11 安全读取（预算内，禁实体/网络）→ 语义映射（`<origin>`/`<axis>`、continuous/prismatic、分支拓扑）→ 草稿＋导入报告 → 用户确认 → `DomainCommand.validate/buildMutations`（纯函数）→ **预编译**（WP-06 WorkCell＋DWC＋`RuntimeNameMap`，任一失败整体为空）→ 成功才 `IProjectCommandService.apply`（恰好一个新修订）；失败时修订号不变并返回诊断。权威切换与重命名同走命令：`SetKinematicAuthority` 只产生一个新修订、旧修订保留转换证据；`RenameObject` 不改 `objectId`，触发 WP-06 名称表重编译（sliceHash 不变）。错误矩阵（新码待 diagnostics.md 登记）：

| 错误码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| `IRD-MDL-IMPORT-BLOCKED` | URDF 零轴、目标链含不支持关节、多可动分支未选链 | Input | Error | 修正输入或显式选择目标链后重导；不产生修订 |
| `IRD-MDL-CONVERSION-INEXACT` | 通用→DH 判定为 `Approximate`/`NotRepresentable` | Engineering | Warning | 只读查看残差；禁止设为权威（MDL-10） |
| `IRD-MDL-CONVERSION-FAILED` | 转换求解器/资源故障（`AnalysisFailed`） | System | Error | 重试或修复资源；不得改判为不可表达 |
| `IRD-MDL-PROPERTIES-INSUFFICIENT` | 物性/材料缺失或惯量张量非法 | Engineering | Warning | 降级证据等级并显示来源；不伪装精确 |
| `IRD-MDL-TOOL-REF-UNRESOLVED` | 工具/TCP/网格引用不可解析 | Input | Error | 补齐引用或解除绑定后重新应用 |

## 5. 关键实现约定

- **物性解析估算（模块私有冻结，MDL-05）**：连杆局部 z 为截面轴向，材料密度 ρ 均匀，质心＝几何中心，惯量参考点＝质心、参考姿态＝连杆坐标系 C。实心圆（半径 r、长 L）：`m=ρπr²L`，`Izz=mr²/2`，`Ixx=Iyy=m(3r²+L²)/12`。空心圆（外/内半径 ro/ri）：`m=ρπ(ro²−ri²)L`，`Izz=m(ro²+ri²)/2`，`Ixx=Iyy=m[3(ro²+ri²)+L²]/12`。矩形（宽 b 沿 x、高 h 沿 y、长 L）：`m=ρbhL`，`Ixx=m(h²+L²)/12`，`Iyy=m(b²+L²)/12`，`Izz=m(b²+h²)/12`。**口径裁决**：公式以半径表达，持久化截面尺寸为直径制（`schemas/robot-design.schema.json` 的 `section.dimensions`，单位 m），转换固定在本估算器边界（r = d/2），不产生第二套持久化语义。每个估算张量执行正定性与三角不等式校验；本公式表是全产品解析估算的唯一语义源（WP-20 派生重算引用，见 optimization.md §5）。
- DH↔显式转换：DH→显式按统一 schilling 约定 `Rz(theta)·Tz(d)·Tx(a)·Rx(alpha)` 确定性生成（§8.1.1）；DH `offset` 只参与生成 q=0 的 `OriginPose`（canonical-kinematics §3）；显式→DH 先可表达性分析（五状态）、再预览、后切换；`Exact/ExactNonUnique` 须满足 §15.3 转换容差（Zero/Home/边界/固定种子 100 姿态 FK＋世界轴线＋附属 Frame）；反算不承诺逐项恢复 DH 数值。`ExactNonUnique` 连杆坐标系规范化规则初版冻结为"优先取与前一关节轴重合或平行、且使 a≥0 的方案"（ADR 待签署）。
- URDF 边界全文引用 §8.1.2：缺失 `<axis>` → 局部 +X＋`Defaulted` 待确认草稿；零/非法轴仅生成报告并阻止新修订；`continuous` 保留语义并要求用户确认工程工作范围；planar/floating/Mimic/闭环在目标链上阻止成模（不得转 FixedFrame 或绕过选择），在被排除分支完整报告；URDF→DH 必须走可表达性验证，自动投影不具权威性。解析预算、DOCTYPE/外部实体/网络禁用由 WP-11 承担，本模块不重复解析不可信文件。
- 编辑器与命令：`ExplicitJoint` 下 Axis 为一等可编辑字段、`StandardDH` 下只读派生（MDL-09）；修改权威轴线＝设计变更，下游按切片失效（MDL-09/CON-05）；编辑走 `EditDraft`，应用走命令，undo/redo 由 WP-04 承担；多可动分支仅保留导入证据，不进入首版计算模型（T07）。
- 确定性：同输入下导入报告条目序、转换判定、估算值与序列化字节一致；四元数符号规范化复用 WP-03 core（canonical-kinematics §6）；RobWork 指针只存在于 WP-06 builder 内，由创建线程释放。

## 6. 测试与证据

| 测试文件 | 覆盖 |
| --- | --- |
| ModelFixtureTest | T01 黄金输入：DH/显式/URDF 任意轴、缺失/零/非单位轴、4/7 轴边界、不支持拓扑（失败断言先行） |
| DomainEditorTest | 草稿校验、唯一 robotId、目标主链、关节限制、来源保留；非法草稿零修订 |
| UrdfImportTest | §8.1.2 全规则＋AT-15/AT-17；导入报告逐项可观察 |
| DhConversionTest | 四类黄金模型判定＋`AnalysisFailed` 故障注入；往返满足 §15.3 转换容差（AT-16） |
| MaterialToolTest | 三类截面解析算例（§15.3 动力学相对容差）、权威覆盖保留原来源、工具 TCP 不复制几何（MDL-13） |
| RuntimeCompileTest | MDL-06/14：双编译 failpoint 全败零修订、名称映射双向一致、交叉校验清单 |
| IdentityRegressionTest | 复制/导入/删除/重命名/目标链切换、objectId 稳定、AT-18 阶段 B 子链路 |
| ModelingGuiTest | 薄插件：错误定位、应用确认、未应用草稿不失效（QT_QPA_PLATFORM=windows） |

往返夹具先过 `powershell -NoProfile -ExecutionPolicy Bypass -File .\schemas\validate-schemas.ps1`（`testdata/modeling/` 样本与 `robot-design.example.json` 同构）。证据写入 `out/test-evidence/wp-13/<run-id>/`：夹具哈希、导入报告、转换判定与残差、编译日志、身份矩阵、GUI 录屏与独立评审签名。验证命令（双形式，仓库根执行）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_modeling(_contract)?_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_modeling_gui_test$'
```

原生回退：

```powershell
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_modeling_test sdurws_ird_modeling_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_modeling(_contract)?_test$"
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_modeling_gui_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_modeling_gui_test$"
```

GUI 约束：Visual Studio x64 环境设置 `$env:QT_QPA_PLATFORM='windows'`，一次只启动一个 GUI 测试可执行文件。

## 7. 迁移与删除表

| 旧资产 | 处置（requirements §13） | 门禁 |
| --- | --- | --- |
| `JointTransformSpec`（RPY+Pos+隐式 Z 轴）建模链路 | Rewrite 为 `JointDefinition`＋显式 Axis | §13.3 消除项；三入口黄金模型通过 |
| 旧 URDF 导入缺失 axis 用局部 +Z 的行为 | Rewrite 为 +X 待确认草稿（§8.1.2 明确禁止沿用） | AT-15 断言 |
| 从浮点变换反推覆盖原生 DH（30710f8 前行为） | 删除；原生 DH 权威只读往返 | 回归测试固定 |
| `sdurws_robotmodelbuilder` 等旧目标 | 不作依赖；阶段 B 验收后退出构建与安装包 | 安装包审计 |
