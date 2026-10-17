# 任务型阅读路径（context/reading-paths.md）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.0（2026-10-17，随 Agent-native 治理 Task 3 建立） |
| 用途 | 按任务类型定义"必须读 / 按需读 / 禁止默认读"清单——接手任务时按对应路径取用，替代通读全树（上下文经济学） |
| 上游 | `context/README.md` §3（默认起步集）；根 AGENTS.md §6.1（分层阅读顺序） |
| 纪律 | 路径只做**指向**不含正文语义；与权威文档冲突时以权威文档为准并登记 findings |

## 0. 任务契约 schema 映射（治理 Task 3 落地口径）

计划原拟为任务 JSON 补齐五个字段，2026-10-17 实测核对（357 份全查）：**四项在 `ird-task/1` 既有 schema 已全量存在**，不加同义新字段（one home per fact，治理原则 2）：

| 计划字段 | ird-task/1 既有字段 | 覆盖 | 说明 |
| --- | --- | --- | --- |
| status | `status` | 357/357 | planned/ready/blocked/done（CCP 生命周期；执行中的流水线相位另见 state.json，二者不混） |
| ownerUnit | `unit` | 357/357 | 单元归属 |
| sourceRequirements | `requirements` | 357/357 | 需求 ID 数组（planned 占位可为空，CCP 编译时补齐） |
| verificationCommands | `verify` | 357/357 | 验证命令数组 |
| evidencePath | —（无对应） | — | **不提前添加**：证据位置归 Task 7 证据索引统一设计，防与 state.json acceptanceRecord 形成双权威（评审修正 3 同型风险） |

ready 契约的字段完备性由 `validate-task.ps1` 把关（CCP 协议），本文不重复校验。

## 1. 路径一：核心算法（L2 计算内核）

**适用**：core / evidence / policy / runtime / kinematics / trajectory / dynamics / drivetrain / selection / optimization 的计算逻辑改动。

- **必须读**：根 AGENTS.md（红线）→ 源码区 AGENTS.md → `tasks/<taskId>.json`（`requirements`/`designRefs` 定位）→ `units/<unit>.md` 对应章节 → REQUIREMENTS 对应需求条目（确定性相关必含 NFR-COR 系列）。
- **按需读**：`ARCHITECTURE.md` §7（涉及的 SA-xx 决策）；性能任务加 `performance-baseline.md`；测试夹具加 `units/testkit.md`。
- **禁止默认读**：DTB 全文（只取 §2 对应任务行）、traceability 证据、ui 域材料、PIPE/ACC 全文（编码任务不做编排/验收时可只读验收 checklist 对应项）。

## 2. 路径二：Qt 功能（ui 界面）

**适用**：ui 单元、`<unit>/gui_test/`、`_plugin` 目标。

- **必须读**：根 AGENTS.md → 源码区 AGENTS.md（§2 L2 零 Qt 红线＋§5 Windows GUI 测试规则）→ 任务契约 → `units/ui.md` 对应章节 → REQUIREMENTS 对应 UI 条目。
- **按需读**：`units/testkit.md`（Qt 测试基建节）；宿主融合相关加 B.1 设计规格（`docs/superpowers/specs/2026-09-27-host-integrated-b1-design.md`）；`ARCHITECTURE.md` 分层节。
- **禁止默认读**：计算单元卡全文、traceability 证据、非本次面所及的其余 gui_test 材料。

## 3. 路径三：项目持久化（project / execution / io）

**适用**：project 修订与写路径、execution 编排、io 落盘与外部交换。

- **必须读**：根 AGENTS.md（PA-1 权威唯一、PA-2 不可变历史两条红线）→ 源码区 AGENTS.md → 任务契约 → `units/<unit>.md` 对应章节 → REQUIREMENTS 对应条目。
- **按需读**：`ARCHITECTURE.md` §7（SA 决策）；契约修订走 `contract-compilation.md`；io↔project/runtime 协作边见 ARCH §3.5。
- **禁止默认读**：gui 材料、计算域单元卡、历史验收记录全文。

## 4. 路径四：流程门禁（治理 / 流水线 / 验收 / 契约编译 / 门禁脚本）

**适用**：PIPE tick 编排、验收执行、契约编译、validate-*/gate 脚本改动。

- **必须读**：按角色——tick 执行者：`automation-pipeline.md` 全文（§0 自足性声明 tick 输入）＋ `state.json` ＋ 引用的契约；验收者：`acceptance-protocol.md` 全文＋目标契约；契约编译：`contract-compilation.md` ＋ `traceability/contract-compile-log.md`；脚本改动：对应脚本本体＋其上游文档节。
- **按需读**：`governance-log.md`、`traceability/findings.json`、DTB §4（O-xx 待裁决）、本目录（context/）治理产物。
- **禁止默认读**：REQUIREMENTS/ARCHITECTURE 全文（按条目取）、traceability 证据全文、单元卡全文。

## 5. 路径五：测试故障（测试红 / 门禁失败排查）

**适用**：gtest 失败、ird_gates 命中、CI 红、gui_test 挂起。

- **必须读**：失败输出本体（gtest XML / 构建日志 / ird-test-report）→ 任务契约 `verify`/`acceptance` → 对应单元卡测试节。
- **按需读**：`traceability/gtest-reports/`（历史留痕）、`traceability/findings.json`（同类已知问题检索）、`cmake/ird_gates.cmake`＋`ird_gates_whitelist.cmake`（命中项与白名单）、DTB §5（构建与留痕口径——如 §5.5 双 report 口径）。
- **禁止默认读**：整册设计文档（先复现定位再回查语义）、历史验收记录全文、无关单元卡。
