# 动力学模块详细方案（dynamics）

- 方案版本：v0.3；需求基线：v0.7；架构检查点：`IRD-D2-20260829`；治理状态：Proposed
- 负责 WP：WP-17；阶段/发布：阶段 C / R1；任务卡：agent-tasks/WP-17-T01～T06
- 架构契约：`architecture/domain-model.md` §4（SI/类型化广义力）、`architecture/public-interfaces.md` §3/§7、`architecture/evaluation-semantics.md` §1～2、`architecture/execution-model.md` §1～3、`architecture/testing-contract.md`
- 需求锚点：requirements §8.5（DYN-01～08 与 `DriveTrainMappingEvaluator` 映射契约）、§7.1/§7.2/§7.4、§9.3（W+ 冻结式）、§15.3（动力学容差、正动力学收敛、动力包络行）、§6.6（证据等级）、NFR-DEP-05（RobWorkSim 锁定版本）；平台方案：runtime-model、execution-platform、snapshot-result
- 代码前置：WP-06、08、16（总纲 §5.3）；WP-18 依赖本模块（候选无关性由其消费侧契约测试验证）

## 1. 模块职责

实现动力学评估器：以 RobWorkSim 刚体模型＋递归牛顿—欧拉（`rwsim::util::RecursiveNewtonEuler`，DYN-01 规定算法）计算含重力、连杆惯性、末端负载与外力的逆动力学，叠加冻结的黏性＋库仑关节摩擦（DYN-02），沿 `TrajectoryPlan` 输出与候选传动无关的关节侧 `DynamicResult`（规范名，symbol-registry §4.6；`DynamicsResult` 为禁止名称）：类型化广义力、转角/位移、转速、加速度、机械功率、能量、峰值与 RMS 包络、仿真状态（DYN-03/04）；以 RobWorkSim 正动力学执行响应一致性检查与异常检测（DYN-05）；物性/摩擦数据不足时降级证据等级（DYN-06）。本模块拥有公共领域类型 `DynamicResult` 与关节摩擦语义。非目标：传动映射与电机侧量（WP-18）、多负载工况/急停工况包络合并（DYN-07 P1）、曲线联动与回放 UI（DYN-08 P1）、柔性/热/寿命（§19-8）、候选传动比较（WP-19/21）。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/dynamics/
  include/sdurws/ird/dynamics/
    DynamicResult.hpp   DynamicsEvaluator.hpp   DynamicsSemantics.hpp
    DynamicsDiagnostics.hpp
  src/DynamicsEvaluator.cpp   RneAdapter.cpp   FrictionModel.cpp
      PowerEnergyIntegrator.cpp   ForwardDynamicsScenario.cpp
      InertiaValidator.cpp   DynamicsJson.cpp
  test/SemanticFreezeTest.cpp   InverseDynamicsTest.cpp   PowerEnergyTest.cpp
      ForwardDynamicsTest.cpp   InsufficientDataTest.cpp
  testdata/dynamics/{two-link,gravity,cycle,failpoints}/
  evidence/WP-17/
```

CMake target：`sdurws_ird_dynamics`、`sdurws_ird_dynamics_test`、`sdurws_ird_dynamics_contract_test`。允许依赖：WP-03 core（含类型化广义力包装）、WP-05 evidence（端口头＋结果仓库）、WP-06 runtime（`CompiledRobotArtifacts` 的 DWC 工件）、RobWorkSim 稳定 API（DynamicWorkCell、RigidDevice、`rwsim::util::RecursiveNewtonEuler`、积分器/物理引擎，按 NFR-DEP-05 锁定版本）、Qt Core；契约引用（不链接实现）：WP-16 `TrajectoryPlan`/`ResolvedIkBranchSequence` 公共类型（payload 经 `IResultRepository` 取回）；调度经 WP-08 装配（契约引用）。禁止：Qt Widgets、WP-18 及其后模块头、本地效率/减速比计算（映射归 WP-18）、直读 UI 会话态。

## 3. 数据与接口

`DynamicResult` 字段语义以 §7.2 为准（与候选传动无关的关节侧广义力、转速、功率、能量、峰值、RMS、仿真状态），payload 经 `ResultEnvelope.payloadId` 引用，模块提供类型化只读视图；移动关节按类型化广义力（N）正常计算（§8.1 说明行），范围外限定仅作用于选型/传动目录。模块私有类型：

| 类型 | 字段 | 规则 |
| --- | --- | --- |
| `DynamicsSemantics` | 摩擦参数（b、τc、ωeps）、`dynamicsSemanticsVersion` | 版本进 `EvaluatorInputSlice.algorithmVersion`；默认值见 §5.1 |
| `JointSample` | 时间戳、q、ω、α、类型化 τ、P_joint、P_f | 时间戳取自 `TrajectoryPlan` 剖面，单调有限 |
| `PeakWindowStats` | 窗长、滑窗均值峰值、瞬时峰值、所在段与时刻 | 窗长＝器件目录峰值时间能力，无目录数据 1 s（§15.3/§8.5） |
| `RmsCycleStats` | τ/ω/P 的 RMS 与 T_cycle | 完整任务循环实际时间积分，含驻留（DYN-03） |
| `ForwardDynamicsReport` | 控制输入、初始状态、步长 h、积分器、h/h2 残差、发散诊断 | 进入 `DynamicResult` 仿真状态（DYN-05） |
| `ExternalWrenchBinding` | Wrench6D＋表达坐标系 objectId＋激活工况 | 外力在 TCP/连杆坐标系表达，随快照冻结（§7.1） |

`dependencyManifest()` 声明：canonical 物理身份、nameMapId、上游 `TrajectoryPlan` 引用、`LoadCase`/摩擦假设字段、DWC 与 RobWorkSim 版本、`dynamicsSemanticsVersion` 与种子。惯量以"参考点＋参考姿态＋参考坐标系"三要素随对象保存（§7.1），进入本模块前执行正定性与三角不等式校验。

## 4. 调用与状态

```text
snapshot → 校验（TrajectoryPlan 存在且 Current、物性/惯量/摩擦假设完整且有限）
  → 沿 plan 记录时间戳采样 → FK 状态 → RNE 刚体力矩（重力/惯性/负载/外力）
  → 叠加冻结摩擦模型 → 类型化广义力/功率序列 → W+/W−、峰值窗、完整循环 RMS
  → 正动力学一致性检查（DYN-05）→ DynamicResult＋证据 → WP-05 接纳
```

| 错误码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| `IRD-DYN-UPSTREAM-MISSING` | `TrajectoryPlan` 缺失/非 Current/版本不兼容 | Input | Error | 先重算轨迹 |
| `IRD-DYN-INERTIA-INVALID` | 惯量缺失、非正定或违反三角不等式（§15.3 物理一致性） | Engineering | Error | 修正物性后重算 |
| `IRD-DYN-PROPERTIES-MISSING` | 关键连杆/负载物性缺失（DYN-06） | Engineering | Warning | 估算参数＋`Screening` 继续或补数据重算；不得包装成精确结论 |
| `IRD-DYN-FRICTION-MISSING` | 摩擦假设缺失 | Engineering | Warning | 按零摩擦继续并降级证据＋诊断标注 |
| `IRD-DYN-STATE-DISCONTINUOUS` | 轨迹状态不连续（时间戳断裂/速度跳变） | Engineering | Error | 修正轨迹后重算 |
| `IRD-DYN-FD-NOT-CONVERGED` | h 与 h/2 对照超 §15.3 限值 | Engineering | Error | 减小步长或更换积分器后重跑，不得放宽容差 |
| `IRD-DYN-FD-DIVERGED` | 仿真发散/数值溢出 | System | Error | 关节侧结果保留为诊断件（Partial），运行判 Failed |

## 5. 关键实现约定

1. **摩擦模型（冻结）**：τ_j(t)＝τ_RNE(t)＋τ_f(t)，τ_f＝b·ω＋τ_c·s(ω)；零速库仑处理 s(ω)＝ω/ωeps（|ω|≤ωeps）否则 sign(ω)——连续确定性延拓，无独立静摩擦系数、无状态切换，禁止数值抖动；ωeps 默认 1e-4 rad/s（移动关节 1e-4 m/s，模块私有可评审）。DWC 构建时 RobWorkSim BodyInfo 摩擦字段置零，关节摩擦只在本模块计入一次（§8.5 摩擦归属唯一；WP-18 不得复计）。
2. **坐标系（冻结）**：重力在基座坐标（RNE 语义）；负载质量/质心/惯量在 TCP/工具坐标系表达（惯量三要素随 §7.1 保存）；外力 Wrench6D 在 `ExternalWrenchBinding` 声明的 TCP/连杆坐标系表达并随快照冻结，适配时按当前状态 FK 转基座传入；禁止依赖"当前选中坐标系"。
3. **功率与能量（冻结）**：P_joint＝τ_j·ω_j（正＝执行器向机构输出正机械功）；摩擦耗散 P_f＝τ_f·ω≥0 单列；W+＝∫max(P_joint,0)dt、W−＝∫min(P_joint,0)dt，按记录时间戳梯形积分（§9.3 冻结式），采样与积分规则随结果保存；纯关节侧 `DynamicResult` 不得宣称电能（§8.5 能量边界行）。
4. **峰值与 RMS（冻结）**：峰值＝峰值窗（窗长＝目录峰值能力，缺省 1 s）内滑窗均值最大值＋瞬时峰值，同时报告窗长与所在轨迹段（§15.3 动力包络行）；RMS 周期＝完整任务循环 T_cycle（含驻留）实际时间积分。
5. **正动力学（冻结）**：RobWorkSim `DynamicSimulator`＋项目锁定物理引擎，RigidDevice 速度控制模式；控制输入＝plan 关节速度剖面在积分步上的线性插值；初始状态＝轨迹起点 (q0, ω0)；默认步长 h＝1e-3 s。收敛判据按 §15.3：固定 2 s 基准工况分别以 h 与 h/2 各跑一次，末端及全过程最大关节位置差 ≤1e-4 rad、速度差 ≤1e-3 rad/s（移动关节按 m、m/s 同级）。
6. **证据等级**：按 §6.6 三级标注——估算参数最多 `Screening`，用户确认关键物性后 `PreliminaryDesign`，与独立工具/实测对照后 `ExternallyValidated`；不足不得自动提升；报告必须列出运动律与摩擦假设（效率假设属 WP-18 输出侧标注）。
7. **WP-17-T01 语义冻结产出物（清单）**：`DynamicsSemantics.hpp`（§5.1～§5.5 常量与公式 ID）＋`dynamicsSemanticsVersion`；零速摩擦、功率符号、W+ 积分、峰值窗/RMS、外力坐标系、FD 控制输入插值与积分器配置的黄金夹具；二连杆解析与静态重力矩对照数据（WP-02 登记）；动力学/驱动工程师评审签署记录。任一语义变更必须升版本并使依赖切片失效。

## 6. 测试与证据

| 测试 | 断言要点 |
| --- | --- |
| SemanticFreezeTest | §5.1～§5.5 常量与版本、零速连续性（\|ω\|≤ωeps 无符号跳变）、语义变更→切片失效 |
| InverseDynamicsTest | 二连杆解析算例与静态重力矩满足 §15.3 动力学容差（相对 1e-6、近零绝对下限 1e-8 N·m/N）；摩擦不双计 |
| PowerEnergyTest | W+ 梯形积分、含驻留完整循环 RMS、峰值窗口径与所在段、功率符号约定 |
| ForwardDynamicsTest | §15.3 h/h2 收敛、发散→`IRD-DYN-FD-DIVERGED`、控制输入/初始状态/步长入证据 |
| InsufficientDataTest | 物性/摩擦缺失→降级等级＋Warning 诊断，不产生精确结论 |

WP-17-T06 契约测试落在 WP-18 侧（`sdurws_ird_drivetrain_contract_test`）验证本模块输出可被映射且候选无关。GUI（DYN-08 曲线联动/回放）归 WP-10/WP-22 会话态（`QT_QPA_PLATFORM=windows` 一次一个）；本模块测试为 `QCoreApplication` 模型测试。证据写入 `evidence/WP-17/`：二连杆/重力矩黄金数据版本与哈希、完整循环积分报告、正动力学收敛报告（h/h2 曲线）、假设清单、独立对照数据（试点前逐指标签署）。验证命令（双形式，仓库根执行）：

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_dynamics(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_dynamics_test
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_dynamics_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_dynamics(_contract)?_test$"
```

## 7. 迁移与删除表

| 旧资产 | 处置（requirements §13） | 门禁 |
| --- | --- | --- |
| RobWorkSim DynamicWorkCell/RigidDevice/RNE 既有资产 | 保留/迁移（§13.2），经 `RneAdapter` 复用 | 双编译交叉校验＋解析对照通过 |
| 旧插件重复功率/摩擦计算 | 删除（Rewrite） | WP-18 共享映射验收＋静态扫描零命中 |
| 旧动力学 Widget 直读与结果 DTO | 删除；统一经评估端口与 `DynamicResult` | 契约测试通过 |
| 无法证明来源的历史动力学结果 | EvidenceOnly | 评审记录在案 |
