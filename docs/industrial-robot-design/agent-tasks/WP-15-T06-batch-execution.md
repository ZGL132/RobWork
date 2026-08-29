# WP-15-T06 批处理执行

- **Task ID / 需求 ID / ADR / 阶段：** WP-15-T06；KIN-03（批量验证任务点，分别报告可行、工程不可行和数据不足）；无直接关联 ADR；阶段 B / R1。契约：`module-design/kinematics.md` v0.3 §4（固定时序与错误矩阵）、`architecture/execution-model.md` §1～§5、`architecture/evaluation-semantics.md` §2、`architecture/public-interfaces.md` §3～§4/§7。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（kinematics.md v0.3、需求 v0.7）
- **前置任务及必需工件：** WP-15-T02/T04/T05（IK 排序、区域覆盖、碰撞证据可用）；WP-08-T01/T02（`IEvaluationScheduler`、`EvaluationRequest`、`RunIdentity` 五元组与迟到保护）；WP-08-T03（`CancellationToken` 协作取消）；WP-05-T03（`IEngineeringEvaluator` 端口头与 `ResultEnvelope`）；WP-05-T04（结果接纳 `IResultRepository`）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/kinematics/`）
  - 创建：`include/sdurws/ird/kinematics/KinematicsEvaluator.hpp`、`KinematicsSettings.hpp`、`src/KinematicsEvaluator.cpp`、`src/KinematicsSettings.cpp`、`test/BatchExecutionTest.cpp`、`testdata/kinematics/batches/`（固定种子批次夹具，登记 WP-02 manifest）
  - 修改：`CMakeLists.txt`（新源文件编入 `sdurws_ird_kinematics`、`sdurws_ird_kinematics_test`，并登记 `sdurws_ird_kinematics_contract_test` 骨架目标）
  - 创建：`evidence/WP-15/`（本卡工件）；不删除文件。
- **禁止修改的文件和公共接口：** 旧插件 `sdurws_kinematicanalysis` 及一切非本拥有目录源码；WP-08 状态机/身份/缓存/检查点实现与 `IEvaluationScheduler` 冻结签名（只接入不自建线程池/调度）；WP-05 `ResultEnvelope`/接纳语义与评估端口签名；T01～T05 已交付类型；不读取当前 Widget 状态；不新增 symbol-registry 未登记公共符号（evaluatorId `ird.kinematics` 为模块冻结值）；测试运行期禁止写回 `testdata/`。
- **修改前接口：** 无（`ird.kinematics` 评估器装配新增；基线旧链路为插件内自建批量评估入口，属 Rewrite 对照项）。
- **修改后接口：** `IEngineeringEvaluator` 实现：evaluatorId `ird.kinematics`（冻结），`dependencyManifest/validate/evaluate/capabilities` 按 public-interfaces §3；批处理固定时序：WP-08 `submit(EvaluationRequest)`（`RunIdentity`＝{projectId, branchId, revisionId, runId, attemptId} 完整携带）→ worker 消费 `AnalysisSnapshot` → 批次＝任务点或区域采样组合（安全点＝批次边界）→ FK/IK/排序/覆盖/碰撞证据 → envelope（构造边界过 evaluation-semantics §2 校验）→ WP-05 接纳；`KinematicsSettings`（IK 初值数、迭代上限、内部收敛阈值、区域采样线程数）持久化为 `AnalysisConfiguration` 并进入输入切片与缓存键；诊断含 `IRD-KIN-SOLVER-FAILED`（System/Error，保留批次与检查点，新 attempt 重试）。
- **实施步骤：**
  1. 先写 `BatchExecutionTest.cpp` 全部 RED 断言，构建确认失败。
  2. 实现 `KinematicsSettings`（校验、持久化、进输入切片与缓存键）。
  3. 实现 `KinematicsEvaluator`：`dependencyManifest()` 声明切片来源，`validate()` 前置校验，`evaluate()` 按固定时序装配 T02/T04/T05。
  4. 接入 WP-08 调度：身份校验、批次边界安全点、取消/检查点消费 `CancellationToken`、迟到回调只追加原分支历史。
  5. 登记契约测试骨架目标 `sdurws_ird_kinematics_contract_test`（用例填充归 WP-15-T08）。
  6. 按验证命令三形式转绿，写证据并提交。
- **RED 测试：** `BatchRequiresCompleteRunIdentity`；`BatchFixedSequenceReproducesByteStable`（固定输入/种子/线程数）；`LateCallbackAppendsOriginalBranchHistoryOnly`；`CancelStopsAtBatchBoundaryWithCheckpoint`；`EnvelopePassesLegalCombinationValidation`；`SettingsEnterSliceAndCacheKey`。
- **最小实现：** 仅评估器装配与批处理接入转绿所需；GUI 入口归 WP-15-T07；AT-19 契约断言归 WP-15-T08；不实现任何 WP-08 内部机制。
- **正常/边界/失败测试：**
  - 正常：Given 合法切片与固定种子批次（任务点＋区域组合），When 批处理执行，Then 结果经 WP-05 接纳且重复运行逐字节复现。
  - 边界：Given 批次中途取消与检查点存在，When 恢复，Then 从批次边界续算、不重复已接纳结果、迟到回调只追加原分支历史。
  - 失败：Given 身份缺失/不匹配或 RobWork 求解器异常，When 提交/执行，Then 请求不入队或返回 `IRD-KIN-SOLVER-FAILED`，保留批次与检查点等待新 attempt。
- **精确验证命令：**（仓库根目录、VS x64 环境；三形式任选其一必须通过）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_kinematics_test$'`；预期退出码 0。
  - `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_kinematics_test`；预期构建成功。
  - `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_kinematics_test$"`；预期全部通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单文件；无自建线程池/调度/缓存/检查点实现（只调用 WP-08 端口）；无读取 Widget 状态；`check-boundaries.ps1` 零违规。
- **证据工件：** `evidence/WP-15/batch-execution-report.md`（身份校验矩阵、迟到回调/取消/检查点日志、固定种子复现对照、`KinematicsSettings` 进缓存键证明）＋测试日志（命令、commit、配置、manifest 哈希）；独立验证者复核身份与确定性。
- **提交格式：** `WP-15-T06: integrate batch execution`
- **停止与升级条件：** 任务状态转移/取消/检查点语义未被 WP-08 契约覆盖、或评估端口签名与 public-interfaces §3 不一致时停止并升级 WP-08/架构负责人；实现者不得担任本卡独立验证者。
