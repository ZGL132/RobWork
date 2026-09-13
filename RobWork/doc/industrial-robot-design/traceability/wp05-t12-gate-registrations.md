# WP-05-T12（EV-T12）UT-BUILD 并入 CI 登记提交件

| 字段 | 值 |
| --- | --- |
| 提交任务 | EV-T12 文档与门禁同步（≙WP-05-T12，evidence 单元）acceptance 3『UT-BUILD 建议并入 CI（登记给 WP-01 侧——§12 任务行原文）』 |
| 日期 | 2026-09-13 |
| 提交方 | EV-T12 实施段（分支 `wp05-t12`，base 1a8b9cb471b77d79e177d4cbe515b38755811a1a） |
| 接收方 | WP-01（CI 门禁所有者，WP-01-T02 交付物维护方） |
| 性质 | **登记提交件**——本文件只提交事实与建议，接收方处置后才生效；提交方不代行 `scripts/` 与 `ci/` 侧改动（均不在本任务 allowedFiles） |
| 上游依据 | 需求 NFR-MNT-05（代码审查检查循环依赖、未引用目标、重复实现、废弃构建选项和继续增长的巨型文件——构建红线自动化的可维护性口径）；units/evidence.md §12 EV-T01/EV-T12 任务行原文；CORE-T10 先例（traceability/acceptance/CORE-T10-20260911.md）；RT-T13 同型登记件（traceability/wp06-t13-gate-registrations.md §2，2026-09-12） |

---

## §1 evidence 的 UT-BUILD 对象与现状

evidence 的 UT-BUILD（构建红线用例组）＝`evidence/test/BuildRedLineTest.cpp` 四例
（EV-T01 交付，随 `sdurws_ird_evidence_test` 注册；本任务实跑两模式全绿——
traceability/builds/wp05-t12/）：

| 用例 | 红线 |
| --- | --- |
| `EvidenceBuild.SourceTreeReachable_EV_BUILD` | 源码树可达（读失败显性失败，不静默跳过） |
| `EvidenceBuild.NoQtInclude_EV_BUILD_NFR_MNT_01` | L2 产品面零 Qt（NFR-MNT-01，红线 R-3 同源） |
| `EvidenceBuild.NoCrossUnitInclude_EV_BUILD_R1_R2` | 零跨单元 include（R-1/R-2——evidence include 面白名单＝{core, evidence}） |
| `EvidenceBuild.PublicHeaderPathLayout_EV_BUILD_R2` | 公共头路径布局（R-2——公共头仅位于 include/sdurws/ird/evidence/） |

## §2 既有承载（沿 CORE-T10/RT-T13 先例口径如实消账——非新裁决）

1. **本地门禁已常驻**：`gate-all.ps1`（WP-01-T02 交付）执行面＝ird_gates＋双模式构建
   ＋全部 `_test`/`_contract_test` 目标一键执行——evidence UT-BUILD 四例随
   `sdurws_ird_evidence_test` 包含其中。
2. **构建期门禁已常驻**：`ird_gates.cmake`（WP-01-T01 交付）在构建树对产品面执行
   R-2/R-3/R-4/R-5 等静态扫描，与 UT-BUILD 的运行期扫描同源双层（core 单元
   CORE-T01 验收已登记该"双层防线"口径；evidence 侧 EV-T01 起随构建启用）。

## §3 缺口登记（建议事项的真实落点，2026-09-13 实测）

CI 模板（`RobWork/scripts/industrialrobot/ci/industrial-robot-windows.github-workflow.yml`
第 310/413 行与 `industrial-robot-windows.gitlab-ci.yml` 第 122/132 行）测试阶段的
run-tests 正则**当前仍钉 `^sdurws_ird_core_test$`**——与 RT-T13 登记时点（2026-09-12）
相比无变化，属 WP-01-T02 交付时点"唯一已建测试目标"的历史口径延续。此后各单元
`_test`/`_contract_test` 目标陆续落地（testkit/evidence/project/runtime/policy 等），
**evidence（及其余单元）的测试目标尚未进入 CI 执行面**，与 ARCHITECTURE §11.2
第 2 条"构建红线门禁随首批源码启用、CI 常驻"的目标存在执行面差距。

补充事实（同一缺口的证据侧延伸）：evidence 的跨单元契约测试目标
`sdurws_ird_evidence_contract_test`（EV-T07 交付）同样不在 CI 执行面——该目标承载
core↔evidence 词表契约三防线（token 字面量/词表本地定义零命中/包络字段类型
static_assert），其 CI 缺位意味着契约漂移只能靠本地门禁拦截。

## §4 登记请求

| 接收方 | 请求 | 辖域说明 |
| --- | --- | --- |
| WP-01（CI 门禁所有者，WP-01-T02 交付物维护方） | CI 测试阶段执行面由单目标正则扩展为全部单元 `_test`/`_contract_test` 目标（或按单元矩阵逐目标），使 evidence UT-BUILD 四例、evidence 契约测试与其余单元测试随 CI 常驻 | `scripts/` 与 `ci/` 不在 EV-T12 allowedFiles，扩展动作归 WP-01 |

关联登记（不重复提交，仅交叉引用）：RT-T13 已就同一缺口提交同型登记件
（traceability/wp06-t13-gate-registrations.md §2，含 WP-24 复核请求）——本件为
evidence 单元侧的同源补登，两件合并处置即可，处置结果请在本文件追加记录（或于
WP-01 交付物中回链）。

---

## §5 提交方声明

- 本提交件内容为**事实登记与建议**，不改变任何需求/架构语义，不裁决任何未决项。
- 本提交件随 EV-T12 验收请求送验；接收方处置后请在本文件追加处置记录。
