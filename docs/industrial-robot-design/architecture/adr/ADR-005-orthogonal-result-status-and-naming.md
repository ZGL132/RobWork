# ADR-005：评估正交状态拆分与公共符号命名裁决

> 状态：`Proposed`  
> 日期：2026-08-29  
> 检查点：`IRD-D2-20260829`  
> 关联契约：CTR-DOM-004、CTR-EXE-001、CTR-EXE-004、CTR-OPT-002  
> 需求 ID：EVI-01、TASK-01～03、CON-02、OPT-02/06/08  
> 受影响 WP：WP-03、05、08、12～23

## 背景

D1 评审发现三类跨文档权威冲突：① 需求 §7.4 用“完整性（Complete/Partial/Corrupt）”示意三个正交维度，`Corrupt` 与 `None` 在下游易被混用，且 `PayloadCompleteness` 无 `Corrupt` 值，两处口径不一；② 架构 `TaskState` 缺 `Pausing/Paused`，与需求 §6.4 状态机冲突；③ 五组公共符号存在异名（`DynamicsResult`、`CandidateResult`、`AnalysisConfig`、单用 `OptimizationStudy`、混淆 `CompiledRobotArtifacts/CompiledCandidateArtifact`）。同时 `Completed + Warning` 能否正式可行缺少显式裁决。

## 决策

1. **状态维度拆分**：完整性维度由 `PayloadCompleteness（Complete/Partial/None）` 与新增 `ArtifactIntegrity（Valid/Corrupt）` 两个正交枚举承担；`Corrupt` 只能由结果仓库读回时赋予，构造边界不得产生。需求 §7.4 的“完整性”示意由这两个枚举共同实现，不是语义变更。
2. **`Completed + Warning` 裁决**：可以正式可行，当且仅当每条警告的诊断类别在对应评估器 `RequiredEvidenceProfile.allowedWarningCategories` 内；任一未允许即不满足，界面必须列出缺口。依据：需求 §6.6 正式可行定义未排除 Warning，且 profile 显式携带允许警告类别。
3. **状态机补全**：`TaskState` 冻结为 9 态（含 `Pausing/Paused`），完整转移表以 architecture/execution-model.md §1 为唯一权威。
4. **命名裁决**：`DynamicResult`、`DesignCandidate`、`AnalysisConfiguration`、`OptimizationStudyDefinition/OptimizationRunResult` 为规范名；对应异名为禁止名称。`CompiledRobotArtifacts`（WP-06 基线工件）与 `CompiledCandidateArtifact`（WP-20 候选工件）是两个不同符号，不得互换使用。
5. **候选零值语义**：`DesignVector` 中已出现的 0 是合法设置值，未出现才表示未设置；禁止以零值补默认。

## 备选方案

| 方案 | 结论 | 原因 |
| --- | --- | --- |
| `PayloadCompleteness` 直接增加 `Corrupt` 值 | 拒绝 | 与 `None`（未产出）语义混用；覆盖度与可解释性是两个正交概念 |
| `Completed + Warning` 一律不可行 | 拒绝 | 与 REQ-06 的 Must/Should 语义矛盾（Should 未满足仅警告但设计仍可交付），会使多数正式设计不可行 |
| 维持 7 态 `TaskState`，暂停用标志位表达 | 拒绝 | 需求 §6.4 明确 `Pausing/Paused` 为独立状态；标志位无法表达合法转移约束 |
| 各文档就地修正异名、不设禁止清单 | 拒绝 | 无登记裁决则会随 D4～D7 重写回归 |

## 影响

- `evaluation-semantics.md`、`execution-model.md`、`symbol-registry.md`、`candidate-compilation.md` 按本决策落地；6 处下游异名引用已同步修正。
- WP-03 评估语义任务的验收断言改为“仅允许警告类别内的 `Completed + Warning` 可正式可行”。
- `ResultEnvelope` 增加 `ArtifactIntegrity` 维度；读回路径（WP-05）需实现哈希校验赋值。

## 迁移

D1 阶段无代码实现，无需代码迁移；文档层已同步修正，复审时再出现异名或 7 态状态机即为缺陷。

## 验证证据

- 契约测试清单见 evaluation-semantics.md §6、execution-model.md §6、candidate-compilation.md §7。
- 文档门禁：`validate-development-docs.ps1` 通过；异名 grep 归零。

## 接受条件

WP-03、05、08、20、21 消费者评审签署；`IRD-EXEC/RESULT/OPT` 相关诊断码进入 WP-09 目录。
