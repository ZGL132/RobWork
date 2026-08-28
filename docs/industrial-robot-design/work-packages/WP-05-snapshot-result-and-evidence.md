# WP-05 快照、结果与证据实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` and complete this plan task-by-task.

**Goal:** 实现不可变分析快照、评估输入切片、精确失效、追加式结果仓库、证据等级和唯一正式可行判定。

**Architecture:** `SourceRevisionRef` 只负责追溯，`EvaluatorInputSlice` 决定缓存和当前性。结果的执行完整性、当前性和工程结论正交保存；旧结果永不原地修改。

**Tech Stack:** C++、Qt Core JSON、SHA-256、CTest。

---

## 文件与目标

**创建目标：** `sdurws_ird_evidence`、`sdurws_ird_evidence_test`。

**创建：**

- `industrialrobot/evidence/include/.../EvaluatorDependencyManifest.hpp`
- `industrialrobot/evidence/include/.../EvaluatorInputSlice.hpp`
- `industrialrobot/evidence/include/.../AnalysisSnapshot.hpp`
- `industrialrobot/evidence/include/.../EvidenceBundle.hpp`
- `industrialrobot/evidence/include/.../ResultEnvelope.hpp`
- `industrialrobot/evidence/include/.../IResultRepository.hpp`
- `industrialrobot/evidence/include/.../ResultCurrentnessService.hpp`
- `industrialrobot/evidence/src/`
- `industrialrobot/evidence/test/`

**覆盖需求：** CON-01～06，EVI-01，NFR-COR-04，NFR-COR-02 的结果集合部分，AT-04、05、10、12。

## 冻结接口

```cpp
struct EvaluatorDependencyManifest {
    EvaluatorId evaluatorId;
    EvaluatorVersion version;
    std::vector<FieldDependency> fields;
};

struct AnalysisSnapshot {
    SnapshotId id;
    SourceRevisionRef source;
    EvaluatorInputSlice input;
    EngineeringPolicySetRef policy;
    RuntimeNameMapRef runtimeNames;
    SoftwareBaseline software;
    RandomSeed seed;
};

class IResultRepository {
public:
    virtual void append(const ResultEnvelope&) = 0;
    virtual std::vector<ResultEnvelope> query(const ResultQuery&) const = 0;
};
```

当前性只比较评估器声明的规范输入切片。单纯机械臂运行时名称拼写变化不污染基于 objectId 和物理内容的数值切片；名称映射仍完整进入快照并在结果接纳时校验。

## 任务

### Task 1：输入切片和依赖清单

- [ ] 先写 TCP、负载、电机成本、显示开关和机械臂重命名的失效矩阵测试。
- [ ] 实现字段级依赖声明、规范化顺序和内容身份。
- [ ] 验证 TCP 使运动学及真实下游失效，电机成本不使运动学/轨迹失效，显示开关不使任何结果失效。

### Task 2：不可变快照

- [ ] 先写缺策略、缺 RuntimeNameMap、外部 Verified 资源无不可变副本和版本缺失失败测试。
- [ ] 快照包含完整输入对象、配置、策略、名称映射、算法/基础库版本、随机种子和资源哈希。
- [ ] Quick 临时外部引用明确降级，不能进入正式报告。

### Task 3：结果正交状态

- [ ] 参数化覆盖 outcome、engineeringStatus、payloadCompleteness 和 currentness。
- [ ] 拒绝 Canceled/Failed/Interrupted 与 Pass/Infeasible 或 Complete payload 的非法组合。
- [ ] Superseded 只改变索引当前性，不修改历史 payload 和证据。

### Task 4：结果接纳和历史查询

- [ ] 接纳前校验 projectId、branchId、revision、runId、attemptId、切片身份、策略身份和 objectId 名称反解。
- [ ] 迟到结果只追加到原分支历史，不能成为新项目/分支当前结果。
- [ ] 支持按修订、评估器、当前性、证据等级和运行状态查询。

### Task 5：正式可行和报告就绪

- [ ] 复用 WP-03 唯一正式可行谓词，不在报告层重新解释。
- [ ] RequiredEvidenceProfile 缺任何必需评估器、资源保真度或最低等级时列出具体缺口。
- [ ] DataInsufficient、Partial 或 Quick 结果不能成为正式可行或正式报告证据。

## 验证命令

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_evidence_test$'
```

## 退出条件

- A-GATE-01～03、AT-04、05、10、12 的快照/当前性断言通过。
- 所有结果可追到不可变快照；失败、取消和部分结果不会被正式复用。
- 相同切片产生相同内容身份；无关字段变化不误伤结果。
