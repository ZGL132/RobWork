# WP-00-T04 独立验证与评审记录

- Task ID：WP-00-T04（独立验证与评审）
- 执行日期：2026-08-30
- 执行时文档基线 commit：`ef23c93`（main HEAD；实现提交 SHA 于提交后补入完成报告）
- 前置状态：WP-00-T01（00379c0）、WP-00-T02（b7c3d6b7）、WP-00-T03（2aa63eb8）均已由独立治理提交登记 `Done`
- 评审上下文独立性：本评审执行上下文为 T01～T03 的独立验证/治理上下文，非 T01～T03 实现者上下文（T01～T03 实现提交分别产自其他实施会话），满足卡内"评审者与 T01～T03 执行者非同一执行上下文"
- 执行环境：Windows 11 专业版（NT 10.0.26200），Windows PowerShell 5.1.26100.9168 与 PowerShell 7.6.5（双 shell，详见 [t04-dual-shell-byte-compare.md](t04-dual-shell-byte-compare.md)），仓库根目录，无交互
- 结论：**评审通过**。128 项需求六环节链路机械全查无一断点；47 条逐前缀抽样（≥2/前缀）语义一致；ADR-001～005 与需求一致；双 shell 输出与 CSV 哈希一致。断点 0 项，观察 5 项（含继承 3 项），见 §6。

## 1. 评审方法

1. **机械全量校验**：以只读脚本对全部 128 项需求逐行校验六环节存在性与一致性（需求表行 → CSV 行字段 → 主包/支撑 WP 计划声明 Task ID → 模块方案认领 WP 并覆盖任务卡 → 任务卡文件存在 → test_case_ids 可在任务卡/WP 计划解析）。P0 覆盖 114/114＝100%，P1 14/14 同批覆盖。
2. **逐前缀人工语义抽样**：每前缀取排序首、尾 ID，P1 前缀追加首个 P1（确定性规则）；ERR/EVI 为单项前缀记并说明。共 47 条，逐条核对需求摘要、§16 追踪行、CSV 场景聚合与全部映射卡的第 1 字段需求声明（含范围展开），并反向回溯（卡 → WP → 模块方案 → CSV → 需求）。
3. **ADR-001～005 复核**：锚需求存在性机械核验＋决策语义与需求正文/符号注册表比对。
4. **门禁拦截验证（卡内 RED）**：复跑 `run-fixtures.ps1` 确认 8 夹具仍全部非零命中（见 §2）。

## 2. 精确命令复跑（仓库根，无交互）

命令 3（本文件记录）：

```text
$ powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\docs\industrial-robot-design\fixtures\wp-00\run-fixtures.ps1
clean-tree exit=0 pass=True
missing-requirement | exit=1 | keyword=数量错误
duplicate-requirement | exit=1 | keyword=IDs not unique
empty-acceptance | exit=1 | keyword=no acceptance trace
missing-work-package | exit=1 | keyword=unique detailed plan
stale-csv | exit=1 | keyword=CSV stale
invalid-release | exit=1 | keyword=invalid release
orphan-task | exit=1 | keyword=Task ID without card
table-shape | exit=1 | keyword=table separators
clean-tree-pass=True official-tree-hash-unchanged=True
official-tree-hash-before=84-ED-19-96-AB-DB-5A-C7-64-1E-2E-06-D5-C4-30-AA-94-45-63-8E-6A-7C-05-64-08-6E-29-1C-0F-69-E6-8A
official-tree-hash-after=84-ED-19-96-AB-DB-5A-C7-64-1E-2E-06-D5-C4-30-AA-94-45-63-8E-6A-7C-05-64-08-6E-29-1C-0F-69-E6-8A
ALL FIXTURES PASSED (non-zero exit + keyword + official tree unchanged)
exit code: 0
```

（官方树哈希与 T03 验证时 `4C-4B-…` 不同属预期：其后的治理提交 `ef23c93` 更新了 `agent-tasks/task-status.md`，全树哈希随之变化；本次运行前后一致即满足断言。）

命令 1/2（双 shell 门禁）原始输出、成功行逐字节对照与 CSV 哈希见 [t04-dual-shell-byte-compare.md](t04-dual-shell-byte-compare.md)：两 shell 均退出码 0、成功行一致、CSV 哈希 `0725D6EE…848C88` 前后不变。

## 3. 逐前缀抽样表（47 条）

图例：✓＝卡第 1 字段直接命名该需求；✓(范围)＝经范围声明覆盖；◇＝卡第 1 字段无 ID 回声，映射由 WP 计划/模块方案显式承载（观察 §6.1）；□正文＝卡第 1 字段以需求族/节号指称（如 NFR-COR 全族、§7.1）。

| 前缀 | 需求 ID | 优先级 | 主包 | 六环节判定 | 反向回溯（映射卡） | 语义 |
| --- | --- | --- | --- | --- | --- | --- |
| ARC | ARC-01 | P0 | WP-04 | 全通 | WP-04-T01✓ WP-04-T02✓ WP-04-T05✓ WP-03-T01✓ WP-03-T02✓ WP-03-T03✓ WP-03-T04✓ WP-03-T05✓ WP-14-T06✓ | 一致 |
| ARC | ARC-05 | P0 | WP-07 | 全通 | WP-07-T01✓ WP-07-T02✓ WP-07-T03✓ WP-07-T04✓ WP-07-T05✓ WP-03-T02✓(范围) WP-03-T03✓(范围) WP-03-T04✓(范围) WP-03-T05✓(范围) WP-15-T08✓ WP-20-T08✓ | 一致 |
| CON | CON-01 | P0 | WP-05 | 全通 | WP-05-T01✓ WP-05-T02✓ WP-05-T03✓ WP-05-T04✓ WP-04-T01✓ WP-04-T02✓ WP-04-T04✓ WP-04-T05✓ | 一致 |
| CON | CON-06 | P0 | WP-05 | 全通 | WP-05-T01✓ WP-05-T02✓ WP-05-T03✓ WP-05-T04✓ WP-06-T01✓ WP-06-T02✓ WP-06-T03✓ WP-06-T05✓ WP-07-T01✓ WP-07-T02✓ WP-07-T04✓ WP-07-T05✓ | 一致 |
| DEL | DEL-01 | P0 | WP-25 | L6 标签（§4.2） | WP-25-T05✓ | 一致 |
| DEL | DEL-02 | P0 | WP-25 | L6 标签（§4.2） | WP-25-T04✓ | 一致 |
| DYN | DYN-01 | P0 | WP-17 | 全通 | WP-17-T01✓ WP-17-T02✓ | 一致 |
| DYN | DYN-07 | P1 | WP-17 | 全通 | WP-17-T03◇ | 一致 |
| DYN | DYN-08 | P1 | WP-17 | 全通 | WP-17-T03◇ WP-17-T07✓(范围) | 一致 |
| ERR | ERR-01 | P0 | WP-09 | 全通（前缀仅 1 项） | WP-09-T01✓ WP-09-T02✓ WP-09-T03✓ WP-09-T05✓ | 一致 |
| EVI | EVI-01 | P0 | WP-05 | 全通（前缀仅 1 项） | WP-05-T02✓ WP-05-T03✓ WP-05-T04✓ WP-05-T05✓ WP-12-T01✓ WP-12-T02✓ WP-12-T03✓ WP-12-T04✓ WP-12-T05✓ WP-12-T06✓ | 一致 |
| KIN | KIN-01 | P0 | WP-15 | 全通 | WP-15-T01✓ WP-15-T03✓ | 一致 |
| KIN | KIN-07 | P1 | WP-15 | 全通 | WP-15-T07✓(范围) | 一致 |
| KIN | KIN-08 | P1 | WP-15 | 全通 | WP-15-T07✓(范围) | 一致 |
| MDL | MDL-01 | P0 | WP-13 | 全通 | WP-13-T01✓ WP-13-T02✓ WP-22-T06✓ | 一致 |
| MDL | MDL-07 | P1 | WP-13 | 全通 | WP-13-T05✓ WP-13-T08✓ | 一致 |
| MDL | MDL-14 | P0 | WP-13 | 全通 | WP-13-T06✓ WP-13-T07✓ WP-06-T01✓ WP-06-T02✓ WP-06-T04✓ | 一致 |
| NFR-COR | NFR-COR-01 | P0 | WP-02 | 全通 | WP-02-T01✓ WP-02-T02✓ WP-02-T03✓ WP-02-T04✓ WP-23-T01□正文 WP-23-T03□正文 WP-23-T05□正文 | 一致 |
| NFR-COR | NFR-COR-05 | P0 | WP-07 | 全通 | WP-07-T01✓ WP-07-T02✓ WP-07-T03✓ WP-07-T04✓ WP-06-T03✓ WP-06-T04✓ WP-15-T08✓ WP-20-T08✓ WP-23-T01□正文 WP-23-T03□正文 WP-23-T05□正文 | 一致 |
| NFR-DEP | NFR-DEP-01 | P0 | WP-24 | L4 判定（§4.1） | WP-24-T01✓ WP-24-T02✓ WP-01-T03✓ WP-01-T04✓ | 一致 |
| NFR-DEP | NFR-DEP-05 | P0 | WP-24 | L6 标签＋L4 判定（§4.1/§4.2） | WP-24-T03✓(范围) WP-24-T04✓ WP-01-T05✓ WP-17-T04✓ | 一致 |
| NFR-MNT | NFR-MNT-01 | P0 | WP-03 | L4 判定（§4.1） | WP-03-T02✓ WP-03-T05✓ WP-01-T02✓ WP-01-T03✓ | 一致 |
| NFR-MNT | NFR-MNT-07 | P0 | WP-01 | L4 判定（§4.1） | WP-01-T01✓ WP-06-T01✓ WP-06-T02✓ WP-06-T05✓ WP-07-T04✓ WP-07-T05✓ | 一致 |
| NFR-PERF | NFR-PERF-01 | P0 | WP-23 | 全通 | WP-23-T03✓ WP-10-T03✓ WP-10-T05✓ | 一致 |
| NFR-PERF | NFR-PERF-06 | P0 | WP-23 | 全通 | WP-23-T03✓(范围) WP-08-T05✓(范围) WP-21-T01✓(范围) WP-21-T02✓(范围) WP-21-T03✓(范围) WP-21-T04✓(范围) WP-21-T05✓(范围) WP-21-T06✓(范围) | 一致 |
| NFR-REL | NFR-REL-01 | P0 | WP-04 | 全通 | WP-04-T01✓ WP-04-T02✓ WP-04-T03✓ WP-04-T04✓ WP-23-T01□正文 WP-23-T05✓ | 一致 |
| NFR-REL | NFR-REL-05 | P0 | WP-09 | 全通 | WP-09-T01✓ WP-09-T03✓ WP-09-T04✓ WP-23-T01□正文 WP-23-T05✓(范围) | 一致 |
| NFR-SEC | NFR-SEC-01 | P0 | WP-11 | 全通 | WP-11-T01✓ WP-11-T02✓ WP-11-T03✓ WP-11-T04✓ WP-11-T05✓ | 一致 |
| NFR-SEC | NFR-SEC-06 | P1 | WP-24 | L6 标签（§4.2） | WP-24-T05✓ | 一致 |
| NFR-SEC | NFR-SEC-07 | P0 | WP-09 | 全通 | WP-09-T01✓ WP-09-T04✓ WP-09-T05✓ WP-12-T04✓ WP-12-T06✓ | 一致 |
| OPT | OPT-01 | P0 | WP-20 | 全通 | WP-20-T01✓ WP-20-T02✓ WP-21-T01✓ WP-21-T06✓ | 一致 |
| OPT | OPT-10 | P1 | WP-21 | 全通 | WP-21-T01✓ WP-21-T06✓(范围) | 一致 |
| PILOT | PILOT-01 | P0 | WP-25 | L6 标签（§4.2） | WP-25-T01✓ | 一致 |
| PILOT | PILOT-02 | P0 | WP-25 | L6 标签（§4.2） | WP-25-T02✓ | 一致 |
| REQ | REQ-01 | P0 | WP-14 | 全通 | WP-14-T01✓ WP-14-T03✓ | 一致 |
| REQ | REQ-07 | P1 | WP-14 | 全通 | WP-14-T01✓(范围) WP-14-T02✓ WP-14-T07✓ | 一致 |
| REQ | REQ-08 | P1 | WP-14 | 全通 | WP-14-T01✓(范围) | 一致 |
| SEL | SEL-01 | P0 | WP-19 | 全通 | WP-19-T01✓ WP-11-T01✓ WP-11-T02✓ WP-11-T03✓ WP-11-T04✓ WP-11-T05✓ | 一致 |
| SEL | SEL-07 | P1 | WP-19 | 全通 | WP-19-T03◇ | 一致 |
| SEL | SEL-09 | P0 | WP-19 | 全通 | WP-19-T04✓ | 一致 |
| TASK | TASK-01 | P0 | WP-08 | 全通 | WP-08-T01✓ WP-08-T02✓ WP-08-T03✓ WP-21-T03✓ WP-21-T06✓ WP-23-T02✓ | 一致 |
| TASK | TASK-03 | P0 | WP-08 | 全通 | WP-08-T01✓(范围) WP-08-T02✓(范围) WP-08-T03✓(范围) WP-21-T06✓(范围) WP-23-T02✓ | 一致 |
| TRJ | TRJ-01 | P0 | WP-16 | 全通 | WP-16-T01✓ WP-16-T06✓ | 一致 |
| TRJ | TRJ-07 | P1 | WP-16 | 全通 | WP-16-T05◇ WP-16-T07✓(范围) WP-10-T02◇ | 一致 |
| TRJ | TRJ-08 | P1 | WP-16 | 全通 | WP-16-T01✓(正文"后续交付"声明) | 一致 |
| UX | UX-01 | P0 | WP-10 | 全通 | WP-10-T01✓ WP-10-T02✓ WP-10-T03✓ WP-10-T06✓ WP-13-T08✓ WP-14-T07✓ WP-16-T07✓ WP-17-T07✓ WP-19-T07✓ WP-20-T07✓ WP-21-T07✓ WP-22-T01✓ WP-22-T02✓ WP-22-T04✓ WP-22-T05✓ WP-22-T06✓ WP-25-T03✓ | 一致 |
| UX | UX-08 | P0 | WP-07 | 全通 | WP-07-T01✓ WP-10-T01✓ WP-10-T02✓ WP-10-T04✓ WP-10-T06✓(范围) WP-13-T08✓ WP-14-T07✓ WP-16-T07✓(范围) WP-17-T07✓(范围) WP-19-T07✓(范围) WP-20-T07✓(范围) WP-21-T07✓(范围) WP-22-T05✓(范围) WP-25-T03✓(范围) | 一致 |

## 4. 机械全量校验汇总（128/128）

结果：六环节链路无断点；128 行中 19 行含两类需判定项，逐类判定如下，其余 109 行全绿。

### 4.1 L4：WP-01 无模块方案认领（9 行）——结构性不适用，非断点

涉 ARC-02、NFR-MNT-02/04/05/07、NFR-SEC-04（主包 WP-01）及 NFR-DEP-01/02/05（支撑卡含 WP-01）。判定依据（权威层次）：

1. 总纲（development-task-breakdown.md）§WP-01 行：WP-01＝"构建与依赖基线"（CMake 边界、Windows 构建、GitLab CI、依赖/API/许可证清单和静态规则），是工程基础层而非产品模块，主包需求即 ARC-02、NFR-MNT、NFR-DEP-05；
2. module-design/README 权威清单："全部 23 篇方案…（平台 10 篇＋业务 12 篇＋testkit）"，覆盖 WP-02～25，不含 WP-01（WP-00 治理层同理无模块方案）；
3. 门禁 `validate-development-docs.ps1`（退出码 0）的模块必需文件检查与此结构一致。

故模块方案环节对 WP-01（及 WP-00）链路**结构性不适用**：其语义承载链为 总纲 WP-01 行 → WP-01 计划（负责范围）→ 任务卡（边界测试/构建断言）→ 机器门禁（check-boundaries 等，NFR-MNT-07 的 old-plugin-dependency/widget-header/unregistered-library 即此类）。

### 4.2 L6：test_case_ids 为非代码验证方法标签（11 处/10 行）——全部可解析，非断点

WP-00 计划 §4 明确 test_case_ids 允许"真实测试名或非代码验证方法（取自任务卡）"。11 处标签逐项解析：

- `WP-24-T03 检查表首项`（NFR-DEP-03/04/05、NFR-MNT-06）：WP-24-T03 卡 RED 节原文"以检查表首项（六步记录齐备且逐步签署）作为'先失败'"；
- `WP-24-T05 检查表首项`（NFR-MNT-06、NFR-SEC-06）：WP-24-T05 卡内同款检查表首项断言；
- `WP-01-T02 原生构建断言`（NFR-MNT-05）：WP-01-T02 卡 RED 节"先执行'RED 测试'原生构建断言"；
- `release-checklist-review`（DEL-01）、`defect-register-review`（DEL-02）、`pilot-data-signoff-review`（PILOT-01）、`comparison-report-review`（PILOT-02）：WP-25-T01～T05 各卡第 1 字段声明"非代码任务（验证＝…复核，`architecture/testing-contract.md` §4）"，英文标签为该人工复核方法的 D8 映射名，卡同时以 **DEL-01/DEL-02/PILOT-01/PILOT-02** 直接命名需求。

## 5. ADR-001～005 与需求一致性复核

| ADR | 锚需求存在性 | 语义一致性核验 | 结论 |
| --- | --- | --- | --- |
| ADR-001 单机械臂作用域 | ARC-01、ARC-04、CON-06、NFR-MNT-03、NFR-MNT-07 全部存在 | 需求 §1"单项目、单机械臂、4～7 关节串联主链"与决策一致；抽样卡 WP-03-T02/T04、WP-06-T01/T02/T05 以 ownerScope 语义引用 | 一致 |
| ADR-002 `.rwdesign` 目录格式 | ARC-01、CON-01、CON-03、NFR-REL-01、NFR-REL-04、NFR-SEC-01、NFR-SEC-02 全部存在 | WP-04 卡（项目命令/持久化）与 WP-24 卡（`.rwdesign` 用户目录断言不触碰、卸载保留）引用口径一致 | 一致 |
| ADR-003 R1/R2 切片与 OPT-B | OPT-01～10、NFR-PERF-04～06 全部存在 | CSV：OPT-01～04/06～08=R1/R2、OPT-05/09/10=R2、NFR-PERF-04～06=R2、CON-04=R1/R2，与决策及 WP-00 计划 §5.2.3 完全一致 | 一致 |
| ADR-004 单一权威所有者 | ARC-02、ARC-04、ARC-05、CON-02、CON-05、CON-06、EVI-01、NFR-MNT-03、NFR-MNT-07 全部存在 | 抽样卡所有权声明与决策 2 冻结表一致（WP-06-T03"规范模型为共享语义唯一权威"、WP-07 系列"策略为共享语义唯一权威"、WP-19-T04"只经共享 `DriveTrainMappingEvaluator`，无本地映射实现"） | 一致 |
| ADR-005 正交状态与命名裁决 | EVI-01、TASK-01～03、CON-02、OPT-02/06/08 全部存在 | 符号注册表 `SYM-EVI-005`：`ResultEnvelope` 在册、`EvaluationEnvelope` 仅以禁名记录出现（第 52 行禁名列＋第 102 行 D1 禁令），需求正文 0 命中；`TaskState` 九态（Queued/Running/Pausing/Paused/Canceling/Canceled/Failed/Interrupted/Completed，需求 §6.4 第 376～379 行）与冻结一致；合法组合规则（需求 §6.4 第 371 行：Completed↔Pass/Warning/Infeasible/DataInsufficient，Canceled/Failed/Interrupted↔NotEvaluated）与 D2 裁决一致 | 一致 |

## 6. 断点与观察清单

**断点（链路断裂）：0 项。**

观察（不阻断，仅记录）：

1. **继承·README 检查点滞后**（T01 发现）：`docs/industrial-robot-design/README.md` 头部仍为 `IRD-D10-20260829`，权威基线已至 D13。修复动作建议：WP-00 治理后续派生文档同步任务。本卡禁改。
2. **继承·AT-14 未入 §16 追踪表**（T01 发现）：AT-14 在 §15 定义（需求 1201 行），§16 无任何行引用；机器门禁 0 trace gaps 通过（§16 聚合口径），属需求维护者裁决项。本卡禁改。
3. **继承·生成器空 §16 防线失效**（T03 发现）：`generate-traceability.ps1` 的 `@($acceptanceById[$id])` 空键缺陷使其 `no acceptance trace entry` 永不触发；T03 新增的门禁 §16 覆盖检查为当前唯一有效防线。修复动作建议：治理开列生成器修复任务。本卡禁改。
4. **新增·卡内第 1 字段需求 ID 回声缺失（4 处映射）**：DYN-07/DYN-08→WP-17-T03（计划与 dynamics.md 均显式声明"多工况/包络（DYN-07 P1）、曲线联动与回放 UI（DYN-08 P1）"）；SEL-07→WP-19-T03（WP-19 计划与 device-selection.md 同款声明）；TRJ-07→WP-10-T02（trajectory-planning.md 明确"TRJ-07 曲线查看/动画归 WP-10/WP-22 会话态"；WP-16-T07 以 TRJ-01～07 覆盖）。CSV 映射、WP 计划、模块方案三层一致，语义无冲突，机器门禁 0 trace gaps；仅卡第 1 字段未回显需求 ID。修复动作建议：需求维护者裁决是否要求卡内回声，或由卡片所有者补注。
5. **新增·TRJ-08 回声位置**：TRJ-08（C/后续）在 WP-16-T01 第 1 字段行外以"TRJ-08 后续交付"声明，链路可追溯；建议后续冻结修订时将该声明并入第 1 字段。

## 7. 评审结论与签署

- 128 项需求六环节链路机械全查：P0 覆盖 100%（114/114）、P1 全覆盖，无断点；
- 47 条逐前缀抽样语义一致，反向回溯全部闭合；
- ADR-001～005 与需求正文、符号注册表、追踪矩阵一致；
- 双 shell 门禁输出与 CSV 哈希一致（见双 shell 对照文件）；
- 8 夹具复跑全部按预期非零，门禁拦截能力有效（卡内 RED 确认）。

**评审结论：通过。** 断点 0 项；观察 5 项移交需求维护者/后续治理任务，均不阻断。

签署（独立执行上下文）：WP-00-T04 实施者（ZCode 治理外实施会话，2026-08-30）；本签署为卡内步骤 5 的评审签署，不等于任务 `Done` 登记——`Done` 须由独立验证/治理上下文在复跑本卡验证后另行登记。
