# WP-23-T01 系统验收测试套件

- **Task ID / 需求 ID / ADR / 阶段：** WP-23-T01；AT-01～19（需求 §15.2 核心场景系统级执行清单）、NFR-COR（§15.3 冻结容差）、NFR-REL（执行矩阵承载）、A-GATE-01～07（脚本化）；ADR-004（判据语义只消费权威定义不复制）、ADR-005（状态展示不混排）。阶段 A 起持续建设、阶段 D 收口 / R1＋R2。契约：`architecture/testing-contract.md` §1（分层）、§3（Given/When/Then）、§5（Windows 规则）、`architecture/evaluation-semantics.md` §1～2/§5；模块详设 `module-design/system-quality.md` v0.3 §3（AT-01～19 全量执行清单：覆盖需求/执行入口/数据集/通过判据，为判据权威）。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（system-quality.md v0.3）
- **前置任务及必需工件：** 任务级无（包内首任务）；WP 级：WP-01-T03/T04（`run-tests.ps1` 门禁入口与 CI）、WP-02-T01～T03（黄金数据集与断言库，含 `StableSetAssertions`）、对应阶段被测模块测试目标与 failpoint 夹具（WP-04～21 各域按模块详设 §1 节奏分阶段接入）；工件：WP-02 数据集可引用（models/requirements/trajectory/dynamics/catalog/optimization/collision/faultpoints/performance）。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/system/`）创建 `include/sdurws/ird/system/AtRegistry.hpp`、`src/AtRegistry.cpp`、`scenarios/at-01/`～`scenarios/at-19/`（19 个场景目录，含各阶段子集骨架）、`test/SystemSuiteTest.cpp`；修改 `testkit/` CMakeLists（登记 `sdurws_ird_system_suite_test` 与聚合别名 `sdurws_ird_system_test`，后者运行全部五域）；写 `evidence/WP-23/`。不删除文件。
- **禁止修改的文件和公共接口：** 业务实现与各模块测试源（只调用不改）；WP-02 testkit 黄金数据（只读，不写回 testdata）；`benchmark-manifest.json`（只读输入）；`requirements.md`、CSV；不替代 WP-01 构建门禁；绕过统一测试入口的私有脚本。
- **修改前接口：** `testkit/system/` 目录与 `sdurws_ird_system_suite_test`/`sdurws_ird_system_test` 目标不存在；AT-01～19 无统一登记的执行入口、数据集与判据。
- **修改后接口：** `AtRegistry` 登记 19 条执行条目，逐条含覆盖需求（§16 行）、执行入口、数据集（WP-02）、通过判据，判据与 system-quality.md §3 表逐行一致（如 AT-01：DH/显式/URDF 三入口建同一模型，Origin/Axis/限制/Home/Zero/FK 在 §15.3 关节/FK 容差内一致；AT-07：相对误差≤1e-6（近零下限 1e-8）、h 与 h/2 收敛、峰值可定位；AT-09：可行 Pareto 集不含硬约束失败候选＋固定种子复现；AT-13：主程序存活、项目可恢复、任务显示 Failed（A-GATE-05 同口径）；AT-19：三入口对象 ID 碰撞对、判定、原因完全一致）。CMake：`sdurws_ird_system_suite_test`（SystemSuiteTest）＋聚合别名 `sdurws_ird_system_test`（运行 system_suite/fault_injection/benchmark/determinism/release_gate 五域，供安装冒烟与 WP-24 引用）。
- **实施步骤：**
  1. 写 RED 测试（19 条登记完整性、判据逐行对齐 §3 表、每条至少含正常/边界/失败断言）。
  2. 实现 `AtRegistry` 与 `scenarios/at-01/`～`at-19/` 场景夹具（只消费 WP-02 数据集与各域测试目标）。
  3. 按模块详设 §1 节奏分阶段接入：A＝A-GATE-01～07 脚本化场景骨架；B＝AT-01～05、15～18；C＝AT-06～08、19；D＝AT-09～14 与 18/19 全链路。
  4. CMake 登记五域中的本域目标与聚合别名 `sdurws_ird_system_test`。
  5. 执行验证命令，产出 AT 执行矩阵证据。
- **RED 测试：** 实现前 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_system_suite_test$"` 零匹配；落地后 19 条逐条可执行且判据与 §3 表一致。
- **最小实现：** `AtRegistry`＋19 场景夹具＋执行矩阵；故障注入矩阵（T02）、基准（T03）、确定性（T04）、门禁（T05）不在本卡。
- **正常/边界/失败测试：**
  - 正常：Given models 三套等价模型，When 执行 AT-01，Then 权威类型正确且 FK/轴线/惯量在 §15.3 容差内一致。
  - 边界：Given AT-03 缺证据结果，When 求值，Then 判 `DataInsufficient` 且展示符合 evaluation-semantics §5（不与"不可行"混排）；Given AT-05 修改 TCP，Then 实际依赖下游 `Superseded` 显示"需要重算"、单独改电机成本不使运动学/轨迹失效。
  - 失败：Given 任一 AT 判据不满足或场景数据集缺失，When 执行，Then 非零并按 §15.4 登记缺陷，不得调整容差或跳过场景。
- **精确验证命令：**（仓库根、VS x64 环境；GUI 相关 AT 单独启动、一次一个并设 `QT_QPA_PLATFORM=windows`）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_system_suite_test$'`
  - `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_system_suite_test`
  - `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_system_suite_test$"`
  - 五域聚合复核：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_system_test$"`（聚合别名，运行全部五域）。
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；判据逐行引用 system-quality.md §3（不改写需求语义）；无写回 testdata、无绕过统一入口的私有脚本、无未经批准的 manifest 修改；固定种子与输入哈希进入每条证据。
- **证据工件：** `evidence/WP-23/t01-at-matrix.log`：AT-01～19 执行矩阵（每条含覆盖需求/入口/数据集/判据/结果/seed/threadCount/输入哈希/命令原文与 commit）。
- **提交格式：** `WP-23-T01: 系统验收测试套件`
- **停止与升级条件：** system-quality.md §3 判据与被测模块实际行为冲突、或 WP-02 数据集/断言库缺失时，停止并升级独立质量负责人；任何失败登记缺陷（§15.4），不得以关闭测试、调整容差或跳过场景方式"通过"。
