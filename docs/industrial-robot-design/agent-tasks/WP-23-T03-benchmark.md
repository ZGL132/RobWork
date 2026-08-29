# WP-23-T03 性能与规模基准

- **Task ID / 需求 ID / ADR / 阶段：** WP-23-T03；AT-14（大型负载：界面持续响应、无候选静默丢失、无错误写入）、NFR-PERF-01～06（交付断言承载）、NFR-COR（§15.3 规模口径）；ADR-004（基准口径以 manifest 与 system-quality §5 为唯一权威）；阶段 D 收口 / R1＋R2。契约：`architecture/testing-contract.md` §2（数值与性能）、§4（证据命名）、`architecture/execution-model.md`（有界并行与资源预算）；模块详设 `module-design/system-quality.md` v0.3 §5（基准口径权威）、只读输入 `benchmark-manifest.json`（seed 20260828、threadCounts [1,8]、warmupRuns 2、measurementRuns 3）。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（system-quality.md v0.3）
- **前置任务及必需工件：** WP-23-T01（AtRegistry 与 AT-14 场景挂接）；外部：WP-02-T03 performance 数据集（5,000 任务/100k 采样/10k 候选）、`benchmark-manifest.json` 已冻结（`approvalRequiredForChange: true`）、验收机（16 核/64GB/SSD，manifest hardware 字段）可用。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/system/`）创建 `include/sdurws/ird/system/BenchmarkRunner.hpp`、`src/BenchmarkRunner.cpp`、`benchmarks/`（工作负载定义）、`test/BenchmarkTest.cpp`；修改 `testkit/` CMakeLists（登记 `sdurws_ird_benchmark_test`）；写 `evidence/WP-23/`。不删除文件。
- **禁止修改的文件和公共接口：** 业务实现与调度器（只测量不改）；`benchmark-manifest.json`（只读，无产品与测试双负责人批准不得修改）；WP-02 数据集（只读）；`requirements.md`、CSV；不在开发机上出具基准结论。
- **修改前接口：** 无 `BenchmarkRunner`；AT-14 与 NFR-PERF 无可重复执行的测量入口。
- **修改后接口：** `BenchmarkRunner` 只读消费 manifest 全字段执行：数据集 `ird-standard-six-axis-v1`、预热 2＋测量 3、冷热启动均记录（`startupMode: cold-and-warm-recorded`）、中位数聚合且保留全部样本、后台负载（1 个 5,000 任务批处理 worker）、排除首次依赖编译与数据导入；产出 AT-14 与 NFR-PERF 门禁数据。六项交付断言（验收机全部满足，system-quality.md §5）：交互 P95≤200 ms（NFR-PERF-01）；5,000 任务批处理 30 min；100k Quick 样本 20 min；8 线程加速比≥4；标准优化基准（10,000 Quick＋100 Verified 候选）8 h 内；内存峰值≤物理内存 70% 且节流先于诊断。
- **实施步骤：**
  1. 写 RED 测试（manifest 字段逐项消费校验、六项断言、非验收机拒绝出具基准结论）。
  2. 实现 `BenchmarkRunner`：预热 2＋测量 3 循环、冷热启动记录、中位数聚合与全样本保留。
  3. 挂接后台负载与排除项（首次依赖编译、数据导入不入测量）。
  4. 在验收机执行六项测量，开发机结果一律标注"非基准"。
  5. CMake 登记目标，写基准报告证据。
- **RED 测试：** 实现前 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_benchmark_test$"` 零匹配；落地后六项交付断言在验收机全部满足。
- **最小实现：** 基准运行器＋工作负载定义＋基准报告；故障注入（T02）、确定性（T04）、门禁汇总（T05）不在本卡。
- **正常/边界/失败测试：**
  - 正常：Given 验收机与 manifest 冻结参数，When 执行标准优化基准（10,000 Quick＋100 Verified 候选），Then 8 h 内完成且报告含全部样本与中位数。
  - 边界：Given 后台 5,000 任务批处理 worker 持续运行，When 测量交互 P95，Then 仍≤200 ms（NFR-PERF-01）；Given threadCounts [1,8]，Then 8 线程加速比≥4。
  - 失败：Given 测量期间候选静默丢失或错误写入，When 复核 AT-14，Then 判失败并登记缺陷（§15.4）；Given manifest 缺字段或被无批准修改，Then 拒绝执行（`approvalRequiredForChange`）。
- **精确验证命令：**（仓库根、VS x64 环境；性能与内存目标只在验收机执行，module-design/system-quality.md §7）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Release -Regex '^sdurws_ird_benchmark_test$'`
  - `cmake --build out\build\industrial-robot --config Release --target sdurws_ird_benchmark_test`
  - `ctest --test-dir out\build\industrial-robot -C Release -R "^sdurws_ird_benchmark_test$"`
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；manifest 零修改（只读消费）；预热/测量次数、seed 20260828、threadCounts [1,8] 与 manifest 一致；无候选静默丢失、无错误写入路径；开发机报告标注"非基准"。
- **证据工件：** `evidence/WP-23/t03-benchmark.log`：基准报告（含全部样本、中位数聚合、冷热启动两组）、验收机/开发机区分标注、六项交付断言对照表、manifest SHA-256、命令原文与 commit。
- **提交格式：** `WP-23-T03: 性能与规模基准`
- **停止与升级条件：** 验收机不可用或 manifest 与 system-quality.md §5 口径不一致时，停止并升级独立质量负责人（无批准不改 manifest）；六项断言任一不满足时按 §15.4 登记缺陷并上报，不得以降低阈值或排除样本方式"通过"。
