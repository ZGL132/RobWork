# 架构契约注册表

> D1 检查点：`IRD-D1-20260829`；D2 检查点：`IRD-D2-20260829`  
> 文档状态：`Accepted`（IRD-D10-20260829 联合评审结论通过；2026-08-30 用户签署生效，签署记录见 [review/2026-08-29-contract-review.md](review/2026-08-29-contract-review.md) §5）  
> 作用：为跨模块契约分配稳定 ID、唯一所有者、权威位置、消费者和后续接受门禁。

本表是契约索引，不替代契约正文。契约 ID 一经发布不得复用；契约语义只能在“权威位置”修改，消费者文档只能引用。D1 完成“登记和消歧”，D2 完成核心语义，D3 完成字段、Schema、接口和机器校验；未达到目标门禁或未完成签署的条目保持 `Proposed`。

## 1. 注册规则

1. 每个契约只有一个主所有者；联合所有者必须明确各自拥有的子边界。
2. 同一公共字段、状态、单位、默认值或算法不得登记在两个契约中。
3. 修改契约必须保留 ID，提升版本，记录 ADR/变更原因、消费者影响和回归测试。
4. 拆分契约时原 ID 标记 `Superseded` 并指向新 ID；禁止把原 ID 改作其他用途。
5. `Accepted` 至少要求：正文完整、符号一致、消费者评审、失败/正常契约测试和可执行验证命令。

## 2. 契约清单

| 契约 ID | 契约边界 | 权威位置 | 主所有者 | 主要消费者 | 需求/决策来源 | 接受目标 | D1 状态 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `CTR-DOM-001` | 稳定对象身份、作用域、重命名和 ID 不复用 | [domain-model.md](domain-model.md) §1 | WP-03 | WP-04～25 | ARC-04；ADR-001 | D2 | `Accepted` |
| `CTR-DOM-002` | `ProjectRevision` 聚合和领域对象唯一所有权 | [domain-model.md](domain-model.md) §2 | WP-03、WP-04 | 全部业务 WP | ARC-01、ARC-02；ADR-004 | D2 | `Accepted` |
| `CTR-DOM-003` | SI 单位、类型化广义力、有限数和数值边界 | [domain-model.md](domain-model.md) §4 | WP-03 | 全部计算 WP | NFR-COR-03、NFR-MNT-03 | D2 | `Accepted` |
| `CTR-DOM-004` | 评估模式、证据等级、结果正交状态、合法组合和正式可行谓词 | [evaluation-semantics.md](evaluation-semantics.md) | WP-03 | WP-05、08、12～23 | EVI-01、TASK-02；ADR-005 | D2 | `Accepted` |
| `CTR-KIN-001` | 唯一规范 SE(3) 运动学模型、坐标约定、旋转表示和运行时真值 | [canonical-kinematics.md](canonical-kinematics.md) | WP-06 | WP-07、13、15～21 | ARC-03、需求 §7.3.1 | D2 | `Accepted` |
| `CTR-KIN-002` | `StandardDH`/`ExplicitJoint` 权威互斥、转换判定和有损拒绝 | [canonical-kinematics.md](canonical-kinematics.md) §8；决策来源 [requirements.md](../requirements.md) §7.3.2 | WP-06、WP-13 | WP-15～21 | MDL-06、MDL-07、AT-16 | D2 | `Accepted` |
| `CTR-NAM-001` | `RuntimeNameMap`、设备作用域名称和双向对象解析 | [public-interfaces.md](public-interfaces.md) §2 | WP-06 | WP-07、12～23 | ARC-04、CON-06；ADR-001、ADR-004 | D2/D3 | `Accepted` |
| `CTR-POL-001` | `EngineeringPolicySet`、`CollisionPolicy` 和共享评估器唯一性 | [requirements.md](../requirements.md) §6.7；[public-interfaces.md](public-interfaces.md) §4 | WP-07 | WP-05、12、15、16、20、21 | ARC-05、CON-06；ADR-004 | D2/D3 | `Accepted` |
| `CTR-PER-001` | `.rwdesign` 目录布局、路径和 ZIP 传输边界 | [persistence-schema.md](persistence-schema.md) §1 | WP-04 | WP-11、12、24、25 | CON-01、NFR-SEC-01；ADR-002 | D3 | `Accepted` |
| `CTR-PER-002` | 修订、内容寻址、只追加工件和多文件原子提交 | [persistence-schema.md](persistence-schema.md) §2 | WP-04 | WP-05、08、12、20、21 | ARC-01、NFR-REL-01；ADR-002 | D2/D3 | `Accepted` |
| `CTR-PER-003` | JSON 规则、Schema/格式版本、升级和失败恢复 | [persistence-schema.md](persistence-schema.md) §3～4 | WP-04 | 全部持久化消费者 | CON-01、CON-03、NFR-REL-04 | D3 | `Accepted` |
| `CTR-API-001` | 项目查询、领域命令、撤销/重做和修订冲突 | [public-interfaces.md](public-interfaces.md) §1 | WP-04 | 全部业务插件 | ARC-01、ARC-02 | D3 | `Accepted` |
| `CTR-API-002` | 规范模型编译和运行时名称解析端口 | [public-interfaces.md](public-interfaces.md) §2 | WP-06 | WP-07、13、15～21 | ARC-03、ARC-04 | D3 | `Accepted` |
| `CTR-API-003` | 评估器输入切片和 `ResultEnvelope` 输出端口 | [public-interfaces.md](public-interfaces.md) §2 | WP-05、WP-08 | WP-07、15～21 | CON-05、TASK-02 | D3 | `Accepted` |
| `CTR-API-004` | 结果只追加仓库、接纳校验和查询边界 | [public-interfaces.md](public-interfaces.md) §2 | WP-05 | WP-08、12、15～23 | CON-01、CON-02、TASK-03 | D3 | `Accepted` |
| `CTR-DIA-001` | 诊断字段、类别、稳定代码和错误面 | [public-interfaces.md](public-interfaces.md) §6 | WP-09 | 全部模块 | ERR-01、NFR-REL-05、NFR-MNT-03 | D2/D3 | `Accepted` |
| `CTR-EXE-001` | 后台任务生命周期、请求身份和合法状态转移 | [execution-model.md](execution-model.md) §1 | WP-08 | 全部评估器和 UI | TASK-01～03 | D2 | `Accepted` |
| `CTR-EXE-002` | 依赖清单、输入切片、当前性、失效和缓存键 | [execution-model.md](execution-model.md) §2 | WP-05、WP-08 | WP-12、15～23 | CON-04、CON-05 | D2 | `Accepted` |
| `CTR-EXE-003` | 取消、工作进程终止、检查点和恢复 | [execution-model.md](execution-model.md) §3 | WP-08 | 长任务消费者 | TASK-01、CON-04、NFR-REL-02～03 | D2 | `Accepted` |
| `CTR-EXE-004` | 迟到结果、分支/尝试绑定和正式结果接纳 | [execution-model.md](execution-model.md) §4 | WP-05 | WP-08、12、15～23 | CON-02、CON-06、TASK-03 | D2 | `Accepted` |
| `CTR-TST-001` | 测试分层、数值容差、性能基准和 Windows 规则 | [testing-contract.md](testing-contract.md) §1～2、§5 | WP-02、WP-23 | 全部 WP | NFR-COR-01～03、NFR-PERF-01～06 | D3 | `Accepted` |
| `CTR-TST-002` | Given/When/Then、证据字段、命名和独立评审 | [testing-contract.md](testing-contract.md) §3～4 | WP-02、WP-23 | 全部 WP | AT-01～19、NFR-MNT-05 | D3 | `Accepted` |
| `CTR-REL-001` | R1/R2 发布切片、独立性和阶段范围 | [requirements.md](../requirements.md) §14；[ADR-003](adr/ADR-003-release-slices-and-opt-b.md) | WP-00、WP-24 | WP-01～25 | 需求 v0.8 第 14、19 章 | D1 | `Accepted` |
| `CTR-OPT-001` | OPT-B/OPT-D 权威集合、候选不产生修订和应用边界 | [requirements.md](../requirements.md) §8.7.1；[ADR-003](adr/ADR-003-release-slices-and-opt-b.md) | WP-20、WP-21 | WP-05～08、12、13、15～19、22、23 | OPT-01～10 | D1/D2 | `Accepted` |
| `CTR-OPT-002` | 设计变量定义、变量绑定注册、设计向量、候选补丁和候选稳定身份 | [candidate-compilation.md](candidate-compilation.md) | WP-20 | WP-21、WP-05、WP-08 | OPT-02、OPT-06、OPT-08；需求 §7.4、§9.1 | D2 | `Accepted` |

## 3. 完整性结论

- D1（`IRD-D1-20260829`）：为全部跨模块架构边界分配了稳定契约 ID、权威位置、主所有者和消费者。
- D2（`IRD-D2-20260829`）：完成五个最高风险跨模块契约正文——规范运动学（`canonical-kinematics.md`）、评估语义（`evaluation-semantics.md`）、执行状态机（`execution-model.md` 重写）、候选编译（`candidate-compilation.md`）、持久化边界与公共接口（`persistence-schema.md`、`public-interfaces.md` 重写）；消除了 `EvaluationEnvelope` 违禁名、`TaskState` 缺失 `Pausing/Paused`、`q-zero` 双偏置三类权威冲突。
- D3～D9：Schema/接口机器验证、模块详设、工作包与任务卡、真实追踪和门禁增强全部完成（`DOCUMENT-BASELINE.md`）。
- D10（`IRD-D10-20260829`）：语义闭合修复（任务级循环依赖、诊断注册表 117 码裁决一致、跨模块符号补登记、WP-04/WP-12 口径统一、验证命令可执行化）后完成契约联合评审（结论：通过）；全部契约与 ADR 维持 `Proposed`，签署后升 `Accepted`；评审记录见 [review/2026-08-29-contract-review.md](review/2026-08-29-contract-review.md)。此后语义变更必须走契约变更流程并提升版本。
- 签署（2026-08-30）：用户指令"签署契约与 ADR"，评审记录 §5 签署栏四行完成（产品/需求所有者、契约所有者代表、消费模块代表由用户一次性授权，独立验证者由治理会话依据 WP-00-T01～T04 独立复跑证据签署）；§3 追认清单 7 项裁决全部追认、无否决项。25 项契约、76 个公共符号与 ADR-001～005 在同一治理提交中整体升 `Accepted`（不改契约正文，仅状态与签署引用）。
