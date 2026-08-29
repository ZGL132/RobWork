# 工程策略与碰撞模块详细方案

- 方案版本：v0.3；需求基线：v0.8；架构检查点：`IRD-D2-20260829`；负责 WP：WP-07；阶段/发布：阶段 A / R1
- 契约权威：`architecture/public-interfaces.md` §3（评估端口）/§6（策略端口）/§7（值对象）、`architecture/evaluation-semantics.md` §1～2、`architecture/testing-contract.md`；字段权威：requirements §6.7.2；验证参数权威：requirements §15.3；任务卡：`agent-tasks/WP-07-T01～T05`

## 1. 模块职责

拥有唯一策略值对象 `EngineeringPolicySet`（SYM-POL-001）与 `CollisionPolicy`（SYM-POL-002）的规范化、共享唯一碰撞判定实现 `CollisionEvaluator`（SYM-POL-003）、路径验证协议实现和 RobWork 投影。策略字段与语义以 requirements §6.7.2 为准（引用，不复述）；本模块只写规范化次序、采样与二分实现、降级行为和一致性夹具。不拥有 WorkCell 编译、GUI 高亮、项目事务、碰撞后端算法本身与任务调度。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/
  include/sdurws/ird/policy/
    EngineeringPolicySet.hpp CollisionPolicy.hpp
    IEngineeringPolicyProvider.hpp CollisionEvaluator.hpp
    CollisionEvaluation.hpp CollisionPolicyAdapters.hpp
    PathValidationProfile.hpp PolicyDiagnostics.hpp
  src/PolicyNormalizer.cpp CollisionEvaluator.cpp PathValidator.cpp
      RobWorkCollisionAdapter.cpp PolicyProvider.cpp PolicyJson.cpp
  test/PolicyNormalizationTest.cpp CollisionEvaluatorTest.cpp
      PathProtocolTest.cpp RobWorkProjectionTest.cpp PolicyEntryTest.cpp
  testdata/policy/{pairs,profiles,xml,failpoints}/
  # 证据 → out/test-evidence/wp-07/<run-id>/（AGENTS §3，不入源码树）
```

CMake 目标：`sdurws_ird_policy`、`sdurws_ird_policy_test`、`sdurws_ird_policy_contract_test`。

依赖裁决：代码依赖 WP-03 core 与 WP-06（RuntimeNameMap/`IRuntimeNameResolver`、`CompiledRobotArtifacts`）；对 WP-05 仅有契约引用（评估端口 `IEngineeringEvaluator` 与结果契约 `ResultEnvelope`），无代码依赖——policy 目标不链接 WP-05 实现，端口装配由消费者侧完成；**WP-07 不依赖 WP-05**。另允许 RobWork Proximity/PathPlanning 与 Qt Core；禁止 Qt Widgets、业务插件私有碰撞开关、直接写项目 revision、手工 CSV。

## 3. 数据与接口

- `IEngineeringPolicyProvider` 签名以 public-interfaces §6 为准；两个重载均不得叠加私有默认值或覆盖策略字段。
- `EngineeringPolicySet`/`CollisionPolicy` 字段以 requirements §6.7.2 与 `schemas/engineering-policy.schema.json` 为准；对象对按 `(ownerScopeId, objectId)` 排序，引用必须存在于同一快照。
- `CollisionEvaluator` 是 `IEngineeringEvaluator` 的共享唯一实现，端口签名引用 public-interfaces §3（头文件位于 `out/test-evidence/wp-xx/<run-id>/`（AGENTS §3），本模块不复制定义）：`dependencyManifest()` 声明策略内容身份、canonical 物理身份与 nameMapId；`evaluate()` 消费不可变快照＋状态/轨迹；`capabilities()` 声明取消与检查点支持。
- 输出 `CollisionEvaluation`（模块私有视图）填充 `ResultEnvelope`：`ExecutionOutcome/EngineeringStatus/PayloadCompleteness` 值域以 evaluation-semantics §1 为准，合法组合以 §2 为准；同一输入下 pair、采样点与诊断顺序稳定。

## 4. 调用与状态

```text
snapshot(policy + canonical + nameMap + state/path)
  → 规范化：验证来源/字段 → 排序 → 去完全重复 → 拒绝语义冲突 → 计算稳定 content hash
  → 参与对象与配对归一（全部 objectId 经 IRuntimeNameResolver 反解）
  → 共享后端检测 / 路径采样（§5.1、§5.2）
  → CollisionEvaluation + 诊断 + 证据（结论措辞见 §5.1）
```

错误矩阵：

| 错误码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| `IRD-POLICY-CONFLICT` | 语义冲突、无来源、重复规则、负/NaN 安全距离 | Input | Error | 修正策略草稿后显式重新应用 |
| `IRD-POLICY-PAIR-OVERLAP` | 同一对象对同时属于 excluded 与 allowed | Input | Error | 显式二选一后重新规范化 |
| `IRD-POLICY-UNRESOLVED-OBJECT` | 引用不存在于同一快照的对象 | Input | Error | 解除引用或补齐对象 |
| `IRD-POLICY-BACKEND-UNAVAILABLE` | detectorBackend 不可用或 RobWork 投影失败 | System | Error | 切换后端版本后重新评估 |

## 5. 关键实现约定

1. **路径验证协议**：初始等分数、关节步长上限、细分余量、最大细分深度与安全距离一律引用 requirements §15.3 冻结值，由有版本的 `pathValidationProfile` 携带；运动学/轨迹/优化不得在本地覆盖启用状态、对象参与、配对规则或安全距离。本模块实现二分：对碰撞或距离不确定的段优先递归二分；深度耗尽仍未达分辨率输出 `Completed + DataInsufficient + Complete`。结论措辞冻结为"在本策略与分辨率下未发现碰撞"，不得宣称连续安全证明。
2. **距离降级**：最近障碍距离查询不可用时不得推断"距离足够"，仅按关节步长继续细分，并按 `unknownDistanceFallback` 返回 `DataInsufficient/Failed`，禁止默认安全。
3. **语义映射**：excluded pair 跳过检测且不生成无碰证据；allowed contact 仍检测并记录最小距离，不判硬失败；普通 pair 低于安全距离判 `Infeasible`；`DisabledForDraft` 仅用于草稿预览，不与 `EvaluationMode` 混用。
4. **跨插件一致**：同一快照＋同一策略下，运动学/轨迹/优化三入口返回相同的对象 ID 对、判定与允许/排除原因；差异只能来自显式记录的评估模式或验证 profile。显示/高亮等会话状态不进 policy hash、sliceHash、缓存与结论。
5. **投影**：`CollisionPolicyAdapters` 从同一策略＋`RuntimeNameMap` 确定性生成 `CollisionSetup`/`ProximitySetup`/路径 profile；XML 导入先合并为策略草稿，enabled、pair 或距离冲突返回诊断并要求显式选择，不得静默择一。

## 6. 测试与证据

| 测试 | 断言要点 |
| --- | --- |
| PolicyNormalizationTest | 策略 JSON 往返、排序去重、冲突/未知对象/非法距离拒绝、content hash 稳定 |
| CollisionEvaluatorTest | 参与矩阵、excluded/allowed 语义、距离 fallback、三消费者一致性 |
| PathProtocolTest | §15.3 参数逐项、二分行为、深度耗尽转 DataInsufficient、结论措辞固定 |
| RobWorkProjectionTest | 投影反解 objectId/参与/配对/距离与策略规范 JSON 一致；XML 冲突诊断 |
| PolicyEntryTest 与 PolicyProviderContractTest | provider 不叠加默认值；hash 不含显示状态；私有开关静态扫描 |

验证命令（脚本形式与原生回退）：

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_policy(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_policy_test
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_policy_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_policy(_contract)?_test$"
```

证据包含 snapshot/policy/revision 身份、策略 content hash、采样配置、每对距离、fallback 原因、后端与 RobWork 版本、结论措辞和独立评审签名。

## 7. 迁移与删除表

| 旧链路 | 处置 | 条件 |
| --- | --- | --- |
| `collisionSetup.enabled`/`proximitySetup.enabled` | 只读适配 → 删除 | 静态扫描与 AT-19 通过 |
| 插件私有碰撞开关与排除规则 | Rewrite | 策略唯一所有权确立 |
| 无法证明策略来源的旧结果 | EvidenceOnly | 评审记录在案 |
