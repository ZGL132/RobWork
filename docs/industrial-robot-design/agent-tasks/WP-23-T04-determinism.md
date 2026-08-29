# WP-23-T04 并行确定性

- **Task ID / 需求 ID / ADR / 阶段：** WP-23-T04；NFR-COR-02（并行复现，§15.3"并行复现"行）、AT-09（固定种子复现断言由本目标证据支撑）；ADR-004（确定性口径只消费 §15.3 与 manifest，不自行放宽）；阶段 B 子集、阶段 D 收口 / R1＋R2。契约：`architecture/testing-contract.md` §2（数值与性能：并行复现）、`architecture/evaluation-semantics.md` §1～2（可行集语义）；模块详设 `module-design/system-quality.md` v0.3 §5（并行复现行：seed/threadCount 与一致集口径权威）。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（system-quality.md v0.3）
- **前置任务及必需工件：** WP-23-T01（AT-09 优化场景与夹具）、WP-23-T03（seed/threadCount 口径自 `benchmark-manifest.json` 统一读取）；外部：WP-02-T02（`StableSetAssertions` 断言库）、WP-20-T05（缓存键含 seed/threadCount 语义——只消费不复制）；工件：T01/T03 用例通过、optimization 基线/改进/硬约束失败候选样本可用。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/system/`）创建 `test/DeterminismTest.cpp`；修改 `testkit/` CMakeLists（登记 `sdurws_ird_determinism_test`，仅追加测试文件）；写 `evidence/WP-23/`。不删除文件。
- **禁止修改的文件和公共接口：** WP-20/21 优化实现与稳定 ID 公式（只复算对照）；T01/T03 已交付夹具（只复用）；WP-02 断言库与数据集（只读）；`benchmark-manifest.json`（只读）；`requirements.md`、CSV；不新增 CMake 产品目标。
- **修改前接口：** 无 `DeterminismTest`；跨线程并行复现无系统级对照证据。
- **修改后接口：** `DeterminismTest` 复用 T01 优化场景与 WP-02 `StableSetAssertions`，在 seed 20260828、threadCounts [1,8]（取自 manifest）下断言：①候选稳定 ID 逐位一致；②可行集合一致；③Pareto 支配关系一致；④数值满足 §15.3 算法级相对/绝对误差；⑤不要求浮点文件逐字节相同（NFR-COR-02）；⑥AT-09"固定种子复现"判据由本目标证据支撑（T01 执行矩阵引用本证据）。
- **实施步骤：**
  1. 写 RED 测试（[1,8] 两线程组对照：稳定 ID/可行集/支配关系/数值容差四类断言）。
  2. 实现 `DeterminismTest`：同 seed 两次运行（1 线程与 8 线程）结果集合经 `StableSetAssertions` 对照。
  3. 断言非逐字节口径：数值走 §15.3 相对/绝对误差，集合走稳定 ID 与支配关系。
  4. CMake 登记目标，执行验证命令，写确定性对照报告。
- **RED 测试：** 实现前 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_determinism_test$"` 零匹配；落地后四类一致断言全部通过。
- **最小实现：** 确定性对照测试＋对照报告；不新增优化实现、不改缓存键（WP-20-T05 语义）、不做基准计时（T03）。
- **正常/边界/失败测试：**
  - 正常：Given seed 20260828、1 线程与 8 线程各运行一次标准优化场景，When 对照，Then 候选稳定 ID、可行集合、Pareto 支配关系完全一致。
  - 边界：Given 两运行数值差异落在 §15.3 算法级相对/绝对误差内，When 判定，Then 视为一致（不要求浮点文件逐字节相同）；Given 硬约束失败候选样本，Then 不进入可行集合（两线程组一致排除）。
  - 失败：Given 任一候选稳定 ID 或支配关系不一致，When 对照，Then 非零并登记缺陷（§15.4），不得以改种子或减线程方式规避。
- **精确验证命令：**（仓库根、VS x64 环境）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_determinism_test$'`
  - `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_determinism_test`
  - `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_determinism_test$"`
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；seed/threadCount 只读自 manifest（不硬编码旁路值）；无浮点逐字节断言；无对优化实现的任何修改；无第二套稳定 ID 计算副本。
- **证据工件：** `evidence/WP-23/t04-determinism.log`：确定性对照报告（seed 20260828、threadCounts [1,8] 两组的候选稳定 ID/可行集/支配关系对照表、数值误差表）、manifest SHA-256、命令原文与 commit。
- **提交格式：** `WP-23-T04: 并行确定性`
- **停止与升级条件：** 跨线程一致无法达成且根因在优化实现（WP-20/21）时，停止并升级对应工作包所有者（本卡不改业务代码）；确定性口径与 §15.3/manifest 冲突时停止上报，不自行改写权威语义。
