# DOC-T03 基础契约回填就绪审计记录

| 字段 | 值 |
| --- | --- |
| 任务 | DOC-T03（补齐基础单元 planned 任务的需求追溯与放行条件，契约 tasks/DOC-T03.json） |
| 日期 | 2026-09-10 |
| 执行模式 | PIPE v1.7 §0.4 单会话（实施＋复核同会话，对抗式复核降级声明）；编译留痕 42 行见 contract-compile-log.md |
| 范围 | **42 份**缺字段 planned 契约（空 requirements/无锚 designRefs/泛化 acceptance）——契约 acceptance 第 1 条写"52 个"系立项时点计数，现况实测 42（phase-one §2"57 中 42 空"口径一致；差值 10 份已在 DOC-T02/治理批次先行补齐），按"缺啥补啥"原则回填全部缺字段契约，零遗漏（131 份全量扫描确认无第三种缺字段形态） |

## 1. 回填方法（CCP §3 七步的单会话执行）

- **读源**：requirements/designRefs/dependsOn/verify/acceptance 逐字段编译自 DTB §2 WP 行（≙ 映射＋需求追溯列）＋单元卡任务行（phase-one-task-index.json sourceRow 原文）；
- **门槛**：CCP §2 逐字段——真实需求 ID（非 ID 引用如"附录 D C4"移入 acceptance/designRefs 文本）、锚点化 designRefs（`units/<unit>.md#<节>`，validate-task ②机器校验锚点真实存在）、动宾 title、逐条可验证 acceptance（映射卡内 UT 组/完成条件/禁止项）、治理文档显式入 allowedFiles（CORE-T01 教训）、interUnit 必填＋knownPitfalls 纯 ID（校验器句法 O-nn/CR-nn/P-XX-nn）；
- **机器校验**：validate-task 三查 42/42 通过＋validate-docs PASS＋validate-state PASS（真读校验面）。

## 2. 放行判定（acceptance 第 2 条）

**ready 翻转 4 份**（前置已 done 且接口交接为卡内闭环，无跨卡未决项）：

| 任务 | 前置 | 翻转依据 |
| --- | --- | --- |
| CORE-T02（身份与摘要） | CORE-T01 done | §4.1/§4.2 接口全在 core 卡内冻结；CR-02 落位即权威 |
| CORE-T09（约定锁定与值语义测试） | CORE-T01 done | 纯卡内自测（§4.6/§8），无跨单元接口产出 |
| TK-T02（JsonLite） | TK-T01 done | §4.1 接口卡内冻结；P-TK-6 处置约束＝D-02 边界 |
| TK-T09（故障注入原语） | TK-T01 done | §6.4 接口卡内冻结；O-33 处置约束＝ProcessRunner 头冻结不实现 |

**保持 planned 38 份**——原因登记：除上述 4 份外，全部任务的技术前置（dependsOn）含至少一份未完成任务（如 EV-T02+ 依赖 EV-T01〔ready 未 done〕、RT-T02+ 依赖 RT-T01、POL-T02+ 依赖 POL-T01、TK-T03+ 依赖 TK-T02 等），逐份前置链核对于契约 dependsOn 字段（机器可查）；**无 blocked 项**（无不可满足前置或登记性阻断）。

**CR-06 门禁原样（acceptance 第 3 条）**：CORE-T04/05、TK-T04/05 四份契约本次**零触碰**（本就不在缺字段清单——其 CR-06 门禁条目原样保留，API diff 未执行前置不消除）。

## 3. knownPitfalls 处置约束（纯 ID 句法的配套说明，CCP"每条附一句"的承载处）

| ID | 消费任务 | 处置约束 |
| --- | --- | --- |
| CR-01 | CORE-T03、EV 系 | core 公共类型语义变更走 foundation-api-diff 冻结流程，消费卡增量修订联动 |
| CR-02 | CORE-T02、POL-T03、RT-T04 | 摘要算法唯一实现归 core——消费方编码器声明排除字段、不另写摘要 |
| CR-04 | POL-T05 | 经 IPolicyNameContext/CollisionScene 消费 runtime，禁止直依赖 runtime 库 |
| CR-05 | RT-T04、RT-T10 | CanonicalModel 身份直接消费，禁止 evidence/runtime 重编码 |
| P-D-1 | CORE-T08 | 四词表跨卡确认（O-24）未闭合——事件载荷签名不私改 |
| P-EV-1/-3/-4 | EV-T02/03/05/06/08/09/10/11 | core Draft 基线按卡内签名实现，冻结 diff 后增量同步；汇总顺序按 §8.1 表 2 字面；"不可判定"不新增第三持久态 |
| P-EX-7 | EV-T10 | 运行期能力声明承载位置未决——descriptor 不上收执行能力字段 |
| P-POL-2/-4/-5 | POL-T02/07/08/09 | 附录 D 外阈值 nullopt 显式不适用；Draft 基线增量同步；后端版本暂取构建版本留痕 |
| P-PR-6/P-EX-8 | POL-T10 | sink 形态统一待 diagnostics 裁决，码值权威归 StableCodeRegistry |
| P-RT-1/-3/-4/-7 | RT-T02~T08、RT-T10/T11 | core Draft 基线；L1 依赖集确认前不扩散；预设轴向维持单侧定义待 modeling 核对；病态阈值随诊断输出实际条件数 |
| P-TK-2/-6/-7 | TK-T03/06/07/08/10、TK-T02 | core 类型冻结前按卡内基线；JsonLite 不用于产品格式；lint 字典需求所有者复核 |
| O-12 | RT-T05 | R-4 例外措辞确认前，前缀操作仅限名称解析器模块（清单随 RT-T13 提交登记册） |
| O-13 | EV-T05/06 | 证据词表/汇总口径为需求侧待裁决——按保守字面实现 |
| O-33 | TK-T09 | ProcessRunner 仅头文件冻结，不随本任务实现 |

## 4. 同步核验（acceptance 第 4 条）

- validate-docs PASS（20 units、12 trace entries、139 task files）；42 份逐份 validate-task 三查 PASS；
- phase-one-readiness §2"基础任务契约完整性"行已同步（42 空→0；ready 3→7）；
- 追踪矩阵：需求级覆盖（DTB §3，180＋12＋2）不受本任务影响（零需求语义修改）；任务级追溯载体＝契约 requirements 字段＋本记录＋编译留痕 42 行；
- RT-T08/RT-T11 陷阱选择说明：原拟 SA-02/MDL-06（需求/决策号，非登记册 ID）不合规法句法，换 P-RT-3（rwsim/L1 依赖集合法性——同一处置约束：API 缺项走补丁登记、依赖集不扩散）。

## 5. 遗留与建议

- 10 份"先行补齐"契约（立项时 52 与现况 42 的差值）非本任务产物，其质量以各自产出时的治理留痕为准（compile-log 既有行）；
- 建议级发现：无新增（契约句法与卡行一致性由三查＋本记录承载；governance-log 的 P-*/O-* 状态与 knownPitfalls 引用一致性已核对——引用 ID 均在册）。
