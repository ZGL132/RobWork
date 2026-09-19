# WP-08-T10（EX-T10）门禁登记提交件

| 字段 | 值 |
| --- | --- |
| 提交任务 | EX-T10 文档与门禁同步（execution 单元卡内治理任务，≙WP-08 收尾；契约 `tasks/foundation/EX-T10.json`） |
| 日期 | 2026-09-19 |
| 提交方 | EX-T10 实施段（分支 `wp08-t10`，base `ddf2031f85fac89e9c0042a0bba2f4c8f1cf882a`） |
| 接收方 | §2/§3 → WP-01（构建边界与门禁横切，`ird_gates.cmake`/CI 交付物维护方）＋WP-24（交付与部署基础） |
| 性质 | **登记提交件**——本文件只提交清单与建议，接收方处置后才生效；提交方不代行 `scripts/`、`ci/` 与白名单数据侧改动（均不在本任务 allowedFiles）；不私改门禁语义与白名单数据 |
| 上游依据 | 需求 NFR-MNT-05；units/execution.md §12 EX-T10 行产物列原文"`ird_gates`/CI 建议提交"；CORE-T10/EV-T12（traceability/wp05-t12-gate-registrations.md）/RT-T13（traceability/wp06-t13-gate-registrations.md）/POL-T12（traceability/wp07-t12-gate-registrations.md）同型先例 |

---

## §1 R-4 例外登记——本卡零申请（事实声明）

execution 单元**不提交 R-4 例外实现文件清单**："RobWork 名称前缀拼接/剥离"在 execution 产品面（include/src/worker）零实现点——名称解析语义归 runtime（R-4 红线归属），execution 经 `INameResolverAdapter` 注入消费反解结果（units/execution.md §3.3，D-17），worker/通道层将名称作不透明字符串承载。2026-09-19 实测：`RobWork.` 字面量与 `erase(0)/substr 前缀剥离`形扫描在 execution 源码零命中。P-EX-3（runtime 注入形态）维持待架构侧裁决，与本声明无涉。

## §2 UT-BUILD 并入 CI 建议（登记 WP-01／WP-24）

### 2.1 对象与现状

execution 的 UT-BUILD（构建红线用例组）＝`execution/test/BuildRedLineTest.cpp` 七例，已注册于 `sdurws_ird_execution_test`：

| 用例 | 红线/依据 |
| --- | --- |
| `ExecutionBuild.SourceTreeReachable_DT_BUILD` | 源码树可达（读失败显性失败，不静默跳过） |
| `ExecutionBuild.NoVocabularyRedefinition_O24_DT_BUILD` | 上游词表零重定义（O-24 处置钉子） |
| `ExecutionBuild.NoQtInclude_DT_BUILD_NFR_MNT_01` | L2 产品面零 Qt（NFR-MNT-01；D-01 零 Qt 含 Core） |
| `ExecutionBuild.NoEigenOrRwMathDirectInclude_DT_BUILD` | 禁 Eigen/RW 数学头直含 |
| `ExecutionBuild.NoCrossUnitInclude_DT_BUILD_R1_R2` | 零跨单元 include（R-1/R-2） |
| `ExecutionBuild.RuntimePolicyZeroCompileFace_DT_BUILD_PEX3` | runtime/policy 零编译面（P-EX-3 注入形态结构性钉子） |
| `ExecutionBuild.PublicHeaderPathLayout_DT_BUILD_R2` | 公共头路径布局（R-2 跨单元私有头防线） |

### 2.2 既有承载（沿 CORE-T10/EV-T12/RT-T13/POL-T12 先例口径如实登记——非新裁决）

1. **本地门禁已常驻**：`gate-all.ps1`（WP-01-T02 交付）执行面＝ird_gates＋双模式构建＋全部 `_test`/`_contract_test` 目标一键执行——execution UT-BUILD 七例随 `sdurws_ird_execution_test` 包含其中（wp08-t01～t10 各期留痕）。
2. **构建期门禁已常驻**：`ird_gates.cmake`（WP-01-T01 交付）对产品面执行 R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB 静态扫描，与 UT-BUILD 的运行期扫描同源双层。
3. **红线断言钉住**：上述七例中 NoCrossUnitInclude/PublicHeaderPathLayout/RuntimePolicyZeroCompileFace 与 ird_gates 静态扫描互为双保险（运行期断言＋构建期扫描），执行面扩展后防线不减弱。

### 2.3 缺口登记（建议事项的真实落点，2026-09-19 复测）

CI 模板（`RobWork/scripts/industrialrobot/ci/industrial-robot-windows.github-workflow.yml` 两处、`industrial-robot-windows.gitlab-ci.yml` 两处）测试阶段 run-tests 正则**实测仍钉 `^sdurws_ird_core_test$`**（2026-09-19 复核，与 POL-T12 2026-09-14 登记时一致）——WP-01-T02 交付时点"唯一已建测试目标"的历史口径；此后 testkit/core/evidence/project/runtime/policy/diagnostics/io/ui/execution 各单元测试目标已陆续落地，execution 两目标（149＋38 用例，含真进程契约套件）**尚未进入 CI 执行面**，与 ARCH §11.2 第 2 条"构建红线门禁随首批源码启用、CI 常驻"存在执行面差距。本缺口与 EV-T12/RT-T13/POL-T12 登记的为同一处缺口（四卡各自登记各自 UT-BUILD 面，缺口本体一致、互为佐证）。

### 2.4 登记请求

| 接收方 | 请求 | 辖域说明 |
| --- | --- | --- |
| WP-01（构建边界与门禁横切） | CI 测试阶段执行面由单目标正则扩展为全部单元 `_test`/`_contract_test` 目标（或按单元矩阵逐目标），使 execution UT-BUILD 七例、真进程契约套件与其余单元测试随 CI 常驻 | `scripts/` 与 `ci/` 不在 EX-T10 allowedFiles，扩展动作归 WP-01；执行面扩展时请一并评估真进程用例的 CI 环境前提（Windows runner＋VS x64，worker 子进程派发） |
| WP-24（交付与部署基础） | WP-24-T01 冻结版本基线首版登记时，复核 CI 执行面对"全部单元测试目标"的覆盖状态（含本条扩展是否已完成） | WP-24-T01 依赖 WP-01-T01，基线登记即自然复核点 |

## §3 ird_gates 命中集合状态（事实登记，非请求）

1. **本任务零新增命中**：EX-T10 为纯文档任务（execution 源码/CMake/worker 零触碰），`ird_gates` 命中集合与 wp08-t09 验收基线排序比对逐项一致（GATES-IDENTICAL-18，新增 0/移除 0——traceability/builds/wp08-t10/gates-hit-set-diff.txt）。
2. **F-221 实例行增补仍待 WP-01-T03**：EX-T09（wp08-t09）转登的 F-221（`sdurws_ird_execution_test`/`sdurws_ird_execution_contract_test` → `sdurws_ird_testkit` 两条 SUB 命中——T-1 允许形态、契约 acceptance 明文消费边，F-173/F-115 同型先例）的 DTB §4.5 登记册实例行增补归 WP-01-T03 维护方，本卡不代行白名单数据改动。
3. **现状命中均属既有登记**：18 项命中（LIB×3——io 链 libzip/expat 白名单外三方；SUB×13——各单元测试目标 testkit 消费边＋testdata_lint；R4×1——project ProjectStoreImpl.cpp RobWork 字面量，DTB §4.5 已登记例外；T1×1——testdata_lint）全部为 findings.json/DTB §4.5 既有归属，本任务未新增未移除。

## §4 提交方声明

- 本提交件内容为**事实登记与建议**，不改变任何需求/架构语义，不裁决任何未决项（P-EX-3 注入形态维持待架构侧；CI 执行面处置权在 WP-01）。
- 本提交件随 EX-T10 验收请求送验；接收方处置后请在本文件追加处置记录（或于各自交付物中回链本文件）。
