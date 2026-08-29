# WP-18 传动映射实施计划

> 阶段/发布：阶段 C / R1；方案对齐 `module-design/drivetrain.md` v0.3（本模块唯一权威，本文只做实施深化，不复述其冻结语义）；架构检查点 `IRD-D2-20260829`；需求基线 v0.7。
> 共享计算包：位于 `evaluation/drivetrain/`，不提供独立业务插件或 Widget；实现者、独立验证者与独立评审者必须是不同执行上下文（总纲 §4.1）。WP-17-T06 侧联契约测试落在本包测试目标。

**需求与契约：** DYN-04、SEL-05、AT-07/08 传动映射断言；架构契约与模块方案清单见 §2。
**拥有目录：** `industrialrobot/evaluation/drivetrain/` 及其测试（文件树见 §3）。
**输入/输出：** 输入＝关节侧 `DynamicResult` 只读视图＋每轴 `DriveTrainDesign`＋目录效率证据（可选）；输出＝每轴 `MotorSideOperatingPoint`（工作点/反射惯量/惯量比/能量分项/工作制统计）＋假设清单与诊断（见 §4）。

## 1. 目标与非目标

**目标：** 交付唯一 `DriveTrainMappingEvaluator`（SYM-EVL-001，ADR-004 所有权裁决）：把关节侧 `DynamicResult` 按每轴 `DriveTrainDesign` 映射为电机侧工作点（`MotorSideOperatingPoint`）、反射惯量、能量分项与工作制统计，供选型（WP-19）与优化（WP-21）内层在快照内调用，消除效率、惯量与摩擦的重复实现（DYN-04/SEL-05）。§8.5 冻结契约（速比正方向与单位、双向效率分别定义、折算公式版本化、摩擦归属唯一、峰值窗、RMS 周期、四象限声明、输出量类型化、能量分项与假设标注）是语义上限；本包只冻结实现形态与模块私有默认值。
- 目标交付：`sdurws_ird_drivetrain` 及其模型/契约测试、多速比正/反向效率与反射惯量黄金数据、三消费方一致性记录。
- 完成定义：动力学/选型/优化三调用方同输入同输出；改变候选 `DriveTrainDesign` 不改变关节侧 `DynamicResult`；仓库内无第二套映射/效率/惯量实现。

**非目标：** 关节侧动力学（WP-17）、目录解析与筛选规则（WP-11/19）、直线传动映射（SEL-09 P1，范围外诊断即终态）、优化策略（WP-21）。

## 2. 需求、契约与发布切片

- 需求：DYN-04、SEL-05；§8.5 映射契约冻结清单（权威，本文不复述）、§9.3（能耗口径裁决）、§15.1（传动映射黄金数据）、§15.3（动力包络行）、§7.2（`DriveTrainDesign`）；AT-07/AT-08 的传动映射断言。
- 架构契约：`architecture/domain-model.md` §4（SI/类型化量纲）、`architecture/public-interfaces.md` §7～§8（所有权汇总与值对象，本文不复制）、`architecture/symbol-registry.md` SYM-EVL-001、ADR-004、`architecture/testing-contract.md`。
- 发布切片：阶段 C 形成 R1；直线传动（SEL-09 P1）只登记 Schema 占位不实现。

## 3. 文件所有权与 CMake 目标

拥有目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/evaluation/drivetrain/`，子目录 `include/sdurws/ird/drivetrain/`（DriveTrainMappingEvaluator.hpp、MotorSideOperatingPoint.hpp、DriveTrainEnergySplit.hpp、DrivetrainDiagnostics.hpp）、`src/`（DriveTrainMappingEvaluator.cpp、RotaryMappingCore.cpp、EfficiencyModel.cpp、ReflectedInertia.cpp、EnergySplitter.cpp、DutyCycleWindow.cpp）、`test/`（MappingSemanticsTest.cpp、RotaryMappingTest.cpp、EnergyBoundariesTest.cpp、DutyCycleTest.cpp、SharedEvaluatorTest.cpp、DrivetrainContractTest.cpp）、`testdata/drivetrain/{efficiency,inertia,energy,golden}/`、`evidence/WP-18/`。文件树以模块详设 §2 为权威。

CMake 目标：`sdurws_ird_drivetrain`、`sdurws_ird_drivetrain_test`、`sdurws_ird_drivetrain_contract_test`。允许依赖：WP-03 core、C++ 标准库、Qt Core；契约引用（不链接实现）：WP-17 `DynamicResult` 公共类型（由调用方注入只读视图）。禁止：Qt Widgets、RobWork 运行时对象、业务插件头、第二套映射/效率/惯量实现（静态扫描，ADR-004）、直读 UI 会话态。

## 4. 输入/输出与数据流

- 输入：关节侧 `DynamicResult` 只读视图＋每轴 `DriveTrainDesign`（减速比、效率假设、传动侧惯量，字段语义以 §7.2 为准，只读消费）＋目录效率证据（可选，经调用方传入）。本模块是共享计算服务而非独立 `IEngineeringEvaluator` 入口，由 WP-19/21 评估器在快照内调用。
- 输出：每轴 `MotorSideOperatingPoint`——转矩/转速/功率序列（τ_m、ω_m＝i·ω_j、P_m，方向相关效率；类型化 N·m/rad/s/W，直线传动 N/m/s/W 属 P1）、峰值/RMS 需求（与 DYN-03/§15.3 动力包络同口径）、反射惯量 J_ref（关节侧）与 J_load,motor（电机侧，公式带版本号）、惯量比 J_load,motor/J_rotor、能量分项（W_motor+、W_motor−、W_regen、驱动器电能占位，逐项标注假设）、工作制统计（四象限时间占比、连续/峰值区间占比，假设进证据，默认无回馈）＋假设清单随证据输出。
- 调用流（模块详设 §4）：调用方校验（i 有限且 >0，否则 `IRD-DTM-RATIO-INVALID`；目标轴为旋转传动，否则 `IRD-DTM-ROTARY-ONLY`）→ 方向判定 sign(P_j)，|P_j|≤ε_P 按正向耗散 → 序列映射 → 反射惯量与惯量比 → 峰值窗/RMS/工作制统计 → 能量分项（按证据存在性）→ 输出＋诊断。
- 错误面（模块详设 §4 表，此处只列码）：`IRD-DTM-RATIO-INVALID`（Input）、`IRD-DTM-ROTARY-ONLY`（Engineering，不套用旋转公式、不静默降级）、`IRD-DTM-EFFICIENCY-MISSING`（Engineering，电机侧机械功不可输出）、`IRD-DTM-REVERSE-EFFICIENCY-MISSING`（Warning，反向流分项 DataInsufficient）、`IRD-DTM-INERTIA-INVALID`（Warning，惯量比 NotEvaluated 并列举缺口）。

## 5. 冻结实现约定（引用模块详设 §5，实施不得偏离）

1. **速比与方向**：旋转传动无量纲比 i>1，正方向与单位遵循 §8.5 冻结行；直线传动属 SEL-09 P1，只登记 Schema 占位。
2. **效率模型（首版冻结：方向相关常数）**：η⁺（电机→负载）与 η⁻（负载→电机）分别取自目录减速器效率证据或 `DriveTrainDesign` 效率假设；正向流 τ_m＝τ_j/(i·η⁺)、P_m＝P_j/η⁺；反向流 τ_m＝η⁻·τ_j/i、P_m＝η⁻·P_j；单一效率值只允许充当 η⁺。方向判定用 sign(P_j(t))，|P_j|≤ε_P（默认 1e-6 W，模块私有可评审）按正向耗散处理。
3. **反射惯量**：J_ref,joint＝(J_rotor＋J_coupling＋J_reducer,eq)·i²（动能等价推导：电机侧惯量折算到关节侧乘 i²）；电机侧等效负载惯量 J_load,motor＝J_j/i²，J_j 由调用方显式传入并声明取值规则（模块私有默认：任务循环内构型相关等效惯量的最大值保守口径，可评审），来源与取值点进证据；公式带 `driveTrainInertiaFormulaVersion`，变更即失效下游切片。
4. **能量边界（§9.3 裁决执行）**：关节侧正机械功 W+ 直接取自 `DynamicResult`（默认"能耗"口径）；电机侧机械功 W_motor± 仅当存在效率证据时输出；W_regen 仅当用户显式声明回馈策略时输出，默认"无回馈、制动能量按耗散计"；驱动器输入电能首版不输出（目录无驱动器效率证据），以占位诊断表达而非空字段；各分项标注假设，不得与关节侧机械能混称。
5. **工作制窗口**：duty 统计窗口＝完整任务循环（含驻留），与 DYN-03 RMS 同周期；峰值窗＝器件目录声明峰值时间能力，无目录数据 1 s；四象限/制动/保持工况显式声明为假设并进证据。
6. **摩擦归属**：只计传动效率与减速器摩擦（含于效率常数），不复计关节摩擦（WP-17 RNE 侧已计入）。
7. **候选无关性**：候选传动参数只进入输入与输出，不回写、不改变关节侧 `DynamicResult`；WP-17-T06 契约测试断言之。

## 6. 任务依赖 DAG

```text
WP-18-T01 → WP-18-T02 → WP-18-T03
WP-18-T02 → WP-18-T04
WP-18-T02、WP-18-T03、WP-18-T04 → WP-18-T05
WP-18-T05 与 WP-17-T06 侧联（DrivetrainContractTest 承载双方验收）
```

## 7. 逐任务深化

### WP-18-T01 传动映射语义冻结
- 代码范围：`include/sdurws/ird/drivetrain/DriveTrainMappingEvaluator.hpp`（服务接口与输入校验）、`include/sdurws/ird/drivetrain/DrivetrainDiagnostics.hpp`；`test/MappingSemanticsTest.cpp`。
- 前置任务：无（包内首任务；外部前置 WP-03、WP-17 由总纲 §5.3 规定，`DynamicResult` 为契约引用）。
- 输出工件：语义冻结——速比正方向与单位（无量纲 i>1）、η⁺/η⁻ 分别定义（单一效率值只允许充当 η⁺）、`driveTrainInertiaFormulaVersion`、ε_P＝1e-6 W、五个错误码与类别/严重度、假设清单 Schema。
- 验收断言：`MappingSemanticsTest`（模块详设 §6）——§5.1～§5.3 常量/公式版本、非法速比诊断（`IRD-DTM-RATIO-INVALID`）、语义变更→下游切片失效。

### WP-18-T02 旋转传动映射
- 代码范围：`src/RotaryMappingCore.cpp`、`src/EfficiencyModel.cpp`、`src/ReflectedInertia.cpp`；`test/RotaryMappingTest.cpp`；`testdata/drivetrain/{efficiency,inertia,golden}/`。
- 前置任务：WP-18-T01。
- 输出工件：方向相关效率模型（正向 τ_m＝τ_j/(i·η⁺)、P_m＝P_j/η⁺；反向 τ_m＝η⁻·τ_j/i、P_m＝η⁻·P_j）、反射惯量计算（J_ref 公式与 J_load,motor＝J_j/i²，J_j 调用方传入＋保守默认口径）、多速比黄金数据夹具（WP-02 登记）。
- 验收断言：`RotaryMappingTest`（模块详设 §6）——多速比正/反向映射黄金数据（§15.1）；移动关节 → `IRD-DTM-ROTARY-ONLY` 不套用旋转公式；η⁻ 缺失而循环含反向功率流 → `IRD-DTM-REVERSE-EFFICIENCY-MISSING`，反向流分项 DataInsufficient、禁止以 η⁺ 反向套用；转子/减速器等效惯量缺失 → `IRD-DTM-INERTIA-INVALID`，惯量比 NotEvaluated。

### WP-18-T03 能量边界分离
- 代码范围：`include/sdurws/ird/drivetrain/DriveTrainEnergySplit.hpp`；`src/EnergySplitter.cpp`；`test/EnergyBoundariesTest.cpp`；`testdata/drivetrain/energy/`。
- 前置任务：WP-18-T02。
- 输出工件：能量分项器（W+ 取自 `DynamicResult`；W_motor± 按证据存在性；W_regen 按显式声明；驱动器电能占位诊断）与逐项假设标注。
- 验收断言：`EnergyBoundariesTest`（模块详设 §6）——双向效率、η⁻ 缺失→DataInsufficient 分项、W+ 与电机侧分项不混称、默认回馈假设标注（"无回馈、制动能量按耗散计"）；正向效率证据缺失 → `IRD-DTM-EFFICIENCY-MISSING`。

### WP-18-T04 峰值与工作制评估
- 代码范围：`src/DutyCycleWindow.cpp`；`test/DutyCycleTest.cpp`。
- 前置任务：WP-18-T02。
- 输出工件：峰值窗统计（窗长＝目录峰值能力，缺省 1 s；滑窗均值峰值＋瞬时峰值＋所在段）、完整任务循环 RMS（含驻留）、四象限/连续/峰值区间占比（显式假设进证据）。
- 验收断言：`DutyCycleTest`（模块详设 §6）——峰值窗（目录值/缺省 1 s）、完整循环 RMS（含驻留，与 DYN-03 同周期）、四象限占比、摩擦不双计（§15.1 黄金断言）。

### WP-18-T05 共享传动评估器
- 代码范围：`src/DriveTrainMappingEvaluator.cpp`（唯一入口装配）；`include/sdurws/ird/drivetrain/MotorSideOperatingPoint.hpp`；`test/SharedEvaluatorTest.cpp`、`test/DrivetrainContractTest.cpp`。
- 前置任务：WP-18-T02、WP-18-T03、WP-18-T04。
- 输出工件：完整 `MotorSideOperatingPoint` 输出与假设清单随证据输出；三消费方（动力学/选型/优化）调用一致性记录；候选无关性契约测试（承载 WP-17-T06 侧联验收）。
- 验收断言：`SharedEvaluatorTest`（模块详设 §6）——动力学/选型/优化三调用方同输入同输出；无第二实现（静态扫描零命中，ADR-004）。`DrivetrainContractTest`（模块详设 §6，承载 WP-17-T06）——改变 `DriveTrainDesign` 不改变关节侧 `DynamicResult`。

## 8. 测试矩阵（模块详设 §6 为断言权威）

| 测试目标/文件 | 断言要点 | 覆盖需求 |
| --- | --- | --- |
| `sdurws_ird_drivetrain_test` / MappingSemanticsTest.cpp | 常量/公式版本、非法速比诊断、语义变更→下游失效 | DYN-04 |
| 同上 / RotaryMappingTest.cpp | 多速比正/反向黄金数据；移动关节范围外 | DYN-04、SEL-05、§15.1 |
| 同上 / EnergyBoundariesTest.cpp | 双向效率、DataInsufficient 分项、不混称、回馈假设标注 | §9.3 |
| 同上 / DutyCycleTest.cpp | 峰值窗、含驻留 RMS、四象限占比、摩擦不双计 | DYN-03/04、§15.3 |
| 同上 / SharedEvaluatorTest.cpp | 三调用方一致、无第二实现（静态扫描零命中） | ADR-004 |
| `sdurws_ird_drivetrain_contract_test` / DrivetrainContractTest.cpp | 候选无关性（WP-17-T06 侧联） | DYN-04、AT-07/08 |

本包无 Widget；全部测试为 `QCoreApplication` 或无 Qt 入口的模型测试，不适用 GUI 平台规则。

## 验证命令（双形式，仓库根执行）

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_drivetrain(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_drivetrain_test
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_drivetrain_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_drivetrain(_contract)?_test$"
```

## 10. 独立验证与独立评审

- 独立验证（黑盒）：多速比正/反向映射黄金数据独立复算、能量分项对账（关节侧 W+ 与电机侧分项分离）、三消费方一致性、摩擦不双计断言。
- 独立评审：由驱动工程师与独立测试人员双评审签署——双向效率模型、反射惯量公式与版本、能量边界（§9.3 裁决）、候选无关性。
- 证据写入 `evidence/WP-18/`：黄金数据（多速比正/反向效率与反射惯量）版本/哈希、假设清单、三消费方一致性记录、双评审签署。

## 11. 迁移与删除（requirements §13）

| 旧资产 | 处置 | 门禁 |
| --- | --- | --- |
| 旧插件内传动/效率/惯量计算 | 只保留黄金对照后删除（Rewrite） | 共享 evaluator 验收＋静态扫描零命中 |
| 旧功率/摩擦重复实现 | 删除 | 摩擦不双计黄金断言通过 |
| 直线传动旧尝试/草稿 | 删除（SEL-09 P1 另立） | "范围外"诊断即终态 |

## 退出条件

- DYN-04、SEL-05 及 AT-07/AT-08 的传动映射断言通过；`DriveTrainMappingEvaluator` 为唯一映射实现（静态扫描零命中）。
- 候选无关性成立：改变 `DriveTrainDesign` 不改变关节侧 `DynamicResult`（DrivetrainContractTest 通过，WP-17-T06 侧联验收签署）。
- 能量分项严格按 §9.3 裁决输出并标注假设；摩擦不双计。
- §11 删除清单执行完毕，重复效率/惯量/摩擦实现退出构建。

## 13. 人周（总纲 §5.3：4～6 人周，含实现/测试/评审/修正）

| 任务 | 人周 |
| --- | ---: |
| WP-18-T01 | 0.5～1 |
| WP-18-T02 | 1～1.5 |
| WP-18-T03 | 0.5～1 |
| WP-18-T04 | 0.5～1 |
| WP-18-T05 | 1～1.5 |

## 任务卡索引

- [WP-18-T01 传动映射语义冻结](../agent-tasks/WP-18-T01-mapping-semantics.md)
- [WP-18-T02 旋转传动映射](../agent-tasks/WP-18-T02-rotary-mapping.md)
- [WP-18-T03 能量边界分离](../agent-tasks/WP-18-T03-energy-boundaries.md)
- [WP-18-T04 峰值与工作制评估](../agent-tasks/WP-18-T04-duty-cycle.md)
- [WP-18-T05 共享传动评估器](../agent-tasks/WP-18-T05-shared-evaluator.md)
