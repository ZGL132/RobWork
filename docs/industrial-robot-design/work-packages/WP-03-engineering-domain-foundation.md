# WP-03 工程领域基础实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` and complete this plan task-by-task.

**Goal:** 建立无 Qt Widgets 依赖的工程数学、身份、来源、领域对象和全局评估语义，成为所有模块唯一共享契约。

**Architecture:** 小型不可变值对象表达单位、姿态、身份和状态；聚合对象只保存领域语义，不保存 RobWork 指针、Widget、当前选择或运行时全名。JSON 适配与领域类型分离。

**Tech Stack:** C++、RobWork Math、Qt Core JSON 适配、CTest。

---

## 文件与目标

**创建目标：** `sdurws_ird_core`、`sdurws_ird_core_test`。

**创建目录：**

- `industrialrobot/core/include/rwslibs/industrialrobot/core/`
- `industrialrobot/core/src/`
- `industrialrobot/core/test/`

**核心文件：**

- `ObjectIdentity.hpp`
- `EngineeringUnits.hpp`
- `EngineeringPose.hpp`
- `ValueProvenance.hpp`
- `EvaluationSemantics.hpp`
- `DomainObjects.hpp`
- `DomainValidation.hpp`
- `DomainJson.hpp/.cpp`

**覆盖需求：** ARC-01、03，EVI-01，ERR-01 的公共字段，NFR-COR-03，NFR-MNT-01、03、04。

## 冻结接口

```cpp
struct ObjectIdentity {
    ObjectId objectId;
    ObjectId ownerScopeId;
    std::string localName;
};

enum class EvaluationMode { Quick, Verified };
enum class EvidenceLevel { Screening, PreliminaryDesign, ExternallyValidated };
enum class ExecutionOutcome { Completed, Canceled, Failed, Interrupted };
enum class EngineeringStatus { Pass, Warning, Infeasible, DataInsufficient, NotEvaluated };
enum class PayloadCompleteness { Complete, Partial, None };

bool isFormallyFeasible(const EvaluationEnvelope&, const RequiredEvidenceProfile&);
```

领域层姿态使用刚体变换/四元数，不持久化 RPY 作为真值。长度、角度、质量、时间和功率内部统一 SI；转动与移动关节广义力使用不同类型，禁止裸 `double` 混用。

## 任务

### Task 1：工程数学和类型安全

- [ ] 先写单位误用、非有限值、零轴、无效旋转和转动/移动广义力混用的失败测试。
- [ ] 实现 SI 值类型、姿态、旋转测地角和有向轴误差。
- [ ] 验证 RPY 只存在于输入/显示适配，不进入领域持久化结构。

### Task 2：稳定身份与来源

- [ ] 先写名称变化后引用仍指向同一 objectId 的测试。
- [ ] 实现不可由名称推导的 ObjectId、ownerScopeId 和作用域内 localName 校验。
- [ ] 实现 `ImportOrigin` 与 `ValueProvenance` 正交保存和 JSON 往返。

### Task 3：全局评估语义

- [ ] 对全部合法/非法 outcome、engineeringStatus、payload 组合编写参数化测试。
- [ ] 实现 Quick/Verified、EvidenceLevel 和 RequiredEvidenceProfile。
- [ ] 实现第 6.6 节唯一正式可行谓词，证明 Completed 不自动等于正式可行。

### Task 4：领域聚合最小 Schema

- [ ] 定义 RobotDesign、ToolDefinition、EnvironmentModel、EngineeringRequirements、LoadCase、DriveTrainDesign、分析配置、目录引用和优化研究的身份与引用骨架。
- [ ] 定义各不可变结果对象的公共 Envelope，不在 WP-03 实现领域算法字段。
- [ ] JSON 往返验证 ID、枚举、引用、来源完全一致，有限标量误差不超过 `1e-12`。

### Task 5：依赖边界

- [ ] CMake 明确禁止 Qt Widgets 链接。
- [ ] 扫描 public headers，禁止 `QWidget`、旧插件头、运行时名称拼接和可变全局状态。
- [ ] 独立验证者只链接 `sdurws_ird_core` 完成全部模型测试。

## 验证命令

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\build.ps1 -Configuration Debug -Target sdurws_ird_core_test
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_core_test$'
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1
```

## 退出条件

- 公共状态、单位、身份、来源和正式可行语义只有一个定义。
- 非有限数、非法单位和引用缺失不能静默转为 0 或默认通过。
- 核心测试可在不创建 QApplication/Widget 的情况下运行。
- 后续工作包不需要自行决定任何公共枚举或单位口径。
