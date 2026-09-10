# 第一批 11 单元详设同步与编码准入

日期：2026-09-10。范围：文档状态、任务编排、接口交接与编码前置检查；不含产品实现或正式架构批准。

**结论：可以开始 M0 基础构建与测试设施编码，首项建议 CORE-T01，随后 TK-T01 与 WP-01-T01 门禁。不能据此全面放行 11 单元的功能实现。** 详设编写完成、契约冻结、实现验收是三个独立状态。

## 1. 当前基线

| 单元 | 详设 | 评审/冻结 | 任务行数量 | 执行契约 |
| --- | --- | --- | ---: | --- |
| core | 已编写 v0.1 | Draft-Structured / contract-review | 10 | foundation/ |
| testkit | 已编写 v0.1 | Draft-Structured / contract-review | 10 | foundation/ |
| evidence | 已编写 v0.1 | Draft-Structured / contract-review | 12 | foundation/ |
| runtime | 已编写 v0.1 | Draft-Structured / contract-review | 13 | foundation/ |
| policy | 已编写 v0.1 | Draft-Structured / contract-review | 12 | foundation/ |
| project | 已编写 v0.1 | Draft | 16 | 待逐项补齐 |
| execution | 已编写 v0.1 | Draft | 10 | 待逐项补齐 |
| diagnostics | 已编写 v0.1 | Draft | 11 | 待逐项补齐 |
| io | 已编写 v0.1 | Draft | 7 | 待逐项补齐 |
| ui | 已编写 v0.1 | Draft | 14 | 待逐项补齐 |
| reporting | 已编写 v0.1 | Draft | 16 | 待逐项补齐 |

共 131 行单元任务，其中 57 行已有 foundation 执行契约，74 行尚无单份契约；后者包含 DIAG-T01/UI-T01 两项已编写的设计任务，不能把 74 全部计为待编码任务。[全量任务索引](phase-one-task-index.json) 保留每行输入、前置、验收和 WP 映射原文，不以编号后缀推测对应关系。

第一批设计范围包含 reporting；其业务报告验收仍在 B/C。workflow 未在这 11 单元内，阶段 A 生命周期前置设计仍须完成。剩余九份为 modeling、requirements、kinematics、trajectory、dynamics、drivetrain、selection、optimization、workflow。需求阶段 A～E、R1/R2 与 WP-A～I 各自含义不变。

## 2. 已同步事实与保留门禁

| 项目 | 当前事实 | 对编码的影响 |
| --- | --- | --- |
| 五基础单元 CR-01～08 | 既有记录已设计级关闭，联合契约测试未执行 | 五个 T01 可领取；全部 CR 及真实契约测试通过后才能 frozen |
| project 文档完整性 | 当前已有 §1～§15 与 PRJ-T01～16；“只有 §1～§7”已过时 | 文档缺失问题消除；接口联合复核仍需完成，不扩大 CR-03 最小接口 |
| P-RT-6 / O-17 | io §8.6、§15.3 P-IO-2 已裁决：provider 持有稳定缓冲至析构，调用方同步消费，不跨调用长期持有；Recorded 每次重读重算 | 设计级关闭；IO-T05/06 与 runtime 资源复查用例须真实验证 |
| O-25 / P-RPT-6 | reporting §14.3 已裁决不留 PDF 专用接口桩 | 设计级关闭；只同步消费者，不改需求范围 |
| P-PR-6 / P-EX-8 / P-DIAG-5 | diagnostics 已编写，但 sink 名称/归属统一仍未裁决 | 不因卡片存在而自动消账；相关真实装配前完成接口确认 |
| P-PR-5 / P-IO-1 / P-RPT-1～3 | 注入、包编解码、正式写入与 L5 适配仍有交接项 | 不新增 project→io、reporting→io/runtime 编译依赖；受影响任务逐项阻塞 |
| P-IO-3 | ZIP/XML 选型尚待 vcpkg 可用性与版本登记 | IO-T04 保持阻塞条件，不擅自选库 |
| CR-06 | TK-T04/05 依赖 CORE-T04/05 实现后的 API diff | 保持 planned；不能仅凭详设置 ready |
| O-03 | ARCHITECTURE v0.11 仍 Draft | 未擅自 Accepted；发现实际架构冲突的任务停止并登记，不阻塞既定 M0 落位 |
| M0～M3 | 本次没有执行构建、产品测试、ird_gates | 不宣布任何实现里程碑通过；各任务按 DTB §5.2 DoD 留痕 |

## 3. 可执行顺序与任务准备

1. 从 CORE-T01 开始，联同 TK-T01、WP-01-T01 建立双模式构建、vcpkg GTest 与门禁；依赖缺失作为该任务环境问题处理。已有 ready 表示允许开始，不表示测试目标现已存在。
2. 完成 core/testkit 前置后，按单元任务行推进 evidence/runtime/policy；diagnostics 先准备单份契约，再按 core 前置推进。
3. project → execution，io/ui 按各自前置推进。服务单元领取前运行 [DOC-T02](../tasks/DOC-T02.json) 所述契约补齐与交接核对，不把本索引直接交给 verify-task。
4. reporting 先做基础模型/渲染/替身测试；真实章节、往返复算等待 B/C 域结果。ui 的真实 workflow 数据、三维交互、策略编辑按原任务阶段交付，桩测试不等于端到端通过。

基础任务契约中的 requirements/验收字段仍须逐项核对：validate-task 只检查字段存在，不检查非空追溯和实际前置完成。requirements-to-units.json 是导航摘要，完整需求映射仍以 DTB §3 和各单元追踪矩阵为权威，不能把 JSON 条数当覆盖率。

## 4. 验证与交付边界

文档结构和任务契约校验结果见 [phase-one-validation.log](phase-one-validation.log)。补充核对包含：20 个唯一单元、11 个已编写文件、131 个唯一任务编号及原始任务行、57 个 canonical 引用、剩余 9 个未编写单元。

未执行：产品代码编译、独立冒烟、集成构建、gtest、GUI 测试、ird_gates、联合契约测试、系统 AT。此次只修改文档和 JSON，不伪造 gtest XML 或产品 ird-test-report.json。开始实现时按仓库 AGENTS.md 执行中文注释、双模式构建和真实留痕；GUI 测试使用 VS x64、windows 平台插件、单 executable 绝对路径启动。
