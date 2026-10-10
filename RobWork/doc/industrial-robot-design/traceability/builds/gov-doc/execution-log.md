# GOV-DOC 执行留痕——治理批次·文档与契约治理（零产品源码改动）

- 执行日期：2026-10-10
- 任务分支：`gov-doc`（工作树 `build/pipeline-wt`）
- 执行者：实施会话（GOV-DOC）
- 批次性质：无 canonical 契约 JSON——所有者授权治理批次任务二（逐条验收标准见流水线任务依据①~⑦）；**零产品源码改动**——全部为文档面（契约状态修正＋DTB 补登＋卡面勘误＋留痕勘误注）

## 1. 落地面清单（对应任务依据①~⑥）

| 项 | 落点 | 内容 |
| --- | --- | --- |
| ① | `tasks/foundation/WP-16-T07.json` | status ready→blocked；note 增补 blocked 依据（dependsOn 含 blocked 的 WP-16-T02——P-06 冻结＝需求所有者签署动作）＋流水线排批核查 medium finding 出处＋解冻条件 |
| ② | `development-task-breakdown.md` §5.5 | 补登「gtest 留痕双 report 口径」段（权威计数＝per-exe `--ird_report` json＋同模式 gtest XML；wp20-t10 返工轮八件形态；F-571 权威消账——含编号被 audit P0 占用条目失联的附注） |
| ③ | `development-task-breakdown.md` §2.27（新增） | ASM 系装配批次登记：ASM-PLUG/ASM-WF/ASM-UI 三批 canonical 化（权威记录＝acc/asm-plug/1、acc/asm-wf/1、acc/asm-ui/1；合入提交 1761ba82/46445bb0/904f8c64）＋studio 承接批预登记后续（notCovered canonical 化项消账） |
| ④ | `units/workflow.md` 头部 v1.6 版本行 | 「HostAdapterTest 增 3 用例」→「改造 1＋新增 2 用例」（acc/asm-ui/1 建议③） |
| ⑤ | `units/selection.md` §19.2＋§19.5 | P-SEL-9 状态列「登记（编排面不落位）」→「编排面已落位」（acc/asm-wf/1 建议③）；v0.9 变更行组间计数笔误更正（SelBackfillSynthesis 7／SelBackfillPlan 8——源码实测，acc/wp-19-t09/1 建议⑶） |
| ⑥ | `traceability/builds/wp17-t10/execution-log.md` | 文件末尾追加哈希勘误注（expected 哈希前缀 79c9906b…应为 b4ff5cc6…＝manifest integrity 申报值——原始记录行不改写） |

## 2. 执行的命令与结果（全部真实执行；构建树＝`build/pipeline-wt/build`，缓存 RWS_BUILD_INDUSTRIALROBOT=ON）

| 步骤 | 命令 | 结果 |
| --- | --- | --- |
| 测试目标增量构建 | `cmake --build <wt>/build --config Release --target sdurws_ird_workflow_test sdurws_ird_workflow_contract_test` | 零 error（既有 C4834 警告为基线既有，非本批引入；零源码改动） |
| 单元测试执行 | `cmake --build <wt>/build --config Release --target sdurws_ird_workflow_test_report` | **193/193 PASSED**（16 套件） |
| 契约测试执行 | `cmake --build <wt>/build --config Release --target sdurws_ird_workflow_contract_test_report` | **77/77 PASSED**（16 套件） |
| 双 report 留痕直跑 | `sdurws_ird_workflow_test.exe --gtest_output=xml:<repo>/traceability/gtest-reports/gov-doc/sdurws_ird_workflow_test-integration.xml --ird_report=<repo>/…/ird-test-report-integration.json`（PATH 前置 D:/software/Miniconda3＝DTB §5.1 python313.dll 前置） | 193 PASSED；XML tests="193" failures="0" errors="0"；json summary total=193 |
| 双 report 留痕直跑 | `sdurws_ird_workflow_contract_test.exe --gtest_output=xml:<repo>/…/sdurws_ird_workflow_contract_test-integration.xml --ird_report=<repo>/…/ird-test-report-contract-integration.json` | 77 PASSED；XML tests="77" failures="0" errors="0"；json summary total=77 |
| 门禁 | `cmake --build <wt>/build --config Release --target ird_gates` | **EXIT=0——R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中**（引擎自测 10 项按预期检出，含 GOV-GATE 批新增 pass_r4cliff/fail_r4cliff 悬崖回归对） |
| 文档校验 | `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1` | **PASS（20 units, 12 trace entries, 312 task files）**，EXIT=0 |
| 计数实测佐证（⑤） | `grep -c "^TEST" selection/test/BackfillTest.cpp`＋按套件分组计数 | SelBackfillSynthesis 7／SelBackfillCodec 4／SelBackfillAssembly 7／SelBackfillPlan 8——总 26 与登记总数一致，证实 v0.9 行原计数为组间笔误 |
| 哈希佐证（⑥） | 读 `testdata/golden/dyn-two-link-analytic/1.0.0/manifest.json` integrity 段 | `expected/dyn-two-link-expected.json` sha256＝b4ff5cc668816075…——证实勘误注方向正确 |

## 3. 留痕落位

- `traceability/gtest-reports/gov-doc/`：四件（两测试目标 × XML＋per-exe ird-test-report JSON）——本批即 §5.5 双 report 口径的首个治理批示范留痕（零源码改动批只跑回归涉及单元，不冒烟——冒烟面与集成面在本批零代码差异，留痕如实单模式登记）。
- 本文件：`traceability/builds/gov-doc/execution-log.md`。

## 4. 未执行事项（如实登记）

- **独立冒烟构建**：未执行——本批零源码/CMake 改动（文档与留痕面），冒烟树编译闭包与 gov-doc 基线（origin/gov-doc 起点＝redesign-main 同源）零差异；回归以集成树双测试目标承载。
- **其余单元测试目标**：未执行——本批改动面不涉其他单元（WP-16-T07.json 为状态字段修正，trajectory 单元尚无测试目标受其影响；selection/workflow 卡面勘误为注释性文档修订，不触源码）。
- **正式验收**：未发起——按 acceptance-protocol.md 由独立上下文执行。
