# WP-23-T05 质量与发布门禁

- **Task ID / 需求 ID / ADR / 阶段：** WP-23-T05；NFR-REL-01～05（发布门禁证据）、NFR-COR（缺陷等级口径，需求 §15.4）、§15.4（发布规则：开放 Blocker＝0）；ADR-005（证据状态词与正交口径一致）、ADR-004（门禁构成以 system-quality §6 为唯一权威）。阶段 E 收口（对接 WP-24/WP-25）/ R1＋R2。契约：`architecture/testing-contract.md` §4（证据命名：每条证据必含字段）、§1（分层）；模块详设 `module-design/system-quality.md` v0.3 §6（R1/R2 gate 清单构成与证据格式权威）。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（system-quality.md v0.3）
- **前置任务及必需工件：** WP-23-T01（AT 执行矩阵）、WP-23-T02（故障注入与恢复统计）、WP-23-T03（基准报告）、WP-23-T04（确定性对照报告）全部完成；外部：WP-02-T03（manifest SHA-256 与 datasetVersion）、独立质量负责人可签署（评审者不得参与被测实现）。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/system/`）创建 `include/sdurws/ird/system/ReleaseGate.hpp`、`src/ReleaseGate.cpp`、`test/ReleaseGateTest.cpp`；修改 `testkit/` CMakeLists（登记 `sdurws_ird_release_gate_test`）；创建 `out/test-evidence/wp-23/<run-id>/gate/r1-gate.md`、`out/test-evidence/wp-23/<run-id>/gate/r2-gate.md`。不删除文件。
- **禁止修改的文件和公共接口：** T01～T04 证据工件（只汇总不改写）；业务实现与门禁阈值（不得以关闭测试、降低阈值或隐藏诊断代替修复）；`benchmark-manifest.json`；`requirements.md`、CSV；不替代 WP-01 构建门禁与 WP-24 安装审计。
- **修改前接口：** 无 `ReleaseGate`；R1/R2 门禁证据无统一格式与防作弊断言。
- **修改后接口：** `ReleaseGate` 汇总 T01～T04 证据为 R1/R2 门禁清单。每条门禁证据按 testing-contract §4 字段：任务 ID、需求 ID、AT/门禁 ID、提交 SHA、环境（机器/OS/编译器/线程）、命令原文、输入哈希（datasetVersion＋manifest SHA-256）、实际/期望结果、日志路径、评审者、结论。R1 gate＝阶段 B/C 退出条件 AT 子集＋NFR-PERF-01～03＋NFR-REL；R2 追加＝阶段 D 退出条件 AT-09～14＋NFR-PERF-04～06＋确定性＋恢复演练。防作弊断言：发布时开放 Blocker＝0、未关闭 Critical 均有负责人与计划日期（§15.4）；"不得以关闭测试、降低阈值或隐藏诊断代替修复"由门禁脚本与既有阈值/诊断清单比对断言。
- **实施步骤：**
  1. 写 RED 测试（证据字段完整性、R1/R2 gate 构成对齐 §6、防作弊比对、缺证据即非零）。
  2. 实现 `ReleaseGate`：读取 T01～T04 证据工件，按 §6 构成逐条核对并生成清单。
  3. 实现防作弊比对：关闭的测试/阈值/诊断清单与仓库既有值比对，任何不一致非零。
  4. 生成 `r1-gate.md`/`r2-gate.md` 证据清单并提交独立质量负责人签署。
  5. CMake 登记目标，执行验证命令。
- **RED 测试：** 实现前 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_release_gate_test$"` 零匹配；落地后门禁清单字段完整且防作弊断言通过。
- **最小实现：** 门禁汇总器＋清单生成＋防作弊断言；不重跑被测目标（引用 T01～T04 证据）、不承担 WP-24 安装演练证据。
- **正常/边界/失败测试：**
  - 正常：Given T01～T04 证据齐备且全部通过，When 生成 R2 gate，Then 清单逐条含 §4 十一字段并可追溯到 datasetVersion、manifest SHA-256、seed、threadCount、命令原文与提交 SHA。
  - 边界：Given 存在未关闭 Critical 缺陷，When 生成清单，Then 该条目含负责人与计划日期且 gate 结论标注"有条件"（不得静默放行）。
  - 失败：Given 任一 AT/门禁条目缺证据或字段缺失，When 校验，Then 非零并指明缺口；Given 检测到测试被关闭、阈值被降低或诊断被隐藏，Then 防作弊断言非零并阻断发布。
- **精确验证命令：**（仓库根、VS x64 环境）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_release_gate_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_release_gate_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_release_gate_test$"`
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；门禁清单只汇总不改写 T01～T04 证据；未修改任何阈值/测试开关/诊断清单（防作弊比对基线取自仓库既有值）；每条证据含输入哈希与评审者字段。
- **证据工件：** `out/test-evidence/wp-23/<run-id>/gate/r1-gate.md`、`out/test-evidence/wp-23/<run-id>/gate/r2-gate.md`（逐条 §4 字段证据清单）、防作弊比对输出、独立质量负责人签名页、`out/test-evidence/wp-23/<run-id>/t05-release-gate.log`（命令原文与 commit）。
- **提交格式：** `WP-23-T05: 质量与发布门禁`
- **停止与升级条件：** T01～T04 证据缺失或与门禁构成（system-quality.md §6）冲突时，停止并升级独立质量负责人；出现开放 Blocker 或无计划日期的 Critical 时按 §15.4 阻断发布并上报，不得以关闭测试、降低阈值或隐藏诊断方式放行。
