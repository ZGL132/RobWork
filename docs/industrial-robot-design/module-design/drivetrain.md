# 传动映射模块详细方案（drivetrain）

- 方案版本：v0.3；需求基线：v0.8；架构检查点：`IRD-D2-20260829`；治理状态：Proposed（IRD-D10-20260829 联合评审通过，待签署）
- 负责 WP：WP-18；阶段/发布：阶段 C / R1；任务卡：agent-tasks/WP-18-T01～T05（WP-17-T06 侧联契约测试落在本模块测试目标）
- 架构契约：`architecture/domain-model.md` §4（SI/类型化量纲）、`architecture/public-interfaces.md` §7～§8、`architecture/symbol-registry.md` SYM-EVL-001、ADR-004、`architecture/testing-contract.md`
- 需求锚点：requirements §8.5 `DriveTrainMappingEvaluator` 映射契约（冻结清单）与 DYN-04、§8.6 SEL-05、§9.3（能耗口径裁决）、§15.1（传动映射黄金数据）、§15.3（动力包络行）、§7.2（`DriveTrainDesign`）
- 代码前置：WP-03、17（总纲 §5.3）；共享计算包，不提供独立业务插件或 Widget

## 1. 模块职责

唯一 `DriveTrainMappingEvaluator`（SYM-EVL-001，ADR-004 所有权裁决）：把关节侧 `DynamicResult` 按每轴 `DriveTrainDesign` 映射为电机侧工作点、反射惯量、能量分项与工作制统计，供选型（WP-19）与优化（WP-21）内层复用，消除效率、惯量与摩擦的重复实现（DYN-04/SEL-05）。§8.5 冻结契约（速比正方向与单位、双向效率分别定义、折算公式版本化、摩擦归属唯一、峰值窗、RMS 周期、四象限声明、输出量类型化、能量分项与假设标注）是语义上限，本文只冻结实现形态与模块私有默认值，不复制契约正文。非目标：关节侧动力学（WP-17）、目录解析与筛选规则（WP-11/19）、直线传动映射（SEL-09 P1，范围外诊断即终态）、优化策略（WP-21）。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/evaluation/drivetrain/
  include/sdurws/ird/drivetrain/
    DriveTrainMappingEvaluator.hpp   MotorSideOperatingPoint.hpp
    DriveTrainEnergySplit.hpp   DrivetrainDiagnostics.hpp
  src/DriveTrainMappingEvaluator.cpp   RotaryMappingCore.cpp
      EfficiencyModel.cpp   ReflectedInertia.cpp   EnergySplitter.cpp
      DutyCycleWindow.cpp
  test/MappingSemanticsTest.cpp   RotaryMappingTest.cpp   EnergyBoundariesTest.cpp
      DutyCycleTest.cpp   SharedEvaluatorTest.cpp   DrivetrainContractTest.cpp
  testdata/drivetrain/{efficiency,inertia,energy,golden}/
  # 证据 → out/test-evidence/wp-18/<run-id>/（AGENTS §3，不入源码树）
```

CMake target：`sdurws_ird_drivetrain`、`sdurws_ird_drivetrain_test`、`sdurws_ird_drivetrain_contract_test`。允许依赖：WP-03 core、C++ 标准库、Qt Core；契约引用（不链接实现）：WP-17 `DynamicResult` 公共类型（由调用方注入只读视图）。禁止：Qt Widgets、RobWork 运行时对象、业务插件头、第二套映射/效率/惯量实现（静态扫描，ADR-004）、直读 UI 会话态。

## 3. 数据与接口

`DriveTrainMappingEvaluator` 是共享计算服务而非独立 `IEngineeringEvaluator` 入口：由 WP-19/21 评估器在快照内调用。输入＝关节侧 `DynamicResult` 只读视图＋每轴 `DriveTrainDesign`（减速比、效率假设、传动侧惯量）＋目录效率证据（可选，经调用方传入）。输出＝每轴 `MotorSideOperatingPoint`：

| 输出字段 | 内容 | 规则 |
| --- | --- | --- |
| 转矩/转速/功率序列 | τ_m、ω_m＝i·ω_j、P_m（方向相关效率） | 类型化 N·m/rad/s/W（§8.5 量纲行；直线传动 N/m/s/W 属 P1） |
| 峰值/RMS 需求 | 峰值窗滑窗均值峰值；完整任务循环 RMS | 与 DYN-03/§15.3 动力包络同口径 |
| 反射惯量 | J_ref（关节侧）与 J_load,motor（电机侧） | §5.3 冻结公式，带版本号 |
| 惯量比 | J_load,motor/J_rotor | 供 SEL-05 可配置工程规则消费 |
| 能量分项 | W_motor+、W_motor−、W_regen、驱动器电能占位 | §5.4 边界；逐项标注所依赖假设 |
| 工作制统计 | 四象限时间占比、连续/峰值区间占比 | 声明假设进证据；默认无回馈 |

`DriveTrainDesign` 字段语义以 §7.2 为准（每轴现有或候选传动、减速比/传动参数、效率假设、传动惯量）；本模块只读消费，修改只能经项目命令（应用选型，WP-19）。

## 4. 调用与状态

```text
调用方（WP-19/21 评估器，快照内）
  → 校验：i 有限且 >0（否则 IRD-DTM-RATIO-INVALID）；目标轴为旋转传动（否则 IRD-DTM-ROTARY-ONLY）
  → 方向判定 sign(P_j)，|P_j|≤ε_P 按正向耗散 → 转矩/转速/功率序列映射
  → 反射惯量与惯量比 → 峰值窗/RMS/工作制统计 → 能量分项（按证据存在性）
  → MotorSideOperatingPoint[]＋诊断（假设清单随证据输出）
```

| 错误码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| `IRD-DTM-RATIO-INVALID` | 减速比缺失、非有限或 ≤0 | Input | Error | 修正 `DriveTrainDesign` 后重映射 |
| `IRD-DTM-ROTARY-ONLY` | 目标轴为移动关节/直线传动（SEL-09 首版范围外） | Engineering | Error | 该轴输出明确"范围外"，不套用旋转公式、不静默降级 |
| `IRD-DTM-EFFICIENCY-MISSING` | 正向效率证据缺失 | Engineering | Warning | 电机侧机械功分项按 DataInsufficient 输出；补目录/设计效率假设 |
| `IRD-DTM-REVERSE-EFFICIENCY-MISSING` | 反向效率缺失而循环含反向功率流 | Engineering | Warning | 反向流分项按 DataInsufficient 输出，禁止以 η⁺ 反向套用 |
| `IRD-DTM-INERTIA-INVALID` | 转子/减速器等效惯量非有限或违反物理约束 | Input | Error | 修正惯量数据后重映射；缺失情形走证据缺口（NotEvaluated＋gaps），不用本码 |

## 5. 关键实现约定

1. **速比与方向**：旋转传动无量纲比 i>1，正方向与单位遵循 §8.5 冻结行；直线传动（导程/等效折算）属 SEL-09 P1，本版只登记 Schema 占位不实现。
2. **效率模型（首版冻结：方向相关常数）**：η⁺（电机→负载）与 η⁻（负载→电机）分别取自目录减速器效率证据或 `DriveTrainDesign` 效率假设；正向流 τ_m＝τ_j/(i·η⁺)、P_m＝P_j/η⁺；反向流 τ_m＝η⁻·τ_j/i、P_m＝η⁻·P_j；单一效率值只允许充当 η⁺。方向判定用 sign(P_j(t))，|P_j|≤ε_P（默认 1e-6 W，模块私有可评审）按正向耗散处理。
3. **反射惯量（模块私有冻结＋推导）**：J_ref,joint＝(J_rotor＋J_coupling＋J_reducer,eq)·i²。推导：动能等价 ½J_m·ω_m²＝½J_m·(i·ω_j)²＝½(J_m·i²)·ω_j²，电机侧惯量折算到关节侧乘 i²；电机侧等效负载惯量 J_load,motor＝J_j/i²，其中关节侧等效负载惯量 J_j 由调用方显式传入并声明取值规则（模块私有默认：任务循环内构型相关等效惯量的最大值保守口径，可评审），来源与取值点进证据。公式带 `driveTrainInertiaFormulaVersion`，变更即失效下游切片。
4. **能量边界（§9.3 裁决执行）**：关节侧正机械功 W+ 直接取自 `DynamicResult`（默认"能耗"口径）；电机侧机械功 W_motor± 仅当存在效率证据时输出；可回馈能量 W_regen 仅当用户显式声明回馈策略时输出，默认"无回馈、制动能量按耗散计"（§8.5）；驱动器输入电能首版不输出（目录无驱动器效率证据），以占位诊断表达而非空字段。各分项在结果与报告中标注效率与回馈假设，不得与关节侧机械能混称。
5. **工作制窗口（冻结）**：duty cycle 统计窗口＝完整任务循环（含驻留），与 DYN-03 RMS 同周期；峰值窗＝器件目录声明峰值时间能力，无目录数据 1 s；四象限/制动/保持工况显式声明为假设并进证据。
6. **摩擦归属**：本模块只计传动效率与减速器摩擦（含于效率常数），不复计关节摩擦（WP-17 RNE 侧已计入）；黄金数据含摩擦不重复计入断言（§15.1）。
7. **候选无关性**：候选传动参数只进入本模块输入与输出，不回写、不改变关节侧 `DynamicResult`；WP-17-T06 契约测试（`sdurws_ird_drivetrain_contract_test`）断言之。

## 6. 测试与证据

| 测试 | 断言要点 |
| --- | --- |
| MappingSemanticsTest | §5.1～§5.3 常量/公式版本、非法速比诊断、语义变更→下游失效 |
| RotaryMappingTest | 多速比正/反向映射黄金数据（§15.1）；移动关节→`IRD-DTM-ROTARY-ONLY` 不套用旋转公式 |
| EnergyBoundariesTest | 双向效率、η⁻ 缺失→DataInsufficient 分项、W+ 与电机侧分项不混称、默认回馈假设标注 |
| DutyCycleTest | 峰值窗（目录值/缺省 1 s）、完整循环 RMS（含驻留）、四象限占比、摩擦不双计 |
| SharedEvaluatorTest | 动力学/选型/优化三调用方同输入同输出；无第二实现（静态扫描零命中） |
| DrivetrainContractTest | 候选无关性（WP-17-T06）：改变 `DriveTrainDesign` 不改变关节侧 `DynamicResult` |

证据写入 `out/test-evidence/wp-18/<run-id>/`：黄金数据（多速比正/反向效率与反射惯量）版本/哈希、假设清单、三消费方一致性记录、驱动工程师＋独立测试双评审签署。验证命令（双形式，仓库根执行）：

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_drivetrain(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_drivetrain_test
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_drivetrain_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_drivetrain(_contract)?_test$"
```

## 7. 迁移与删除表

| 旧资产 | 处置（requirements §13） | 门禁 |
| --- | --- | --- |
| 旧插件内传动/效率/惯量计算 | 只保留黄金对照后删除（Rewrite） | 共享 evaluator 验收＋静态扫描零命中 |
| 旧功率/摩擦重复实现 | 删除 | 摩擦不双计黄金断言通过 |
| 直线传动旧尝试/草稿 | 删除（SEL-09 P1 另立） | "范围外"诊断即终态 |
