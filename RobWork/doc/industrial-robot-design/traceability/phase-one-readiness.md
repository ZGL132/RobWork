# 第一批 11 单元详设同步与编码准入

日期：2026-09-10。范围：文档状态、任务编排、接口交接与编码前置检查；不含产品实现或正式架构批准。

**结论：可以开始 CORE-T01 的 M0 构建落位实现；TK/EV/RT/POL-T01 的 canonical 契约已将启动顺序编码为 `dependsOn: ["CORE-T01"]`（2026-09-10 消歧：消除"文字准入仅为 CORE-T01"与"ready＋空前置可并行领取"的机器口径失配），CORE-T01 完成留痕后这四份方可领取；WP-01-T01 门禁随首个 STATIC 目标启用。不能据此全面放行 11 单元的功能实现。** 详设编写完成、任务契约完整、公共契约冻结和实现验收是四个独立状态。

## 1. 当前基线

| 单元 | 详设 | 评审/冻结 | 任务行数量 | 执行契约 |
| --- | --- | --- | ---: | --- |
| core | 已编写 v0.1 | Draft-Structured / contract-review | 10 | foundation/ |
| testkit | 已编写 v0.1 | Draft-Structured / contract-review | 10 | foundation/ |
| evidence | 已编写 v0.1 | Draft-Structured / contract-review | 12 | foundation/ |
| runtime | 已编写 v0.1 | Draft-Structured / contract-review | 13 | foundation/ |
| policy | 已编写 v0.1 | Draft-Structured / contract-review | 12 | foundation/ |
| project | 已编写 v0.1 | Draft | 16 | foundation/ |
| execution | 已编写 v0.1 | Draft | 10 | foundation/ |
| diagnostics | 已编写 v0.1 | Draft | 11 | foundation/ |
| io | 已编写 v0.1 | Draft | 7 | foundation/ |
| ui | 已编写 v0.1 | Draft | 14 | foundation/ |
| reporting | 已编写 v0.1 | Draft | 16 | foundation/ |

共 131 行单元任务，**已全部具备 canonical 任务 JSON**（tasks/foundation/；其中 74 份由 DOC-T02 于 2026-09-10 按本索引逐行生成）。当前状态分布：5 个 T01 为 `ready`——其中 TK/EV/RT/POL-T01 的 `dependsOn` 均为 `["CORE-T01"]`（准入顺序已机器编码，CORE-T01 完成留痕前不可领取；单元卡内"平行可做"指无技术前置，不覆盖准入顺序）；DIAG-T01/UI-T01 两项编卡任务为 `done`；其余 124 行为 `planned` 占位——不能领取执行，置 ready 须满足各自前置与门禁（CR-06、P-IO-3 等）。[全量任务索引](phase-one-task-index.json) 保留每行输入、前置、验收和 WP 映射原文，不以编号后缀推测对应关系。另：治理轨契约与编码轨并行可领——tasks/ 根目录 DOC-T03（52 份基础契约需求追溯回填，ready）与 2026-09-10 补建的四份横切契约（WP-00-T01/T02、WP-01-T01/T02，后两者 dependsOn 已编码，DTB v0.7）。

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
| CR-06 | TK-T04/05 依赖 CORE-T04/05 实现后的 API diff；DOC-T04（2026-09-11）已将 CORE-T04/05 置 ready（契约内 CR-06 门禁 acceptance 条目原样保留） | TK-T04/05 保持 planned；不能仅凭详设置 ready，须待 CORE-T04/05 实现后 API diff 通过 |
| O-03 | ARCHITECTURE v0.11 仍 Draft | 未擅自 Accepted；发现实际架构冲突的任务停止并登记，不阻塞既定 M0 落位 |
| M0～M3 | 本次没有执行构建、产品测试、ird_gates | 不宣布任何实现里程碑通过；各任务按 DTB §5.2 DoD 留痕 |
| 基础任务契约完整性 | **DOC-T03 已完成（2026-09-10）**：42 份缺字段 planned 契约逐份回填（requirements/designRefs/dependsOn/verify/acceptance 编译自 DTB §2 WP 行＋卡任务行；原"52"系立项计数，现况 42＋先行补齐 10）；CORE-T02/09、TK-T02/09 四份翻转 ready（前置 done＋卡内接口闭环），其余 38 份按前置链保持 planned；CR-06 门禁（CORE-T04/05、TK-T04/05）零触碰。**DOC-T04 放行批次（2026-09-11）**：core 主链七份 CORE-T03/04/05/06/07/08/10 整单元拓扑序批次置 ready（所有者裁决对 DOC-T03 保守口径的显式放宽，依据＝同单元接口设计级 diff 已全部关闭〔foundation-api-diff.md〕＋core.md 卡内锚点即交接核对；入队序 T03→T04→T05→T06→T07→T08→T10 由所有者口令经编排侧执行）；早期补齐的 T04/05/06/07 按 DTB §2 WP 行＋core.md §2.1 复核补 requirements（DYN-03/SEL-02、NFR-COR-02、EVI-01/TASK-02/PM-03、MDL-06）与 branch/interUnit/knownPitfalls 放行字段；CORE-T04/05 的 CR-06 门禁条目原样保留，TK-T04/05 仍 planned | 空 requirements 的 planned 归零；ready 总数 3→7（DOC-T03），本批再放行七份——canonical 现况 ready＝core 主链七份＋DOC-T04 本体（此前 ready 契约已随实现转 done；foundation-tasks.json 索引内嵌状态非权威，冲突以 canonical 为准）；放行逐项进行——审计记录见 [readiness-audit-doc-t03.md](readiness-audit-doc-t03.md)、编译留痕（含 DOC-T04 七行放行登记）见 contract-compile-log.md |

### 2.1 服务单元交接核对记录（DOC-T02，2026-09-10）

| 交接接口 | 核对结论 | 契约承载 |
| --- | --- | --- |
| 诊断 sink 名称/归属（P-DIAG-5/P-PR-6/P-EX-8） | 未裁决——DIAG-T04 工厂与 DIAG-T07 日志管线仅登记接缝（ErrorCodeTranslator 映射、ILogFileOps），不实现具体 sink 归属 | DIAG-T04/T07 acceptance 注明；保持 planned |
| io 包装配与 project→io 依赖（P-PR-5/P-IO-1） | 无编译边，注入式先行；补边与否待架构所有者裁决 | IO-T06 限定"io 侧 rename 发布/读 .staging 禁止"；PRJ-T18（阶段 B）落位实体 |
| reporting 写入通道（P-RPT-1～3） | reports/ 写入经 project 归档端口协调，reporting 不直写磁盘；L5 适配交接 | RPT-T09（IReportArtifactSink 契约＋Fake）、RPT-T12（L5 适配建议） |
| 摘要接口（RPT-T12 ↔ runtime §9.7 交接） | ModelSummary/ITaskStatusSource schema 未冻结——冻结留痕前不做消费端实现 | RPT-T12 acceptance 限定"schema 冻结留痕" |
| ZIP/XML 选型（P-IO-3） | 待 vcpkg 可用性与版本登记 | IO-T04 阻塞条件写入契约（未冻结不执行） |

## 3. 可执行顺序与任务准备

1. 从 CORE-T01 开始，建立 core 的静态目标、测试目标与零依赖门禁。当前构建树尚不存在该测试目标，正是 T01 的交付内容；不能在实现前运行它的完成态 verify 命令并将失败解读为设计阻塞。
2. CORE-T01 真实完成并留痕后，启动 TK-T01 与 WP-01-T01，完成 GTest 接入和门禁，再按依赖顺序推进其他基础单元。
3. 完成 core/testkit 前置后，按单元任务行推进 evidence/runtime/policy；diagnostics 先准备单份契约，再按 core 前置推进。
4. project → execution，io/ui 按各自前置推进。服务单元执行契约已由 [DOC-T02](../tasks/DOC-T02.json) 补齐（2026-09-10，74 份，交接核对见 §2.1）；基础单元 planned 占位的 requirements/验收回填仍由 [DOC-T03](../tasks/DOC-T03.json) 承担。两类索引都不能直接交给 verify-task。
5. reporting 先做基础模型/渲染/替身测试；真实章节、往返复算等待 B/C 域结果。ui 的真实 workflow 数据、三维交互、策略编辑按原任务阶段交付，桩测试不等于端到端通过。

基础任务契约中的 requirements/验收字段仍须逐项核对：validate-task 只检查字段存在，不检查非空追溯和实际前置完成。requirements-to-units.json 是导航摘要，完整需求映射仍以 DTB §3 和各单元追踪矩阵为权威，不能把 JSON 条数当覆盖率。

## 4. 验证与交付边界

文档结构和任务契约校验结果见 [phase-one-validation.log](phase-one-validation.log)。补充核对包含：20 个唯一单元、11 个已编写文件、131 个唯一任务编号及原始任务行、131 个 canonical 契约（其中 74 份为 DOC-T02 新增）、剩余 9 个未编写单元。

未执行：产品代码编译、独立冒烟、集成构建、gtest、GUI 测试、ird_gates、联合契约测试、系统 AT。此次只修改文档和 JSON，不伪造 gtest XML 或产品 ird-test-report.json。开始实现时按仓库 AGENTS.md 执行中文注释、双模式构建和真实留痕；GUI 测试使用 VS x64、windows 平台插件、单 executable 绝对路径启动。
