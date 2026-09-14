# WP-07-T12（POL-T12）门禁登记提交件

| 字段 | 值 |
| --- | --- |
| 提交任务 | POL-T12 文档与门禁同步（≙WP-07-T12，policy 单元） |
| 日期 | 2026-09-14 |
| 提交方 | POL-T12 实施段（分支 `wp07-t12`） |
| 接收方 | §2 → WP-01（CI 门禁所有者，WP-01-T02 交付物维护方）＋WP-24（交付与部署基础） |
| 性质 | **登记提交件**——本文件只提交清单与建议，接收方处置后才生效；提交方不代行 `scripts/` 与 `ci/` 侧改动（均不在本任务 allowedFiles） |
| 上游依据 | 需求 NFR-MNT-05（审查循环依赖/未引用目标等构建卫生）；units/policy.md §12 POL-T12 任务行（产物原文"UT-BUILD 建议并入 CI（WP-01 侧）"）、§15.4 v0.13；CORE-T10/EV-T12（traceability/wp05-t12-gate-registrations.md）/RT-T13（traceability/wp06-t13-gate-registrations.md）同型先例 |

---

## §1 R-4 例外登记——本卡无清单提交（事实声明，非请求）

policy 单元**不提交 R-4 例外实现文件清单**，与 runtime（RT-T13）不同：本单元产品面不存在任何"RobWork 名称前缀拼接/剥离"实现点——名称消费仅经 `IPolicyNameContext` 注入接口与场景 Frame 整名（policy.md §3.3/§6.1，D-11），静态扫描此前的命中面已由 POL-T06 的 TU 切分承载（policy.md §15.4 v0.7⑤——那是门禁启发式误报的结构性隔断，**不是例外申请**）。P-POL-8（"policy 只读消费完整设备作用域名不构成拼接/剥离"的例外登记措辞）与 O-12 的措辞裁决维持待架构所有者处置，本卡不预设、不代替其裁决；裁决后如需登记，按 DTB §4.5 登记册流程另行提交。

## §2 UT-BUILD 并入 CI 建议（登记 WP-01／WP-24）

### 2.1 对象与现状

policy 的 UT-BUILD（构建红线用例组）＝`policy/test/BuildRedLineTest.cpp` 四例，已注册于 `sdurws_ird_policy_test`：

| 用例 | 红线 |
| --- | --- |
| `PolicyBuild.SourceTreeReachable_POL_BUILD` | 源码树可达（读失败显性失败，不静默跳过） |
| `PolicyBuild.NoQtInclude_POL_BUILD_NFR_MNT_01` | L2 产品面零 Qt（NFR-MNT-01，红线 R-3 同源） |
| `PolicyBuild.NoCrossUnitInclude_POL_BUILD_R1_R2` | 零跨单元 include（R-1/R-2） |
| `PolicyBuild.BaselineLibsPinned_POL_BUILD_R5` | L1 基线库链接清单钉住（R-5 proximity 直链唯一许可方——policy 侧口径） |

### 2.2 既有承载（沿 CORE-T10/EV-T12/RT-T13 先例口径如实消账——非新裁决）

1. **本地门禁已常驻**：`gate-all.ps1`（WP-01-T02 交付）执行面＝ird_gates＋双模式构建＋**全部 `_test`/`_contract_test` 目标一键执行**——policy UT-BUILD 四例随 `sdurws_ird_policy_test` 包含其中（POL-T11 留痕：冒烟/集成各 7 项测试发现与执行全 PASS）。
2. **构建期门禁已常驻**：`ird_gates.cmake`（WP-01-T01 交付）在构建树对产品面执行 R-2/R-3/R-4/R-5 等静态扫描，与 UT-BUILD 的运行期扫描同源双层（CORE-T01 验收登记的"双层防线"口径）。

### 2.3 缺口登记（建议事项的真实落点）

CI 模板（`RobWork/scripts/industrialrobot/ci/industrial-robot-windows.github-workflow.yml` 两处、`industrial-robot-windows.gitlab-ci.yml` 两处）测试阶段 run-tests 正则当前仍钉 `^sdurws_ird_core_test$`（2026-09-14 实测复核）——这是 WP-01-T02 交付时点"唯一已建测试目标"的历史口径；此后各单元 `_test`/`_contract_test` 目标已陆续落地（testkit/evidence/project/runtime/policy 等），**policy（及其余单元）的测试目标尚未进入 CI 执行面**，与 ARCH §11.2 第 2 条"构建红线门禁随首批源码启用、CI 常驻"的目标存在执行面差距。本缺口与 EV-T12/RT-T13 登记的为同一处缺口（三卡各自登记各自的 UT-BUILD 面，缺口本体一致、互为佐证）。

### 2.4 登记请求

| 接收方 | 请求 | 辖域说明 |
| --- | --- | --- |
| WP-01（CI 门禁所有者，WP-01-T02 交付物维护方） | CI 测试阶段执行面由单目标正则扩展为全部单元 `_test`/`_contract_test` 目标（或按单元矩阵逐目标），使 policy UT-BUILD 四例与其余单元测试随 CI 常驻 | `scripts/` 与 `ci/` 不在 POL-T12 allowedFiles，扩展动作归 WP-01 |
| WP-24（交付与部署基础） | WP-24-T01 冻结版本基线首版登记时，复核 CI 执行面对"全部单元测试目标"的覆盖状态（含本条扩展是否已完成） | WP-24-T01 依赖 WP-01-T01，基线登记即自然复核点 |

---

## §3 提交方声明

- 本提交件内容为**事实登记与建议**，不改变任何需求/架构语义，不裁决任何未决项（P-POL-8/O-12 维持待架构侧裁决；CI 执行面处置权在 WP-01）。
- 本提交件随 POL-T12 验收请求送验；接收方处置后请在本文件追加处置记录（或于各自交付物中回链本文件）。
