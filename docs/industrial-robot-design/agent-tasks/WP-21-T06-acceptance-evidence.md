# WP-21-T06 联合优化验收证据

- **Task ID / 需求 ID / ADR / 阶段：** WP-21-T06；OPT-01～10 收口、AT-09～14（本卡重点 AT-10～14：分支切换、中断恢复、应用候选、崩溃、大型负载）、TASK-01～03、NFR-PERF-04～06、NFR-REL-02；ADR-004。阶段 D / R2（阶段 D 门禁，总纲 §8.4）。契约：`architecture/testing-contract.md` §3～§5、`architecture/execution-model.md` §4～§5；模块详设 `module-design/optimization.md` v0.3 §6；需求 §15.3。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-21-T03（检查点/恢复）、WP-21-T04（审计与鲁棒性）、WP-21-T05（应用路径）；WP-08-T03（崩溃/取消 failpoint）、WP-02-T03（optimization 长任务/检查点样本）；工件：T01～T05 全部用例通过、`sdurws_ird_execution_test` 可用。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/optimization/`）创建/修改 `test/AcceptanceEvidenceTest.cpp`（AT-10～14 用例与证据装配）；创建 `out/test-evidence/wp-21/<run-id>/` 下证据文件（R2 基准报告、误淘汰审计、恢复统计、Pareto 黄金集、AT-09～14 记录）；修改模块 CMakeLists（仅当需登记用例数据）。不删除文件。
- **禁止修改的文件和公共接口：** T01～T05 已交付实现源文件（发现缺陷走上游所有者流程，不回改冻结行为）；WP-08/16～19/20 源文件；`requirements.md`、CSV、`schemas/`、`benchmark-manifest.json`（性能门禁数据与 WP-23-T03 协作采集，本卡不改 manifest）；不新增 CMake 目标。
- **修改前接口：** `AcceptanceEvidenceTest.cpp` 不存在（或无 AT-10～14 用例）；`out/test-evidence/wp-21/<run-id>/` 无验收证据集。
- **修改后接口：** `sdurws_ird_optimization_joint_test` 内含 AT-10～14 五组验收用例；`out/test-evidence/wp-21/<run-id>/` 证据集：R2 基准报告（与 WP-23-T03 协作）、误淘汰审计、恢复统计、Pareto 黄金集、AT-09～14 记录与独立评审签名。
- **实施步骤：**
  1. 写 RED 测试（AT-10～14 五场景断言，见下）。
  2. AT-10 分支切换：优化运行期间切换/关闭项目，旧结果只追加原分支历史、项目完整（TASK-03）。
  3. AT-11 中断恢复：注入中断（WP-08 failpoint），从兼容检查点恢复、已完成批次不重复计入。
  4. AT-12 应用候选：方案分支＋恰好一个新修订、基线不覆盖、修订数不随候选增长（复跑 T05 断言形成记录）。
  5. AT-13 崩溃：注入 worker 崩溃，主程序存活、任务显示 `Failed`、项目可恢复。
  6. AT-14 大型负载：WP-02 performance 样本下无候选静默丢失、无错误写入（性能数值断言由 WP-23-T03 在验收机出证，本卡出行为证据）。
  7. 装配证据文件并按 testing-contract §4 字段补全（任务 ID、需求 ID、提交 SHA、环境、命令原文、输入哈希、实际/期望、日志路径、评审者、结论），取得独立评审签名。
  8. 执行验证命令，归档。
- **RED 测试：** 实现前 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_joint_test$"` 无 AT-10～14 用例；落地后五组用例全部通过。
- **最小实现：** 仅 AT 用例与证据装配；不修改任何业务实现；不在本卡内补建性能基准设施（WP-23 所有）。
- **正常/边界/失败测试：**
  - 正常：Given WP-02 optimization 长任务样本，When 完整执行 AT-10～14 场景，Then 五条判据（§4 各 AT 行）逐条成立并留日志。
  - 边界：Given 恢复时检查点为最近兼容版本且存在更旧不兼容版本，When 恢复，Then 只用兼容版本且统计不重复。
  - 失败：Given 崩溃后项目状态损坏（人为破坏样本），When 检查，Then 检出并给出稳定诊断，不伪装完整结果（`Interrupted/Failed` 口径）。
- **精确验证命令：**（仓库根、VS x64 环境）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_joint_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_joint_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_joint_test$"`
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `test/AcceptanceEvidenceTest.cpp`、`out/test-evidence/wp-21/<run-id>/` 与（如需）模块 CMakeLists 用例数据登记；T01～T05 实现源文件零变化；证据文件含 commit、种子、输入哈希身份。
- **证据工件：** `out/test-evidence/wp-21/<run-id>/`：`t06-at10-14.log`、R2 基准报告（协作 WP-23-T03）、误淘汰审计汇总、恢复统计表、Pareto 黄金集、独立评审签名页（实现者不得自评）。
- **提交格式：** `WP-21-T06: 联合优化验收证据`
- **停止与升级条件：** AT 判据与需求 §15.3/§16 或 system-quality §3 表冲突、或验收机性能证据无法由 WP-23-T03 提供时，停止并升级工作包所有者与独立质量负责人；本卡实现者不得担任最终评审者。
