# Agent Notes：长期决策记录

| 字段 | 值 |
| --- | --- |
| 体系版本 | v1.0（2026-10-17，随 Agent-native 治理 Task 4 建立） |
| 用途 | 承载**已决架构/治理决策的叙事**：问题是什么、选了什么、否了什么备选、后果与代价、如何验证——决策即资产（治理原则 5），遗忘是负债 |
| 格式 | 六段式：Problem / Decision / Alternatives considered / Consequences / Verification / Related sources（各 Note 逐节成文，缺一不可） |
| 执法 | validate-docs.ps1 校验目录结构、六段齐全、implemented 禁计划语气（启发式黑名单——机器把关明显违例，真把关在验收 review） |

## 1. 与相邻载体的分工（防双权威）

| 载体 | 职责 | 关系 |
| --- | --- | --- |
| `ARCHITECTURE.md`（SA-xx/PA-x） | 架构规则的 **normative 权威**——规则正文是什么 | Notes 不复制规则正文，只链接并补叙事；冲突以 ARCHITECTURE 为准 |
| 本 Notes | 决策**叙事**——为什么立此规、否了哪些备选、代价是什么 | 单向引用 ARCHITECTURE 章节与 REQUIREMENTS 条目 |
| `governance-log.md` | **在途**待裁决项（P-*/O-*）流转台账 | 裁决落定并稳定后，其决策叙事可迁入 Notes（台账行保留流转记录） |

## 2. 目录与状态

| 目录 | 状态语义 | 写作语气 |
| --- | --- | --- |
| `proposed/` | 决策草案（未定稿） | 允许计划语气 |
| `implemented/` | 已生效决策 | **禁计划语气**（"待实现/将落位/尚未实现"等——implemented Note 陈述的是现行事实） |
| `rejected/` | 被否决的方案（含否决理由——被否方案同样是组织记忆） | 陈述已否决事实 |
| `archived/` | 被后续决策取代的旧决策 | 顶部注明被哪条取代 |

状态流转留痕：翻转 Note 状态时更新头部表格的状态字段并注明日期与触发批次；Notes 是叙事记录不是证据链，状态可翻转（PA-2 不可变纪律约束的是项目修订与 traceability 证据，不约束本目录）。

## 3. 写作纪律

1. **Alternatives considered 诚实性**：文档链未逐项登记当时备选的，事后重构必须显式标注"事后重构，非当时记录"——重构的备选按决策语境辨识，不得伪装成历史档案。
2. **Related sources 必须给真实锚点**：ARCHITECTURE 章节号、REQUIREMENTS 条目 ID、验证设施路径，逐条可核。
3. **新增决策先入 `proposed/`**，经所有者裁决生效后迁 `implemented/`（迁移＝移动文件＋状态字段更新，一个提交内完成）。
4. 迁移登记（如本批自 ARCHITECTURE 迁移）在 Note 头部注明来源章节与迁移日期；**源文档不删原文**（加法修订，删除推迟到治理 Task 7 之后——评审修正 4）。
