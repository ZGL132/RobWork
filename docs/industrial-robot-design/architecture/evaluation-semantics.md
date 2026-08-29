# 评估语义与正式可行契约

> 契约 ID：`CTR-DOM-004`（权威位置自本文件起从 domain-model.md §3 迁入）  
> 检查点：`IRD-D2-20260829`  
> 文档状态：`Proposed`（等待 WP-03、05、08、12～23 消费者评审签署）  
> 权威边界：本文件是评估正交状态、合法组合、`RequiredEvidenceProfile` 和 `isFormallyFeasible()` 的唯一权威；需求 §6.4、§6.6、§7.4 是产品语义决策来源。

## 1. 正交维度（冻结）

```cpp
enum class ExecutionOutcome    { Completed, Canceled, Failed, Interrupted };
enum class EngineeringStatus   { Pass, Warning, Infeasible, DataInsufficient, NotEvaluated };
enum class PayloadCompleteness { Complete, Partial, None };
enum class ArtifactIntegrity   { Valid, Corrupt };            // 新增，SYM-STA-007
enum class ResultCurrentness   { Current, Superseded, Historical };
```

- `ExecutionOutcome` 描述运行终态；`EngineeringStatus` 描述工程判定；两者不得合并或混用。
- **维度拆分裁决**：需求 §7.4 的“完整性（Complete/Partial/Corrupt）”由两个正交枚举承担——`PayloadCompleteness` 只表达计划产出的覆盖（不再使用 `Corrupt` 值，消除与 `None` 的混用）；工件可解释性由 `ArtifactIntegrity` 单独表达。
- `ArtifactIntegrity = Corrupt` 只能由结果仓库在装载/读回时赋予（内容哈希或 Schema 校验失败），构造边界不得产生 `Corrupt`。`Corrupt` 结果不进入任何正式用途，payload 视同不可解释，但保留诊断与原始引用等待修复重算。
- `ResultCurrentness` 是仓库关联状态，不修改历史 payload：`Current`（切片与当前修订匹配）、`Superseded`（切片已变化，界面显示“需要重算”）、`Historical`（归属旧快照/旧分支的完整历史证据）。

## 2. 合法组合表（冻结）

`ExecutionOutcome(4) × EngineeringStatus(5) × PayloadCompleteness(3)` 共 60 个组合，仅以下两类合法；其余在构造边界拒绝并返回稳定诊断：

| 合法组合 | 用途 |
| --- | --- |
| `Completed + {Pass \| Warning \| Infeasible \| DataInsufficient} + Complete` | 正式结果候选，能否进入正式用途由 §4 谓词判定 |
| `{Canceled \| Failed \| Interrupted} + NotEvaluated + {Partial \| None}` | 诊断性结果，永不进入正式报告或可行 Pareto 集 |

非法代表项及理由（全部非法项 = 60 − 上述两类）：

- `Completed + NotEvaluated`：要么运行完成且有判定，要么没有创建结果（输入校验失败不创建运行结果，需求 §6.4）。
- `Completed + {Pass|Warning|Infeasible|DataInsufficient} + {Partial|None}`：正常结束的运行必须交付其声明 payload；覆盖缺口以 `DataInsufficient + Complete`（payload 内含逐项覆盖统计，如区域覆盖，需求 §15.3）表达。
- `{Canceled|Failed|Interrupted} + {Pass|Warning|Infeasible|DataInsufficient}`：未完成运行不得给出工程判定。
- 需求 §15.3 两个锚点用例必须按本表落位：用户取消的区域覆盖 = `Canceled + NotEvaluated + Partial/None`；预算耗尽 = `Completed + DataInsufficient + Complete`。

## 3. `RequiredEvidenceProfile`（字段冻结）

```text
RequiredEvidenceProfile
  - usageId                       正式用途稳定标识（如 "VerifiedReport"、"ParetoFeasible"）
  - profileVersion                Schema 版本
  - requiredEvaluators[]          每项：evaluatorId、minEvidenceLevel、
                                  requiredResourceFidelity（碰撞几何/目录曲线/物性来源）、
                                  allowedWarningCategories[]（该评估器允许的警告类别集合）
  - description
```

- `EvidenceLevel = Screening / PreliminaryDesign / ExternallyValidated`；证据不足不得自动提升等级（需求 §6.6）。
- `Quick` 结果不得作为正式通过证据；例外只有需求 §9.4 可证明保守的硬淘汰规则。
- 同一正式用途全局只有一个 profile 实例；报告层不得自行猜测证据要求（SYM-EVI-007）。

## 4. 正式可行谓词（冻结）

**裁决：`Completed + Warning` 可以正式可行，当且仅当该结果每条警告的诊断类别都在对应评估器的 `allowedWarningCategories` 内；任一警告类别未获允许即不满足正式可行，界面必须列出具体缺口。** 依据：需求 §6.6 正式可行定义未排除 `Warning`，且 `RequiredEvidenceProfile` 显式携带“允许警告类别”（需求 §6.6）；Must/Should 语义（REQ-06：Must 未满足判不可行、Should 未满足给警告）与此一致。

```cpp
struct FeasibilityVerdict {
    bool formallyFeasible;
    std::vector<FeasibilityGap> gaps;   // 每个失败条件一项，供界面列举
};

FeasibilityVerdict isFormallyFeasible(
    const std::map<EvaluatorId, ResultEnvelope>& results,
    const RequiredEvidenceProfile& profile)
{
    // 前置：所有 envelope 已通过 §2 合法组合校验且 artifactIntegrity == Valid
    for (const auto& req : profile.requiredEvaluators) {
        const ResultEnvelope* r = find(results, req.evaluatorId);
        if (!r)                                     return gap("缺少必需评估器结果");
        if (r->mode != EvaluationMode::Verified)    return gap("评估模式不是 Verified");
        if (r->outcome != ExecutionOutcome::Completed)         return gap("运行未完成");
        if (r->engineeringStatus == EngineeringStatus::NotEvaluated ||
            r->engineeringStatus == EngineeringStatus::DataInsufficient ||
            r->engineeringStatus == EngineeringStatus::Infeasible)
            return gap("工程判定不满足: " + name(r->engineeringStatus));
        if (r->payloadCompleteness != PayloadCompleteness::Complete)
            return gap("payload 不完整");
        if (r->evidenceLevel < req.minEvidenceLevel) return gap("证据等级不足");
        if (r->engineeringStatus == EngineeringStatus::Warning)
            for (const auto& d : r->diagnostics)
                if (d.severity == Severity::Warning &&
                    !contains(req.allowedWarningCategories, d.category))
                    return gap("警告类别未获允许: " + d.category);
    }
    return {true, {}};
}
```

- “计算完整”（`Completed + Complete`）不等于“证据完整”，也不等于“正式可行”；实现与测试一律以本谓词为准（需求 §6.6）。
- 硬约束违反由评估器以 `Infeasible` 表达；谓词不重复实现约束逻辑。

## 5. 展示义务

- 不满足正式可行时，界面必须按 `gaps` 列出具体缺口，不得只显示“不可行”。
- `DataInsufficient`、`Partial`、`NotEvaluated` 必须显式展示，不得与“不可行”混排（需求 §6.4、CTR-EXE-004）。
- `Superseded` 显示为“需要重算”；`Historical` 显示为历史证据并保留原快照名称。

## 6. 契约测试

1. 60 组合构造测试：仅 §2 两类合法，其余全部在构造边界拒绝。
2. 谓词正反例：`Completed + Warning + 允许类别` 可行；同状态但类别未允许不可行且 `gaps` 非空。
3. 证据等级边界：`minEvidenceLevel` 恰好满足/差一档。
4. `Corrupt` 读回：哈希不一致时仓库赋 `ArtifactIntegrity = Corrupt` 并拒绝正式用途。
5. profile 唯一性：同一 `usageId` 第二定义被拒绝。
