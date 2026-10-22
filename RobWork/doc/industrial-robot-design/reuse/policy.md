# 上游复用硬约束与执行协议

本文件是 AGENTS.md §0.1 的操作协议；硬约束原文唯一维护于根 AGENTS.md。依据为所有者 2026-10-22 的 H1—H6 指令及“请执行治理，交付并验收‘文档＋约束＋门禁’治理变更，验收通过后再进入适配层和算法替换”。本次批准覆盖治理实施及独立验收，不是新增核心算法或上游补丁的批准。

## 1. 不可跳过的顺序

1. 从根 AGENTS.md 进入，只读本任务涉及的协议章节、migration 阶段、catalog/inventory 条目和契约；全量库存交机器检查，不默认加载到上下文。
2. 先按 ARCHITECTURE §3.6 / SA-19 确认对象/操作权限，再按需求分批检索 RobWork/RobWorkSim 的源码、公共头、构建目标、测试、示例和文档，反查产品隐藏实现并作适用性探测；记录命令、范围、版本、具体符号和适用边界。找不到一次不等于没有能力。
3. 按直接 API → 薄封装/适配 → 最小上游扩展 → 新实现选择。后两项须先提交 extensions/ 中的完整提案并取得所有者明确批准。
4. 产品变更任务须有 reuse 字段及 changes/<taskId>.json；所有产品源码/测试/CMake 变动逐文件绑定内容摘要、能力、方式及验证命令。补强后的逐功能矩阵、catalog 格式与 direct/adapter 生产正向证据按 [audit-contract.md](audit-contract.md)；该补强验收前执行现有文件凭证，不能据此宣称语义审计通过。普通文档维护不额外编制产品变更凭证。
5. 按根 AGENTS.md §4 执行相关验证并提交推送；复用迁移保留双模式验证、独立验收及不可变记录，所有者决定合入。普通文档维护无需算法迁移的全部流程，实施者不能自签批准。

## 2. 基线不是豁免

catalog.json 的 baselineCommit 固定为 b12e8503cc1c2a1acc040f506842a02e126b9515。inventory.json 按 Git 对象登记全部 20 单元的 C/C++/模板/脚本/含代码页面/CMake 文件，含测试；这是文件级覆盖清单，不冒充每个函数均已完成人工语义审计。

classification 为 migration-debt 的文件已有重复证据；audit-required 是尚待语义审计；reuse-candidate 只有候选来源，未证明等价；existing-adapter 是已观察到的适配路径。未变化的基线允许治理/构建检查执行，但输出 DEBT/AUDIT 数量；不把存量合规性报告为 PASS。变动文件必须有逐文件复用记录，移除或修改重复内核不得增加另一条核心路径。最终 -RequireMigrationComplete 遇任何债务或未审计项必须失败。

不得移动基线、删除库存条目、用一条目录通配掩盖新文件、把核心算法改名后申报为业务逻辑，或以冒烟模式缺库为理由新写计算内核。关键词检查是启发式，语义重复由独立审查补足。

## 3. 类型、权威与生命周期

权限唯一维护于 [ARCHITECTURE §3.6 / SA-19](../ARCHITECTURE.md#36-上游使用权限与纯净性sa-19)：纯值直接用、受控模型视图与任务私有会话、宿主呈现精确例外分别审查；runtime 是模型 owner，不能把“使用 rw 类型”解释为任意构造权。业务 DTO、稳定诊断码、证据与命令端口保留，仅在必要边界使用私有薄适配器。runtime 维护名称、编译模型、关节/TCP 映射、权威角与 q_rw 转换；policy 维护共享策略。不扩大业务单元互链，不跨私有头，L2 零 Qt。

evidence 的 EvaluationOutput/aggregateVerdict/ResultEnvelope 与 diagnostics 的注册表/文案权威、worker 进程边界均按 SA-19；不得在业务或 adapter 中另写 envelope/码表或把宿主 WorkCell 作为计算真值。直接 API 可满足契约时不强加 wrapper；不建全库 facade、第二模型层或通用 backend 框架。

已发布快照不可变；State、IK 求解器、仿真引擎按任务/线程独占。必须明确 m/rad/kg/N/N·m、坐标系、所有权、载荷和摩擦唯一计入点。公共接口按契约版本迁移，不直接插入虚表并声称 ABI 不变。上游抛错须转译，不伪造成功、迭代数、取消响应或能力位。

## 4. 批准与证据

扩展提案至少含缺口检索命令与结果、已读文件/符号、不能适配的反例、候选上游扩展点、直接复用/适配替代方案、API/ABI/数值/性能/构建/许可证影响、测试、补丁关闭和回滚方式。批准记录须逐字引用所有者的明确授权，限定功能、提案文件和内容 SHA-256；实施者不能填一个 approved=true 代替批准。批准本治理计划不批准任何扩展提案。

变更记录中 extension/new-core 方式必须引用批准和提案；validate-reuse 核验结构、范围、摘要与引用。批准是否确由所有者作出仍须独立审查及 PR 审阅，JSON/Markdown 本身不提供身份认证。

构建/测试证据按 traceability 规则只增不改，含命令、退出码、passed/skipped/failed、gtest XML、ird-test-report.json；PowerShell 门禁自测单列 JUnit XML＋JSON，不冒充 gtest。适配对照不能只与旧内核互证，必须保留独立解析 oracle 及上游直接调用。

## 5. 阶段门与故障处理

治理任务 GOV-T21 的验收对象为冻结 SHA。后续 REUSE 任务在进入 ready 前，必须登记 governanceAcceptance（验收记录提交、路径、被验收 SHA）；门禁读取该 Git 对象核对 verdict=pass、taskId=GOV-T21 和 reviewedCommit，并检查被验收提交为当前祖先。未取得记录时后续任务保持 planned，不触碰计算内核。

每项能力缺口单独停在提案阶段，不缩小既有需求、放宽容差或启用旧内核兜底。独立验收失败先返工并对受影响范围完整重验、独立复验；不得自行合入 redesign-main。main 冻结、上游源码只读及 SA-02 补丁纪律继续有效。

## 6. 入口与门禁契约

- validate-reuse.ps1：全量文件库存、上游路径/符号/摘要、逐文件变更凭证、任务阶段门、重复核心指纹；默认核对当前工作树（含未跟踪产品文件），-Head 核对冻结 Git 提交而非工作区。
- test-reuse-governance.ps1：隔离 Git 仓库内验证正例和负例，输出可重跑的 JUnit/JSON；不修改实施工作树。
- validate-docs、validate-task、verify-task、gate-all、ird_gates、CI：调用同一校验器，不维护第二套规则。
- -RequireMigrationComplete：最终交付使用，债务和未审计文件均须关闭；治理阶段不宣称该命令通过。

## 7. 库存结项协议

库存是不可变历史事实，不能通过更改classification洗白。GOV-T22 实现的 RequireMigrationComplete 读取逐文件/逐功能关闭记录，而非永久锁死历史债务计数；无有效关闭记录仍须拒绝。格式与独立验收绑定见 audit-contract §7.1，最终使用仍须 REUSE-T06 冻结验收；不得简单清空库存或改基点。

后续语义结项须关联 functions/<unit>.json 的真实生产链、旧内核删除提交、有效正向证据与独立验收，不限于 16 个已知重复文件。catalog 的 candidate/reviewed/verified 是能力证据状态，不替代功能适用性和架构权限。完整补强契约见 audit-contract.md §3～§7。

现有 GOV-T21 pass 不自动验收本次补强；GOV-T22 的本地冻结版本已取得第三轮独立验收 pass，具体 Git 对象/冻结 SHA 见 tasks/GOV-T22.json。远程推送与 CI 尚未完成，不冒充远程交付。后续执行契约须增加 reuseGateAcceptance，引用该真实记录；REUSE-T01 全功能审计仍须完成并独立验收，才可放行 REUSE-T02。文档通过和格式夹具通过均不构成生产迁移通过。

审计存量时，固定基线中未变动的源码可以登记 decision 与 status 均为 pending/proposal 的功能，受限使用输出 REUSE-AD-PENDING；这只记录待审事实，不授予文件、目标或操作权限。自基线变动的文件、新文件及已解决记录仍受精确权限检查；最终模式拒绝这些未决项。不得为了让审计数据通过而把既有越权行为登记为合法 grant。

## 8. 验收返工GOV-T21/1

首轮冻结64ef0fd独立验收失败：CI干净检出夹具无变更可提交、.c与脚本/页面后缀遗漏。保留全部失败证据；返工扩大同一文件谓词与固定基线库存（原1370条不删除，补31条至1401）。自测夹具允许空提交，报告必须列expected/executed/notRun/driverFailure/complete；部分用例通过不能等同驱动或矩阵通过，JUnit驱动异常用error，不伪报skip。修复后冻结新SHA全量重验。

新产品文件若不属于已登记源码发现类型（含未知后缀、无后缀），默认REUSE-UNDECLARED；必须先通过治理变更登记类型和复用契约，不能将未知后缀当作豁免。基线非代码资产仍受既有文档/数据门禁和独立审查约束。
