# WP-15-T04 区域覆盖

- **Task ID / 需求 ID / ADR / 阶段：** WP-15-T04；KIN-04（工作区域按位置与姿态采样，分别给出位置覆盖率与姿态覆盖率）；无直接关联 ADR；阶段 B / R1。契约：`module-design/kinematics.md` v0.3 §3/§5.4、`architecture/evaluation-semantics.md` §2（两锚点合法组合，冻结）、`architecture/execution-model.md` §1～§3；分母定义以 WP-14 冻结为准。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（kinematics.md v0.3、需求 v0.8）
- **前置任务及必需工件：** WP-15-T02（`IkCandidate` 过滤/排序可用）；WP-14-T03（冻结分母定义＝计划的位置—姿态组合 `positionSampleCount×orientationSampleCount`、网格含边界、采样预算与 `IRD-REQ-SAMPLING-BUDGET`）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/kinematics/`）
  - 创建：`include/sdurws/ird/kinematics/RegionCoverage.hpp`、`src/RegionCoverage.cpp`、`test/RegionCoverageTest.cpp`、`testdata/kinematics/regions/`（网格/边界/预算夹具，登记 WP-02 manifest）
  - 修改：`CMakeLists.txt`（新源文件编入 `sdurws_ird_kinematics` 与 `sdurws_ird_kinematics_test`）
  - 创建：`out/test-evidence/wp-15/<run-id>/`（本卡工件）；不删除文件。
- **禁止修改的文件和公共接口：** 旧插件 `sdurws_kinematicanalysis` 及一切非本拥有目录源码；WP-14 分母定义与采样持久化语义、WP-08 调度/取消实现、WP-07 `CollisionEvaluator`、T02 的 `IkSolver/CandidateRanking`；不得改写 `Canceled/NotEvaluated/Partial` 正交语义（evaluation-semantics §2）；不新增 symbol-registry 未登记公共符号；测试运行期禁止写回 `testdata/`。
- **修改前接口：** 无（新增）。
- **修改后接口：** 模块私有 `RegionCoverage::evaluate`；`CoverageReport`＝{plannedCombinations、evaluatedCombinations、达标组合数、positionCoverage、orientationCoverage}，分母＝WP-14 冻结的计划位置—姿态组合；冻结口径：网格含边界，位置覆盖＝存在至少一个达标姿态组合的采样位置占比，姿态覆盖＝存在至少一个达标位置组合的采样姿态占比，组合级统计同时保留；Verified 区域每空间轴至少两个样本（§15.3）；两锚点：用户取消＝`Canceled + NotEvaluated + Partial/None`，预算耗尽＝`Completed + DataInsufficient + Complete`，均不得输出 Verified 通过。
- **实施步骤：**
  1. 先写 `RegionCoverageTest.cpp` 全部 RED 断言，构建确认失败。
  2. 实现网格枚举（含边界）与分母计算（消费 WP-14 冻结定义，不自行计数）。
  3. 实现组合级达标判定（复用 T02 `IkCandidate` 可应用结论）与两级覆盖率统计。
  4. 实现取消与预算耗尽两锚点的 envelope 状态落位（经 evaluation-semantics §2 合法组合校验）。
  5. 生成 regions 夹具（分母/边界/预算案例）并登记 WP-02 manifest（版本/SHA-256）。
  6. 按验证命令三形式转绿，写证据并提交。
- **RED 测试：** `CoverageDenominatorMatchesFrozenDefinition`；`GridSamplesIncludeBoundary`；`UserCancelYieldsCanceledNotEvaluatedPartial`；`BudgetExhaustedYieldsCompletedDataInsufficientComplete`；`PartialCoverageNeverVerified`；`CoverageAxesRequireTwoSamples`（§15.3）。
- **最小实现：** 仅区域采样、达标统计与两锚点状态落位转绿所需；单任务点批量验证归 WP-15-T06；采样线程数消费 `KinematicsSettings`（装配归 T06）。
- **正常/边界/失败测试：**
  - 正常：Given 计划分母 N＝位置×姿态组合与固定种子，When 覆盖评估，Then 两级覆盖率与组合级统计按冻结口径复现。
  - 边界：Given 网格边界采样点与仅两样本的空间轴，When 评估，Then 边界点计入、Verified 区域满足每轴两样本。
  - 失败：Given 用户取消（已算部分组合），When 停止，Then `Canceled + NotEvaluated + Partial/None` 且不出现 Verified；Given 采样预算耗尽，Then `Completed + DataInsufficient + Complete` 且 payload 含逐项覆盖统计。
- **精确验证命令：**（仓库根目录、VS x64 环境；第一形式必执行，脚本不可用时按回退顺序执行原生两形式）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_kinematics_test$'`；预期退出码 0。
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_kinematics_test`；预期构建成功。
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_kinematics_test$"`；预期全部通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单文件；未改写/复制分母定义与采样预算常量（只消费 WP-14）；两锚点组合与 evaluation-semantics §2 一致且无第三落位；`check-boundaries.ps1` 零违规。
- **证据工件：** `out/test-evidence/wp-15/<run-id>/coverage-matrix.md`（分母/边界/两锚点/Partial 禁 Verified 结果矩阵、种子）＋测试日志（命令、commit、配置、manifest 哈希）；独立验证者复核覆盖率口径。
- **提交格式：** `WP-15-T04: 实现区域覆盖验证`

  - 新增区域覆盖与采样评估
  - 新增覆盖完整性测试
  - 新增运行证据记录
- **停止与升级条件：** 分母/采样口径与 WP-14 冻结定义不一致、或两锚点与 evaluation-semantics §2 无法同时满足时停止并升级架构负责人；实现者不得担任本卡独立验证者。
