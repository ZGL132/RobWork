# 契约编译协议（contract-compilation）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.1（2026-09-10 二轮审查修复：interUnit/branch 必填化、留痕册建册补录） |
| 文档代号 | CCP |
| 上游 | development-task-breakdown.md §2/§8（任务行登记与契约家族归属）、units/&lt;unit&gt;.md 任务拆分表（编译源）、acceptance-protocol.md §2（评审独立性）、automation-pipeline.md §0（阅读阶梯的前提） |
| 适用范围 | `tasks/` 与 `tasks/foundation/` 下一切执行契约的 planned→ready 编译，及 ready 契约的后续修订 |
| 留痕载体 | 编译评审结论追加登记 `traceability/contract-compile-log.md`（一行一契约；v1.1 建册并补录既有 10 份 ready/done 契约——含 branch/interUnit/knownPitfalls 逐份判定） |

## 0. 为什么需要本协议

自动化流水线（PIPE）的全部核心机制——阅读阶梯按 designRefs 锚点定向阅读、防偷懒按 acceptance 逐条自证、验收按逐条证据表裁决——都以**"契约是单元卡的忠实编译产物"**为前提。现状是 150 份契约中绝大多数为占位（planned，空 requirements、泛化 acceptance、无锚点 designRefs、占位 title），而 planned→ready 的编译环节此前没有协议：谁编译、质量标准、是否评审均无载体。契约质量是实现的精度上限，本协议补上该环节（PIPE v1.3 审核结论 P0-1）。

## 1. 角色与时机

- **编译者**＝治理会话（所有者委托）。独立性约束：编译者不得担任该契约的实施者；§3 第 6 步的评审者不得与编译者为同一会话。
- **时机**：任务进入流水线 queue 之前必须已 ready（PIPE §3 守卫 4 的机器前提）；所有者下达"调整队列"口令前，应确认拟入队契约已按本协议编译放行。
- **首个适用对象**＝DOC-T03（foundation 52 份 planned 契约逐份回填放行）；后续批次（project/execution/io/ui/reporting 等 72 份，以及 modeling/kinematics 等 14 单元编卡后的新契约）一律同规范。

## 2. 编译产物质量标准（逐字段 ready 门槛）

| 字段 | ready 门槛 |
| --- | --- |
| `taskId` / `unit` / `masterWp` | 沿用卡内编号与 ≙ 映射（DTB §8 唯一编号来源），不双轨 |
| `title` | 动宾业务摘要；**禁止** "XX-Tnn implementation slice" 式占位文案 |
| `requirements` | 非空；每条为 REQUIREMENTS 现行真实 ID（可附 AT 编号）；空数组即不得 ready |
| `dependsOn` | 只编码**真实技术前置**（目标存在/公共头可用/CR 门禁未关闭等）；"建议先做"之类的顺序偏好归 queue 严格次序承载，不进 dependsOn。机器校验：引用必须存在（validate-task ①） |
| `designRefs` | 每条 `path#锚点`：锚点指向卡内任务行所在节＋直接支撑实现的接口/数据模型节；`traceability/**` 等过程留痕文件可整读、不强制锚点。机器校验：设计文档锚点必须真实存在（validate-task ②）——锚点失真＝实施者读错章节＝静默实现偏差 |
| `allowedFiles` / `forbiddenFiles` | 允许面精确到目录 `/**` 或逐文件；**需要同步的治理文档必须显式列入 allowedFiles**（CORE-T01 教训：allowedFiles 不含文档路径，留痕被迫拆提交）；forbiddenFiles 至少含上游需求/架构正文 |
| `verify` | 每条可在仓库根独立执行、结论二元可判；脚本调用口径见 DTB §5.1 |
| `branch` | 任务分支名（v1.1 必填，ready 门槛）：`wp<nn>-t<kk>`（单元任务按 ≙ 映射，如 TK-T01→wp02-t01）或治理族 `doc-t<nn>` 等 `<前缀>-t<序号>` 小写句法；PIPE queue 结构化条目与本字段同名同值（唯一来源在此，queue 只携带指针） |
| `interUnit` | **必填 bool（v1.1 收紧：缺字段即校验失败，不再容忍省略）**：任务是否消费/产出跨单元公共接口——含构建层建立单元间依赖边（如 T01 落位建立 unit→core 边，T-2 白名单边也算跨单元交互）；纯单单元/纯治理任务为 false。判定依据须能在编译评审中陈述（见 traceability/contract-compile-log.md 补录批次的逐份判定先例） |
| `acceptance` | **逐条可独立验证**：每条映射卡内 UT 编号/具名测试、可复现命令或可核对产物形态，条目数与卡内任务行"完成条件"对齐；**禁止**"实现本文对应任务行定义的接口和不变量"式泛化文案 |
| `interUnit=true 时` | `knownPitfalls` 必填：与本任务接口/数据相关的已登记语义陷阱（单元卡 §10.2 类 P-xx、`traceability/foundation-contract-review.md` 的 CR-xx、DTB §4 的 O-xx），每条附一句处置约束或 blocked 触发条件。机器校验 ID 句法（validate-task ③）。**陷阱随契约走**——阅读阶梯禁止通读，实施者不该被指望自己翻三处登记册；interUnit=false 时可自愿登记（如 WP-01-T01 的 O-21/O-12） |
| `note` | 编译决策、豁免依据、与卡行的既登记偏差 |

## 3. 编译步骤（七步）

1. **读源**：卡内该任务行（§9/§12 类任务拆分表）＋其"输入"列锚定章节＋"产出"列文件清单。
2. **汇总陷阱**：从三处登记册（单元卡 §10.2、foundation-contract-review.md、DTB §4）筛出与本任务相关的条目——这一步的产物就是 `knownPitfalls` 与 blocked 触发条件。
3. **起草**：按 §2 逐字段成文；acceptance 从卡行"完成条件"逐条展开并补测试映射。
4. **自检**：§2 表逐字段核对＋四个禁止（空 requirements、泛化 acceptance、设计文档无锚点、占位 title）。
5. **机器校验**：`validate-task.ps1` 三查全过＋`validate-docs.ps1` 通过。
6. **独立评审**：另一会话对抗式核对"卡行↔契约"逐字段一致性与可验证性（独立性参照 ACC §2：全新上下文、只比产物、为不通过找证据）；结论追加登记 `traceability/contract-compile-log.md`；不通过→回到第 3 步。
7. **放行**：status→ready；由所有者确认入队（state.json `queue` 或"调整队列"口令）。

## 4. 编卡任务（T01）的产物质量标准

"编写单元任务卡"类任务（先例 DIAG-T01、UI-T01，后续 12 张卡同）的完成标准＝**卡通过同款评审**：跨卡一致性（contract-review 流程，先例 traceability/foundation-contract-review.md）、引用锚点真实存在、**任务行可按本协议编译**（每行具备输入/产出/依赖/验证方式/完成条件五要素）。卡不可编译＝编卡任务未完成——这是防止后期 14 张卡质量参差、契约编译集体返工的前置闸。

## 5. 修订与缺陷处置

- ready 契约的修订＝治理增量修订：同一提交内更新契约本体＋`contract-compile-log.md` 追加一行修订说明；**实施者不得私改契约**。
- 实施/验收中发现契约缺陷（acceptance 不可验证、designRefs 锚点失真、范围缺失、陷阱漏登）→ 任务转 `blocked`＋登记 DTB §4 或 `traceability/findings.json`，修订契约并重新走 §3 第 5~7 步后方可继续——不得在代码中自行解释（DTB §8 既定纪律）。

## 6. 变更记录

| 版本 | 日期 | 说明 |
| --- | --- | --- |
| v1.0 | 2026-09-10 | 随流水线 v1.3 审核修复建立：编译角色与时机、逐字段 ready 门槛（含 interUnit/knownPitfalls"陷阱随契约走"）、七步编译与独立评审、编卡任务质量标准、契约缺陷处置路径 |
| v1.1 | 2026-09-10 | 二轮审查修复（P1-4：门槛此前实际未生效）：①interUnit 由"建议 bool"收紧为 ready/done 必填 bool（validate-task 缺字段即失败——此前 139/139 份契约集体缺失等于守卫被绕空）；②新增 branch 必填字段（PIPE v1.4 结构化 queue 的唯一来源）；③traceability/contract-compile-log.md 建册，既有 10 份 ready/done 契约按补录批次登记（branch/interUnit/knownPitfalls 逐份判定留痕），后续编译不再有补录 |
