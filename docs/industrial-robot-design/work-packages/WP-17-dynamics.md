# WP-17 动力学实施计划

> 阶段/发布：阶段 C / R1；方案对齐 `module-design/dynamics.md` v0.3（本模块唯一权威，本文只做实施深化，不复述其冻结语义）；架构检查点 `IRD-D2-20260829`；需求基线 v0.8。
> 实现者、独立验证者与独立评审者必须是不同执行上下文（总纲 §4.1）；构建/门禁入口由 WP-01 交付；WP-18 依赖本模块（候选无关性由其消费侧契约测试验证）。

**需求与契约：** DYN-01～08、AT-07、NFR-DEP-05；架构契约与模块方案清单见 §2。
**拥有目录：** `industrialrobot/plugins/dynamics/` 及其测试（文件树见 §3）。
**输入/输出：** 输入＝`RobotDesign`/DWC 工件＋上游 `TrajectoryPlan`＋`LoadCase` 与摩擦假设；输出＝关节侧 `DynamicResult`（类型化广义力/功率/能量/峰值/RMS/仿真状态）＋证据等级（见 §4）。

## 1. 目标与非目标

**目标：** 实现动力学评估器 `DynamicsEvaluator`：以 RobWorkSim 刚体模型＋递归牛顿—欧拉（`rwsim::util::RecursiveNewtonEuler`，DYN-01 规定算法）计算含重力、连杆惯性、末端负载与外力的逆动力学，叠加冻结的黏性＋库仑关节摩擦（DYN-02），沿 `TrajectoryPlan` 输出与候选传动无关的关节侧 `DynamicResult`（规范名，symbol-registry §4.6；`DynamicsResult` 为禁止名称）：类型化广义力、转角/位移、转速、加速度、机械功率、能量、峰值与 RMS 包络、仿真状态（DYN-03/04）；以 RobWorkSim 正动力学执行响应一致性检查与异常检测（DYN-05）；物性/摩擦数据不足时降级证据等级（DYN-06）。
- 目标交付：`sdurws_ird_dynamics` 及其模型/契约测试、语义冻结工件、二连杆/重力矩黄金数据、完整循环积分与正动力学收敛报告。
- 完成定义：DYN-01～06 P0 全部通过；关节侧结果不随候选传动改变（由 WP-18 契约测试断言）。

**非目标：** 传动映射与电机侧量（WP-18）、多负载工况/急停工况包络合并（DYN-07 P1）、曲线联动与回放 UI（DYN-08 P1，归 WP-10/WP-22）、柔性/热/寿命（§19-8）、候选传动比较（WP-19/21）。

## 2. 需求、契约与发布切片

- 需求：DYN-01～08 与 §8.5 `DriveTrainMappingEvaluator` 映射契约（冻结清单，本文不复述）；§7.1/§7.2/§7.4、§9.3（W+ 冻结式）、§15.3（动力学容差、正动力学收敛、动力包络行）、§6.6（证据等级）；AT-07；NFR-DEP-05（RobWorkSim 锁定版本）。
- 架构契约：`architecture/domain-model.md` §4（SI/类型化广义力）、`architecture/public-interfaces.md` §3/§7、`architecture/evaluation-semantics.md` §1～2（合法组合，本文不复制）、`architecture/execution-model.md` §1～3、`architecture/testing-contract.md`。
- 发布切片：阶段 C 形成 R1；不以 DYN-07/08 P1 能力作为退出条件。

## 3. 文件所有权与 CMake 目标

拥有目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/dynamics/`，子目录 `include/sdurws/ird/dynamics/`（DynamicResult.hpp、DynamicsEvaluator.hpp、DynamicsSemantics.hpp、DynamicsDiagnostics.hpp）、`src/`（DynamicsEvaluator.cpp、RneAdapter.cpp、FrictionModel.cpp、PowerEnergyIntegrator.cpp、ForwardDynamicsScenario.cpp、InertiaValidator.cpp、DynamicsJson.cpp）、`test/`（SemanticFreezeTest.cpp、InverseDynamicsTest.cpp、PowerEnergyTest.cpp、ForwardDynamicsTest.cpp、InsufficientDataTest.cpp）、`testdata/dynamics/{two-link,gravity,cycle,failpoints}/`、`out/test-evidence/wp-17/<run-id>/`。文件树以模块详设 §2 为权威。

CMake 目标：`sdurws_ird_dynamics`、`sdurws_ird_dynamics_test`、`sdurws_ird_dynamics_contract_test`。允许依赖：WP-03 core（含类型化广义力包装）、WP-05 evidence（端口头＋结果仓库）、WP-06 runtime（`CompiledRobotArtifacts` 的 DWC 工件）、RobWorkSim 稳定 API（DynamicWorkCell、RigidDevice、`rwsim::util::RecursiveNewtonEuler`、积分器/物理引擎，按 NFR-DEP-05 锁定版本）、Qt Core；契约引用（不链接实现）：WP-16 `TrajectoryPlan`/`ResolvedIkBranchSequence` 公共类型（payload 经 `IResultRepository` 取回）；调度经 WP-08 装配（契约引用）。禁止：Qt Widgets、WP-18 及其后模块头、本地效率/减速比计算（映射归 WP-18）、直读 UI 会话态。

## 4. 输入/输出与数据流

- 输入：`AnalysisSnapshot`（canonical 物理身份、nameMapId、上游 `TrajectoryPlan` 引用、`LoadCase`/摩擦假设字段、DWC 与 RobWorkSim 版本、`dynamicsSemanticsVersion` 与种子，经 `dependencyManifest()` 声明）；`ExternalWrenchBinding`（Wrench6D＋表达坐标系 objectId＋激活工况，随快照冻结）。
- 输出：关节侧 `DynamicResult`（payload 经 `ResultEnvelope.payloadId` 引用，模块提供类型化只读视图；字段语义以 §7.2 为准）＋模块私有类型 `JointSample`（时间戳、q、ω、α、类型化 τ、P_joint、P_f）、`PeakWindowStats`（窗长、滑窗均值峰值、瞬时峰值、所在段与时刻）、`RmsCycleStats`（τ/ω/P 的 RMS 与 T_cycle）、`ForwardDynamicsReport`（控制输入、初始状态、步长 h、积分器、h/h2 残差、发散诊断）。
- 主数据流（模块详设 §4）：snapshot → 校验（TrajectoryPlan 存在且 Current、物性/惯量/摩擦假设完整且有限）→ 沿 plan 记录时间戳采样 → FK 状态 → RNE 刚体力矩 → 叠加冻结摩擦 → 类型化广义力/功率序列 → W+/W−、峰值窗、完整循环 RMS → 正动力学一致性检查 → `DynamicResult`＋证据 → WP-05 接纳。
- 错误面（模块详设 §4 表，此处只列码）：`IRD-DYN-UPSTREAM-MISSING`、`IRD-DYN-INERTIA-INVALID`、`IRD-DYN-PROPERTIES-MISSING`（Warning）、`IRD-DYN-FRICTION-MISSING`（Warning）、`IRD-DYN-STATE-DISCONTINUOUS`、`IRD-DYN-FD-NOT-CONVERGED`、`IRD-DYN-FD-DIVERGED`（System；关节侧结果保留为 Partial 诊断件，运行判 Failed）。

## 5. 冻结语义裁决（引用模块详设 §5，实施不得偏离）

1. **摩擦模型**：τ_j(t)＝τ_RNE(t)＋τ_f(t)，τ_f＝b·ω＋τ_c·s(ω)；零速库仑处理 s(ω)＝ω/ωeps（|ω|≤ωeps）否则 sign(ω)——连续确定性延拓，无独立静摩擦系数、无状态切换；ωeps 默认 1e-4 rad/s（移动关节 1e-4 m/s，模块私有可评审）。DWC 构建时 BodyInfo 摩擦字段置零，关节摩擦只在本模块计入一次（WP-18 不得复计）。
2. **坐标系**：重力在基座坐标（RNE 语义）；负载质量/质心/惯量在 TCP/工具坐标系（惯量"参考点＋参考姿态＋参考坐标系"三要素随 §7.1 保存）；外力按 `ExternalWrenchBinding` 声明坐标系表达、适配时按当前状态 FK 转基座；禁止依赖"当前选中坐标系"。
3. **功率与能量**：P_joint＝τ_j·ω_j（正＝执行器向机构输出正机械功）；摩擦耗散 P_f＝τ_f·ω≥0 单列；W+＝∫max(P_joint,0)dt、W−＝∫min(P_joint,0)dt，按记录时间戳梯形积分（§9.3 冻结式），采样与积分规则随结果保存；纯关节侧 `DynamicResult` 不得宣称电能。
4. **峰值与 RMS**：峰值＝峰值窗（窗长＝器件目录峰值时间能力，无目录数据 1 s）内滑窗均值最大值＋瞬时峰值，同时报告窗长与所在轨迹段；RMS 周期＝完整任务循环 T_cycle（含驻留）实际时间积分。
5. **正动力学**：RobWorkSim `DynamicSimulator`＋项目锁定物理引擎，RigidDevice 速度控制模式；控制输入＝plan 关节速度剖面在积分步上的线性插值；初始状态＝轨迹起点 (q0, ω0)；默认步长 h＝1e-3 s；收敛判据按 §15.3——固定 2 s 基准工况分别以 h 与 h/2 各跑一次，末端及全过程最大关节位置差 ≤1e-4 rad、速度差 ≤1e-3 rad/s（移动关节按 m、m/s 同级）。
6. **证据等级**：估算参数最多 `Screening`，用户确认关键物性后 `PreliminaryDesign`，与独立工具/实测对照后 `ExternallyValidated`；不足不得自动提升；报告必须列出运动律与摩擦假设。

## 6. 任务依赖 DAG

```text
WP-17-T01 → WP-17-T02 → WP-17-T03
WP-17-T02 → WP-17-T04
WP-17-T02、WP-17-T03 → WP-17-T05 → WP-17-T06
WP-17-T06 与 WP-18-T01/T02 侧联（契约测试落在 WP-18 测试目标）
```

## 7. 逐任务深化

### WP-17-T01 动力学语义冻结
- 代码范围：`include/sdurws/ird/dynamics/DynamicsSemantics.hpp`；`test/SemanticFreezeTest.cpp`；`testdata/dynamics/` 黄金夹具登记位。
- 前置任务：无（包内首任务；外部前置 WP-06、08、16 由总纲 §5.3 规定）。
- 输出工件（模块详设 §5.7 七项产出物）：① `DynamicsSemantics.hpp`（§5.1～§5.5 常量与公式 ID）＋`dynamicsSemanticsVersion`；② 零速摩擦黄金夹具（斜坡延拓，ωeps＝1e-4 rad/s/1e-4 m/s）；③ 功率符号约定夹具；④ W+ 梯形积分夹具；⑤ 峰值窗/RMS 口径夹具；⑥ 外力坐标系绑定夹具；⑦ FD 控制输入插值与积分器配置夹具。另交二连杆解析与静态重力矩对照数据（WP-02 登记版本/哈希）与动力学/驱动工程师评审签署记录。
- 验收断言：`SemanticFreezeTest`（模块详设 §6）——§5.1～§5.5 常量与版本、零速连续性（|ω|≤ωeps 无符号跳变）、语义变更→依赖切片失效；任一语义变更必须升版本并使依赖切片失效。

### WP-17-T02 逆动力学计算
- 代码范围：`src/RneAdapter.cpp`、`src/FrictionModel.cpp`、`src/InertiaValidator.cpp`；`include/sdurws/ird/dynamics/DynamicResult.hpp`（类型化广义力字段视图）；`test/InverseDynamicsTest.cpp`；`testdata/dynamics/{two-link,gravity}/`。
- 前置任务：WP-17-T01。
- 输出工件：RNE 适配（重力/连杆惯性/末端负载/外力，基座坐标重力）、摩擦叠加实现（含 BodyInfo 摩擦字段置零）、惯量正定性与三角不等式校验器。
- 验收断言：`InverseDynamicsTest`（模块详设 §6）——二连杆解析算例与静态重力矩满足 §15.3 动力学容差（相对 1e-6、近零绝对下限 1e-8 N·m/N）；摩擦不双计；惯量非法 → `IRD-DYN-INERTIA-INVALID`；状态不连续 → `IRD-DYN-STATE-DISCONTINUOUS`。

### WP-17-T03 广义力、功率与能量
- 代码范围：`src/PowerEnergyIntegrator.cpp`、`src/DynamicsJson.cpp`；`test/PowerEnergyTest.cpp`；`testdata/dynamics/cycle/`。
- 前置任务：WP-17-T02。
- 输出工件：`JointSample` 序列、W+/W− 梯形积分器（采样与积分规则随结果保存）、`PeakWindowStats`（滑窗均值峰值＋瞬时峰值＋窗长＋所在段）、`RmsCycleStats`（含驻留完整循环）、JSON 序列化。
- 验收断言：`PowerEnergyTest`（模块详设 §6）——W+ 梯形积分（§9.3 冻结式）、含驻留完整循环 RMS、峰值窗口径与所在段、功率符号约定（P_joint 正＝输出正机械功，P_f≥0 单列）；关节侧结果不宣称电能。

### WP-17-T04 RobWorkSim 正动力学
- 代码范围：`src/ForwardDynamicsScenario.cpp`；`test/ForwardDynamicsTest.cpp`。
- 前置任务：WP-17-T02。
- 输出工件：`ForwardDynamicsScenario`（`DynamicSimulator`＋锁定物理引擎＋RigidDevice 速度控制；控制输入线性插值、初态 (q0, ω0)、h＝1e-3 s）与 `ForwardDynamicsReport`。
- 验收断言：`ForwardDynamicsTest`（模块详设 §6）——§15.3 h/h2 收敛（2 s 基准、位置差 ≤1e-4 rad、速度差 ≤1e-3 rad/s，超限 → `IRD-DYN-FD-NOT-CONVERGED` 且不得放宽容差）、发散 → `IRD-DYN-FD-DIVERGED`（关节侧结果保留 Partial 诊断件、运行判 Failed）、控制输入/初始状态/步长入证据。

### WP-17-T05 物性与摩擦数据不足
- 代码范围：`include/sdurws/ird/dynamics/DynamicsDiagnostics.hpp`；`src/DynamicsEvaluator.cpp`（降级路径与评估主流程）；`test/InsufficientDataTest.cpp`；`testdata/dynamics/failpoints/`。
- 前置任务：WP-17-T02、WP-17-T03。
- 输出工件：DYN-06 降级语义——关键物性缺失 → 估算参数＋`Screening` 继续（`IRD-DYN-PROPERTIES-MISSING`，Warning）或补数据重算；摩擦假设缺失 → 零摩擦继续并降级证据＋诊断标注（`IRD-DYN-FRICTION-MISSING`，Warning）；证据等级三级标注与提升规则。
- 验收断言：`InsufficientDataTest`（模块详设 §6）——物性/摩擦缺失 → 降级等级＋Warning 诊断，不产生精确结论；证据不足不得自动提升等级（§6.6）。

### WP-17-T06 动力学到传动映射契约
- 代码范围（本 WP 侧）：`include/sdurws/ird/dynamics/DynamicResult.hpp`（只读视图公共头稳定化，供 WP-18 契约引用、不链接实现）；契约测试本体落 WP-18 侧 `evaluation/drivetrain/test/DrivetrainContractTest.cpp`（目标 `sdurws_ird_drivetrain_contract_test`）。
- 前置任务：WP-17-T03；对外与 WP-18-T01、WP-18-T02 侧联。
- 输出工件：候选无关性联合验收记录（双评审签署：动力学/驱动工程师＋独立测试）、映射消费接口评审纪要。
- 验收断言：`DrivetrainContractTest`（模块详设 drivetrain §6，WP-18 侧执行）——改变 `DriveTrainDesign` 不改变关节侧 `DynamicResult`；本模块输出可被映射且多速比黄金数据可复算（§15.1 传动映射黄金数据）。

## 8. 测试矩阵（模块详设 §6 为断言权威）

| 测试目标/文件 | 断言要点 | 覆盖需求 |
| --- | --- | --- |
| `sdurws_ird_dynamics_test` / SemanticFreezeTest.cpp | 常量与版本、零速连续性、语义变更→切片失效 | DYN-02、§5.7 |
| 同上 / InverseDynamicsTest.cpp | 二连杆解析与静态重力矩（相对 1e-6/绝对 1e-8）、摩擦不双计 | DYN-01/02、AT-07、§15.3 |
| 同上 / PowerEnergyTest.cpp | W+ 梯形积分、含驻留 RMS、峰值窗与所在段、功率符号 | DYN-03、§9.3 |
| 同上 / ForwardDynamicsTest.cpp | h/h2 收敛、发散诊断、输入/初态/步长入证据 | DYN-05、§15.3 |
| 同上 / InsufficientDataTest.cpp | 降级等级＋Warning、不产生精确结论 | DYN-06、§6.6 |
| `sdurws_ird_drivetrain_contract_test` / DrivetrainContractTest.cpp | 候选无关性（承载 WP-17-T06） | DYN-04 |

模型测试均为 `QCoreApplication`；GUI（DYN-08 曲线联动/回放）归 WP-10/WP-22 会话态，按 `QT_QPA_PLATFORM=windows` 一次一个执行，本包不建 GUI 测试目标。

## 验证命令（双形式，仓库根执行）

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_dynamics(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_dynamics_test
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_dynamics_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_dynamics(_contract)?_test$"
```

注：`sdurws_ird_dynamics_contract_test` 目标由本 WP 在 CMake 登记、按模块详设与 WP-18 共同验收；候选无关性断言本体位于 `sdurws_ird_drivetrain_contract_test`（DrivetrainContractTest），运行该目标见 WP-18 §9 命令。

## 10. 独立验证与独立评审

- 独立验证（黑盒）：二连杆解析算例与静态重力矩独立复算、完整循环积分对账、h/h2 收敛曲线复核、物性缺失注入。
- 独立评审：由动力学/驱动工程师独立复核摩擦模型、坐标系、积分、峰值窗口径与证据等级；语义冻结工件（T01 七项）需评审签署后才允许 T02 实现。
- 证据写入 `out/test-evidence/wp-17/<run-id>/`：二连杆/重力矩黄金数据版本与哈希、完整循环积分报告、正动力学收敛报告（h/h2 曲线）、假设清单、独立对照数据（试点前逐指标签署）。

## 11. 迁移与删除（requirements §13）

| 旧资产 | 处置 | 门禁 |
| --- | --- | --- |
| RobWorkSim DynamicWorkCell/RigidDevice/RNE 既有资产 | 保留/迁移（§13.2），经 `RneAdapter` 复用 | 双编译交叉校验＋解析对照通过 |
| 旧插件重复功率/摩擦计算 | 删除（Rewrite） | WP-18 共享映射验收＋静态扫描零命中 |
| 旧动力学 Widget 直读与结果 DTO | 删除；统一经评估端口与 `DynamicResult` | 契约测试通过 |
| 无法证明来源的历史动力学结果 | EvidenceOnly | 评审记录在案 |

## 退出条件

- DYN-01～06 全部 P0、AT-07 与 §15.3 动力学容差/正动力学收敛/动力包络行断言通过；DYN-07/08 保持可扩展且无空占位。
- 关节侧 `DynamicResult`（规范名）与候选传动无关，可被 WP-18 映射复算（DrivetrainContractTest 通过）。
- 摩擦只计一次、功率符号与 W+ 口径符合 §9.3 冻结式；证据等级不足不自动提升。
- §11 删除清单执行完毕，重复功率/摩擦实现退出构建。

## 13. 人周（总纲 §5.3：8～12 人周，含实现/测试/评审/修正）

| 任务 | 人周 |
| --- | ---: |
| WP-17-T01 | 1～1.5 |
| WP-17-T02 | 2～3 |
| WP-17-T03 | 1.5～2 |
| WP-17-T04 | 1.5～2.5 |
| WP-17-T05 | 1～1.5 |
| WP-17-T06 | 0.5～1 |

## 任务卡索引

- [WP-17-T01 动力学语义冻结](../agent-tasks/WP-17-T01-semantic-freeze.md)
- [WP-17-T02 逆动力学计算](../agent-tasks/WP-17-T02-inverse-dynamics.md)
- [WP-17-T03 广义力功率与能量](../agent-tasks/WP-17-T03-power-energy.md)
- [WP-17-T04 RobWorkSim 正动力学](../agent-tasks/WP-17-T04-forward-dynamics.md)
- [WP-17-T05 物性与摩擦数据不足](../agent-tasks/WP-17-T05-insufficient-data.md)
- [WP-17-T06 动力学到传动映射](../agent-tasks/WP-17-T06-drivetrain-contract.md)
