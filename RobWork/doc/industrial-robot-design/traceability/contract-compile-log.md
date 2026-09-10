# 契约编译留痕（contract-compile-log）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.0（2026-09-10 建册，随 PIPE v1.4 审核修复） |
| 上游协议 | contract-compilation.md（CCP）§3 第 6/7 步与 §5——每份契约的编译评审结论与放行/修订记录，一行一契约 |
| 补录说明 | 本册建立晚于既有 ready 契约的产出。CCP v1.0（2026-09-10）之前 ready 的契约按**补录**处理：编译证据＝其产出时的治理留痕（DTB 变更记录/phase-one-readiness），复核＝2026-09-10 流水线 v1.3/v1.4 批次的机器校验（validate-task 三查＋锚点/前置修复）与人工逐字段核对（branch/interUnit/knownPitfalls 判定）。后续一切编译全按 CCP §3 七步执行，不再有补录。 |

## 登记行

| taskId | branch | interUnit | knownPitfalls | 编译证据（产出留痕） | 复核（2026-09-10 v1.3/v1.4 批次） | 状态 |
| --- | --- | --- | --- | --- | --- | --- |
| CORE-T01 | wp03-t01 | false | —（空库起步，无 ird 跨单元边/接口） | DTB v0.5 §0.3 准入机器化＋phase-one-readiness；验收记录 CORE-T01-20260910.md | validate-task 三查过；branch/interUnit 补录 | done |
| TK-T01 | wp02-t01 | true | O-21（testkit→core 边未入 ARCH §3.5，白名单定稿前消账——落位即建立该边）、CR-06（TK-T04/05 容差接口 API diff 门禁，T01 不得提前消费 core 容差头） | DTB v0.5（dependsOn=CORE-T01 编码）＋phase-one-readiness | validate-task 三查过；跨单元属性＝构建层建立 testkit→core 边（T-2 唯一许可边） | ready |
| EV-T01 | wp05-t01 | true | CR-01（core 词表↔ResultEnvelope 状态联合测试）、CR-02（摘要算法只调 core ContentDigester，本单元编码器须声明排除字段） | 同上 | validate-task 三查过；跨单元属性＝构建层建立 evidence→core 边（acceptance"依赖图仅 core 边"） | ready |
| RT-T01 | wp06-t01 | true | CR-02（canonical 编码→摘要边界）、CR-05（RuntimeSnapshot 身份须作 evidence InputSlice 依赖，禁止 evidence 重编码 CanonicalModel） | 同上 | validate-task 三查过；跨单元属性＝构建层建立 runtime→core 边 | ready |
| POL-T01 | wp07-t01 | true | CR-02（策略编码→摘要边界）、CR-04（经 IPolicyNameContext/CollisionScene 消费 runtime，禁止直依赖 runtime 库） | 同上 | validate-task 三查过；跨单元属性＝构建层建立 policy→core 边 | ready |
| WP-00-T01 | wp00-t01 | false | —（纯治理文档） | DTB v0.7 补建记录 | validate-task 三查过 | ready |
| WP-00-T02 | wp00-t02 | false | —（纯格式契约/生成物） | DTB v0.7 补建记录 | validate-task 三查过；锚点 #附录C→#24 修正 | ready |
| WP-01-T01 | wp01-t01 | false | O-21、O-12（均已在 acceptance 条目内——门禁数据源前置） | DTB v0.7 补建记录 | validate-task 三查过；interUnit=false（治理/构建任务，不消费单元公共接口），knownPitfalls 自愿登记 | ready |
| WP-01-T02 | wp01-t02 | false | —（构建/CI 任务） | DTB v0.7 补建记录 | validate-task 三查过 | ready |
| DOC-T03 | doc-t03 | false | —（治理任务：补齐 planned 契约，不消费单元接口） | DTB §0.3/phase-one-readiness | validate-task 三查过；designRefs 五张单元卡锚点化＋CCP 引用 | ready |
| DOC-T01 | —（分支纪律前的直会话执行） | false | —（纯文档同步） | DTB §0.3 | validate-task 三查过；interUnit 补录 | done |
| DOC-T02 | —（同上） | false | —（纯契约生成治理） | DTB §0.3/phase-one-readiness §2.1 | validate-task 三查过；interUnit 补录 | done |
| FOUNDATION-CR-01 | —（同上） | false | —（纯文档审查：CR-01~08 关闭记录） | traceability/foundation-contract-review.md §4 | validate-task 三查过；interUnit 补录 | done |
| DIAG-T01 | —（同上） | false | —（编卡任务） | DTB §0.3 | validate-task 三查过；interUnit 补录 | done |
| UI-T01 | —（同上） | false | —（编卡任务） | DTB §0.3 | validate-task 三查过；interUnit 补录 | done |

| CORE-T02 | wp03-t02 | true | CR-02 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | ready |
| CORE-T03 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | CR-01 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| CORE-T08 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-D-1 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| CORE-T09 | wp03-t09 | false | — | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | ready |
| CORE-T10 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | false | — | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| EV-T02 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-EV-1 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| EV-T03 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-EV-1 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| EV-T05 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-EV-1、O-13 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| EV-T06 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-EV-3、P-EV-7 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| EV-T08 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-EV-4 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| EV-T09 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-EV-1 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| EV-T10 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-EX-7 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| EV-T11 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-EV-1 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| EV-T12 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | false | — | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| POL-T02 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-POL-2 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| POL-T03 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | CR-02 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| POL-T04 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-POL-4 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| POL-T05 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | CR-04、P-POL-9 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| POL-T07 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-POL-2 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| POL-T08 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-POL-2 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| POL-T09 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-POL-5 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| POL-T10 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-PR-6、P-EX-8 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| POL-T11 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-POL-4 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| POL-T12 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | false | — | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| RT-T02 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-RT-1 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| RT-T03 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-RT-7 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| RT-T04 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | CR-02、CR-05 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| RT-T05 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | O-12 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| RT-T06 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-RT-4 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| RT-T07 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-RT-3 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| RT-T08 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-RT-3 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| RT-T10 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | CR-05 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| RT-T11 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-RT-3 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| RT-T12 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-RT-1 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| RT-T13 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | false | — | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| TK-T02 | wp02-t02 | true | P-TK-6 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | ready |
| TK-T03 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-TK-7 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| TK-T06 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-TK-2 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| TK-T07 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-TK-2 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| TK-T08 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-TK-2 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |
| TK-T09 | wp02-t09 | true | O-33 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | ready |
| TK-T10 | —（planned，放行时按 ≙ 映射补 wp<nn>-t<kk>） | true | P-TK-2 | DOC-T03 回填（2026-09-10）：DTB §2 WP 行（≙）＋卡任务行（phase-one-task-index sourceRow）＋CCP §2 门槛 | validate-task 三查过；单会话对抗式复核（PIPE v1.7 §0.4 降级，处置约束见 readiness 审计记录）；纯 ID 陷阱句法按校验器口径 | planned |

## 修订行（ready 契约的后续修订，CCP §5）

| 日期 | taskId | 修订内容 | 提交 |
| --- | --- | --- | --- |
| 2026-09-10 | DOC-T01 / FOUNDATION-CR-01 / DIAG-T01 / UI-T01 / WP-00-T02 / DOC-T03 | designRefs 锚点补全/修正（validate-task ②查暴露） | 见 DTB v0.9 变更记录 |
| 2026-09-10 | 上表 10 份 | 补 branch/interUnit/knownPitfalls 字段（PIPE v1.4 结构化 queue 与 P1-4 守卫生效的前提） | 见 DTB v0.10 变更记录 |
