# REUSE-T57 — RC-10 可先行面登记批（§13.5.4，2026-10-28）

起始 SHA＝`70660c5b294dfb669067457a9a7c7fc95c4ed199`；分支 `redesign-main`；**零产品代码变更**（纯登记批：functions 矩阵 8 文件＋本目录＋changes/tasks 凭证＋migration 注记）。

任务契约＝[tasks/REUSE-T57.json](../../../tasks/REUSE-T57.json)；变更凭证＝[reuse/changes/REUSE-T57.json](../../changes/REUSE-T57.json)（files=[]，零产品文件变动如实登记）。状态：实施完成待独验（status=ready，独验 NOT RUN 待所有者安排，实施者不自签）。

## 1. 登记依据与方法

RC-10（migration.md §13.5.4 :426）要求对剩余业务值、单位、诊断、事务、策略、worker/总线、独立 oracle **逐行消费既有检索/非重叠证据**，并准确表达「应保留」与「尚未审计」。本批方法：

1. **逐行读现状**：对 8 个矩阵的全部 in-scope pending 行读 search.command/search.conclusion/differences/auditEvidence/auditBoundary；
2. **绑定行内既有证据**：登记行的 nonOverlapEvidence 优先绑定该行 auditEvidence 已引用的专项审计文档或对应独验记录——210 行属此形态；仅 4 个无专项审计文档的行（execution.TaskScheduler.submit、execution.WorkerSupervisor.spawnNewWorker、project.ObjectStore.publishObject、project.ObjectStore.readVerified）按 T46 先例新建 [nonoverlap-retention.json](nonoverlap-retention.json)（族判定＋行差异引用＋本批实测复跑检索）承载绑定；
3. **sha256 实算**：全部绑定按 reuse-contract.ps1 Digest 口径（剥 BOM＋CRLF→LF＋尾换行）对实际文件计算，Node 与 pwsh 双运行时复算 11 个绑定目标逐一一致（防 T33 final.log 双运行时摘要二象教训复发）；
4. **状态口径**：decision=business-specific＋status=audited（RC-06/T56 SVD 面与 T32 rotationAngleBetween 先例同型；不用 migrated——本批无上游替换、无生产证据四象限要求）。cap=NONE 行 decision∈{business-specific,pending,proposal}（reuse-contract.ps1:232）合规满足。

## 2. 登记分布（214 行 business-specific/audited）

| 组 | 行数 | nonOverlapEvidence 绑定 | 依据要点 |
| --- | --- | --- | --- |
| TEST-HARNESS（core 1/execution 1/io 2/kinematics 2/modeling 2/policy 1） | 9 | traceability/acceptance/REUSE-T32-20261025-r1.json（b77e059b…） | migration.md:272 T32 定案：上游 ADD_RW_GTEST 非 CONFIG 导出，20 单元同形薄宏为既有形态；只共享必要产品 CMake include、不搬运上游未导出宏；TEST-HARNESS capability 保持 candidate（零 catalog 变更）。独验记录内含「ird_add_gtest business-specific 定案逐项实测」 |
| diagnostics（b33 解析/去重/注册工厂链 6＋b34 注册/清单 29） | 35 | reuse/diagnostics-schema-audit.md（b042a492…）×6；reuse/diagnostics-registry-manifest-audit.md（3c27191a…）×29 | 稳定码/文案键/去重权威归 diagnostics（架构红线）；上游 PropertyTree 词法/类型差异已核不替代受限文法；T01 batch33/34 真实验证在案 |
| core Units（UTIL-UNITS 生产行） | 28 | reuse/core-units-compare-audit.md（453529fa…） | 量纲/冻结 token 由 core 权威承载（架构红线）；最小 math 常量候选与产品契约分开；C4 反例/F-966 在册为独立事项 |
| core b28 值契约（Provenance 16/DiagData 7/Evaluation 8/Events.cpp 11） | 42 | reuse/core-identity-events-audit.md（3316b734…） | SourcedValue 四态/词表为 core 公共契约，上游无同义功能（审计明示「身份文本解析、保留值和强类型不是上游 StateStructure 局部编号的同义功能」）；F-965（FNV）与 methodTag 文档矛盾为独立在册事项，不影响非重叠判定；其中 2 行行内另引 core-event-lifetime-audit，两文档均在各行 auditEvidence 保持 |
| core b32（Events.hpp 9/DiagData.hpp 8/Provenance.hpp 1） | 18 | reuse/core-event-lifetime-audit.md（39062222…） | 值契约/稳定码/evidence 权威不下放；泛型 noexcept 终止仅观察 |
| core CORE-EVENT 5＋execution EventBus 11 | 16 | traceability/acceptance/REUSE-T41-20261026-r1.json（977cf429…） | T41 落地批（migration.md:316）：生产装配换核 execution::DomainEventBusImpl、越界 core::ReferenceEventBus 产品使用点归零、投递线程转接互斥＋关停 drain 序，独验 PASS；core 类本体保留为测试语义钉（值/事件契约权威不下放） |
| execution Admission | 13 | traceability/reuse/REUSE-T01/execution-admission-audit.md（e3d5d385…） | 受理/登记编排为 execution 权威；core 工厂/稳定码复用已查 |
| execution Scheduler/WorkerSupervisor | 2 | traceability/reuse/REUSE-T57/nonoverlap-retention.json（9edc27f0…） | S1（ThreadPool/ThreadSafeQueue 全部线程原语面）＋S2（上游全源码 CreateProcess/JOB_OBJECT/命名管道零命中）2026-10-28 实测；T06 分类 class A（独验 PASS） |
| policy（PolicyParsing 17/JointLimits 7） | 24 | traceability/reuse/REUSE-T01/policy-boundary-audit.md（0ced2154…） | 共享策略归 policy 红线；闭区间原语不替代业务裕量。CollisionQuery 2 行见 §3（IRD-GATE-AD 权限墙，回退 pending） |
| policy CMake | 1 | REUSE-T32 独验记录 | 同 TEST-HARNESS 组 |
| project（AtomicFile 16/Archive 5/其余 4） | 25 | traceability/reuse/REUSE-T01/project-transaction-audit.md（96b22033…） | project 写协议权威（CAS 发布/摘要核验/原子协议/fsync 防覆盖）；T40 落地批实链回归（project 287/287＋B20 三反例，独验 PASS） |
| project ObjectStore | 2 | REUSE-T57/nonoverlap-retention.json | S3（IOUtil 仅底层工具）实测＋T06 分类＋T40 实链 |
| io/kinematics/modeling 各 2 | 6 | REUSE-T32 独验记录 | 同 TEST-HARNESS 组 |

绑定 sha256（Digest 口径实算，双运行时复算一致）：

| 绑定文件 | sha256 |
| --- | --- |
| traceability/acceptance/REUSE-T32-20261025-r1.json | b77e059bd7b09e7595638f05825eef662a442671fb5ca7f7f0e49d2ff7958ea5 |
| traceability/acceptance/REUSE-T41-20261026-r1.json | 977cf429eecee963e98783837ad3d8e3b1735bd988a17428c992b04971fa2c3a |
| reuse/diagnostics-schema-audit.md | b042a49211ecbd28e4ea76c646514a15d8f52cdad12d04464b73cfcf9bc946cb |
| reuse/diagnostics-registry-manifest-audit.md | 3c27191a70c91691a4054f80f7c043d0a8221bd7d02f06e616beecadafe677d0 |
| reuse/core-units-compare-audit.md | 453529fa76f91a91d1e0fb1f0f1dfe803d57441ae25e77830591cf5ffff5686b |
| reuse/core-identity-events-audit.md | 3316b734813dc16d907f3d2c7ae089a570e0b3be7be586c12090974d45dedf1a |
| reuse/core-event-lifetime-audit.md | 3906222209bb19cbc2bbf36c350de610b50a524f572989f4b81f615fa587b87f |
| traceability/reuse/REUSE-T01/execution-admission-audit.md | e3d5d385565e76f9cc00cfb814bc5d3654211ef79eca2289632b3d0422f55a4e |
| traceability/reuse/REUSE-T01/policy-boundary-audit.md | 0ced21540e885251e3a0e0e5f1693eb9a951438d6c8313f977fda79b41e68822 |
| traceability/reuse/REUSE-T01/project-transaction-audit.md | 96b22033f7fa61cfc3f6de54c0f797c0cb15ca080646a09222017ddc3e767286 |
| traceability/reuse/REUSE-T57/nonoverlap-retention.json | 9edc27f08e4e6dc9f692285a3a683e4eef4c8c957b1e65da0b3beb2f3e28b597 |

## 3. 保持 pending 的 7 行（不包装）

以下行的行内记录自带未决标记，且该未决项未被任何后续独验记录消费——按「证据不足→保持 pending 并在 search.conclusion 写明缺口」登记：

| 行 | 未决标记（原文） | 缺口 |
| --- | --- | --- |
| policy.JointLimits.(anonymous namespace).makeJointDiagnostic | 「context/cause/action为本TU中文文本，按SA-12/19须核与diagnostics正式模板职责，**不能判已合规**」 | 正式文案收口 diagnostics（SA-12/ARCH §3.6.3）未核 |
| policy.PolicyParsing.(anonymous namespace).makeErrorDiag | 「正式中文文案仍由调用点注入，**需与diagnostics权威分清**」 | 同上 |
| policy.PolicyParsing.(anonymous namespace).domainWindowText | 「重复域窗中文描述；**应保留结构化domain/界值由diagnostics渲染**」 | 同上 |
| policy.PolicyParsing.(anonymous namespace).levelText | 「中文级别文本在policy本地，SA-12要求正式文案收口，**待真实消费核对**」 | 同上 |
| execution.admissionDiag | 「core工厂强制必填，**稳定码/中文文案须按diagnostics权威继续核**，不因用了工厂视为全合规」 | 同上 |
| policy.CollisionEvaluationSession.evaluate | 本批登记尝试实测：行脱离 pending 审计豁免即触发 **IRD-GATE-AD**（validate-reuse 首轮实测失败：CollisionQuery.cpp : restricted upstream use needs exact file/target/operation grant） | permissions.json 无 CollisionQuery.cpp 精确 grant（登记扫描 categories=state/operations=write-state）；权限最小方案归 RC-09 逐文件审查，本批不代授 |
| policy.CollisionQuery.CollisionEvaluationSession.buildEvaluationHalf | 同上（同文件；扫描 categories=model,state） | 同上 |

前 5 行：REUSE-T01 B18 审计 :17 对文案族行的自身处方即「因此这些行保留 pending，后续须追到 diagnostics 的正式模板、显示消费者和已有转换接口」——本批遵从该处方，不越权代为收口。后 2 行（CollisionQuery.cpp）：本批实施发现——该文件的受限上游消费（rw/proximity）此前一直依赖行级 pending 审计豁免运行而从未持有 grant，任何非 pending 登记都会被 IRD-GATE-AD 拒绝；按「登记批不代授权限」边界回退 pending（evaluate 行的 S4/B18/T36 结论刷新保留，追加缺口说明），并把权限最小方案列为 RC-09 输入。此 7 行构成 RC-10 面内剩余缺口，归后续精确子任务/所有者裁决。

## 4. 未触面（行级排除，如实登记）

- **core Digest 23 行**（audit-b30 22 行＋ContentDigester.compressBlock）：GAP-DIGEST-01 未批准，禁触（ask 明令）；
- **core UnitsTest 8 行**：独立测试 oracle 行，不在前哨盘点 28 行口径内，保持 pending；
- **core.audit-b32.CoreError.inheritedConstructors**：不在前哨清单，保持 pending；其 differences 的 F-967 滞后字样随行保留原文（登记仍欠，随该行后续登记批处置）；
- **execution/policy UTIL-BINARCHIVE 42 行**（36＋6）：RC-07 字节原语能力批范围；
- **kinematics/modeling 其余 pending 行**（MATH-ROT 族/DH/XML/随机采样等 60 行）：RC-05/后续精确子任务范围；
- **catalog.json / permissions.json / closures.json / 产品源码全树**：零触碰（禁触面）。

## 5. 随批文本处置

- **F-967 滞后字样刷新（36 行）**：T06 统计快照行明示「发现项 CORE-EVENT 16 行 differences 文本 F-967 滞后字样（已 fixed）留登记批处置不属本包授权面」。实测该字样（「F967句柄析构违约及生产参考总线越界待改」）实际分布于 37 行 pending（超出 T06 记录的 16 行 CORE-EVENT 面：core 26 行＋execution 11 行，含 b32 值契约行与 2 行 b28 Provenance 行的批量同文）。本批对其中 36 行登记行按已 fixed 事实刷新（execution EventBus 11 行注明本总线即换核后实现；core CORE-EVENT 5 行注明生产装配归零；其余 20 行注明独验 PASS）；CoreError 行不在范围保留原文。
- **结论刷新（38 行）**：9 行 TEST-HARNESS（「未完成迁移验收」→T32 定案口径）、execution TaskScheduler/WorkerSupervisor（「未完成…替代排除审计」→S1/S2＋T06 分类收口）、project ObjectStore 2＋batch20 25 行（「待调用链与语义完整审查」→S3＋T06 分类＋T40 实链收口）、policy evaluate（→S4＋B18 :27＋T36 review）。原结论全文见 git 历史；新结论逐条对应本 README §2 依据要点。

## 6. 本批实测检索（S1—S4，2026-10-28 对当前 HEAD）

| id | 命令 | 结果 |
| --- | --- | --- |
| S1 | `rg -n "class ThreadPool\|addWork\|stop \(…"` （rw/common/ThreadPool.hpp＋ThreadSafeQueue.hpp） | 命中即上游全部线程池原语面；无优先级通道/受理预算（exit 0） |
| S2 | `rg -ln "CreateProcess\|JOB_OBJECT\|CreateNamedPipe\|ConnectNamedPipe\|WaitNamedPipe" RobWork/RobWork/src RobWork/RobWorkSim/src` | **零命中（exit 1）**——上游无进程隔离/IPC 设施 |
| S3 | `rg -n "readFile\|getFilesInFolder\|getFileSize\|atomic\|publish\|fsync\|digest"` （IOUtil.hpp/.cpp） | 仅底层读写工具；无原子发布/fsync/摘要核验契约 |
| S4 | `rg -n "inCollision \(…\|distance \(…\|createModel\|class ProximityStrategyRW"` （proximity 三头） | 上游策略 API 消费面确认；策略选择/过滤/覆盖为 policy 域语义 |

完整命令与逐条结果登记于 [nonoverlap-retention.json](nonoverlap-retention.json)。

## 7. 验证记录

- 登记脚本内预校验：全部登记行 sources 区段 Digest 重算与矩阵登记值逐一相等、symbol 命中区段（零失败）；
- JSON 逐文件重解析通过；块格式终检零缺陷（wrapper 缩进与 status 行一致、块后无空行，214/214）；
- **validate-reuse.ps1 全量：PASS（exit 0，functions=2267 closedFiles=1；`validate-reuse: PASS governance only; inventory=1401 units=20 changed=170 DEBT=16 UNRESOLVED=1399`）**。首轮曾 FAIL 于 policy/src/CollisionQuery.cpp IRD-GATE-AD（本批发现的真实权限墙），两行回退 pending 后复跑 PASS；其余 REUSE-AD-PENDING 行为 pending 审计豁免的信息性登记（ui/reporting/runtime Snapshot 既有债，非本批引入）；
- **validate-docs.ps1 -TaskFile tasks/REUSE-T57.json：PASS（validate-task PASS 1 tasks；`validate-docs: PASS (20 units, 12 trace entries, 434 task files, 30 registered assets, 6 notes, 7 skills, 2 packs, NOT AUDITED evidence)`，内含一次全量复用检查）**；
- NOT RUN：独立验收（所有者安排）；commit-lint/verify-task-scope（随提交批显式 -Base 70660c5b2 -Head <送验SHA>）；C++ 构建/ird_gates/测试执行（零产品代码/CMake/公共接口变更，按 AGENTS §4 增量范围无受影响构建目标）。
