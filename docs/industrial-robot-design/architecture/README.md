# 架构契约目录

本目录冻结实现工作包之间共享的字段、接口、状态、错误和测试语义。`requirements.md` 只负责产品行为；本目录负责将已冻结行为转换为可实现契约；工作包不得复制或重新解释本目录的公共语义。

> 文档治理基线：`IRD-D0-20260829`  
> 当前检查点：`IRD-D10-20260829`（D1～D9 全链完成 + D10 语义闭合；契约联合评审通过）  
> 目录状态：`Proposed`（`IRD-D10-20260829` 联合评审已执行、结论通过，待签署；评审记录见 [review/2026-08-29-contract-review.md](review/2026-08-29-contract-review.md)）。

## 阅读顺序

1. [契约注册表](contract-registry.md)：稳定契约 ID、权威位置、所有者、消费者与接受目标。
2. [公共符号注册表](symbol-registry.md)：跨模块类型、接口和术语的规范名称与命名裁决。
3. [领域模型](domain-model.md)：身份、单位与核心聚合。
4. [规范运动学契约](canonical-kinematics.md)：SE(3) 变换链、坐标系、旋转表示与任意轴适配。
5. [评估语义契约](evaluation-semantics.md)：正交状态、合法组合与正式可行谓词。
6. [执行模型](execution-model.md)：任务状态机、并发所有权、取消/恢复与结果接纳。
7. [候选编译契约](candidate-compilation.md)：设计变量、绑定注册与候选补丁。
8. [持久化契约](persistence-schema.md)：`.rwdesign` 布局、写边界、追加协议与升级。
9. [公共接口](public-interfaces.md)：端口签名、公共值对象字段与错误面。
10. [测试与证据](testing-contract.md)。
11. [ADR 索引](adr/README.md)。

## 检查点完成边界

- D1（`IRD-D1-20260829`）：登记 24+1 个跨模块契约和 59+4 个公共符号；ADR-001～004 正文；`ResultEnvelope` 命名裁决。
- D2（`IRD-D2-20260829`）：完成五大高风险契约正文——`canonical-kinematics.md`（新增）、`evaluation-semantics.md`（新增）、`execution-model.md`（重写，9 态状态机）、`candidate-compilation.md`（新增）、`persistence-schema.md` 与 `public-interfaces.md`（重写）；消除 `EvaluationEnvelope` 违禁名、`TaskState` 缺态、`q-zero` 双偏置三类权威冲突（ADR-005）；6 处下游异名引用同步修正。
- 契约状态：`IRD-D10-20260829` 联合评审已执行、结论通过，全部条目维持 `Proposed`，签署后升 `Accepted`；此后任何语义变更必须走契约变更流程（所有者提出、ADR 记录、消费者影响清单、版本提升）。

## 契约所有权

| 契约 | 权威内容 | 主要所有者 |
| --- | --- | --- |
| `domain-model.md` | 单位、身份、值对象与领域聚合 | WP-03 |
| `canonical-kinematics.md` | SE(3) 变换链、坐标系、旋转表示、qIndex 与适配补偿 | WP-06 |
| `evaluation-semantics.md` | 评估正交状态、合法组合、证据档案与正式可行谓词 | WP-03 |
| `execution-model.md` | 任务状态机、请求身份、并发、取消/检查点/恢复、结果接纳 | WP-05、WP-08 |
| `candidate-compilation.md` | 设计变量、绑定注册、设计向量与候选补丁 | WP-20 |
| `persistence-schema.md` | `.rwdesign`、修订、资源、追加协议和升级边界 | WP-04 |
| `public-interfaces.md` | 跨模块端口、类型和错误面 | 各接口所有者，WP-03 统筹 |
| `testing-contract.md` | 黄金数据、容差、证据和阶段门禁 | WP-02、WP-23 |

公共契约必须由对应所有者提出变更，附 ADR、消费者影响清单和契约测试；变更完成评审前保持原版本状态，不得由业务 WP 私自修改。消费者发现缺口时应提出变更，不得在模块方案或实现中创建第二套定义。

## 转为 Accepted 的门禁

1. 需求 ID、所有者、消费者和版本明确；
2. 字段、类型、单位、可空性、状态、错误和线程/所有权可机器或契约测试验证；
3. 关键取舍已有 ADR，替代方案和迁移影响完整；
4. 所有消费者完成影响评审，不存在同名异义或异名同义；
5. Schema/IDL/接口样例可解析，并存在失败、正常和兼容性测试；
6. 独立架构与测试评审通过。
