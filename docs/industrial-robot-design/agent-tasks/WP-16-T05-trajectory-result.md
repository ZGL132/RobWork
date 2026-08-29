# WP-16-T05 轨迹结果与候选预览

- **Task ID / 需求 ID / ADR / 阶段：** WP-16-T05；TRJ-06（轨迹结果输出：`TrajectoryPlan`＋`ResolvedIkBranchSequence`、分段诊断与节拍）＋AT-04（候选双击只预览）；无直接关联 ADR；阶段 C / R1。契约：`module-design/trajectory-planning.md` v0.3 §3（Schema v1 冻结）/§4（数据流）、`architecture/evaluation-semantics.md` §2、`architecture/public-interfaces.md` §3/§7；`TrajectoryPlan`/`ResolvedIkBranchSequence`/`IkBranchPolicy` 为本模块拥有的公共领域类型（需求 §7.2）。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（trajectory-planning.md v0.3、需求 v0.7）
- **前置任务及必需工件：** WP-16-T01～T04 全部完成（段生成、规划器、时间参数化、复检可用）；WP-05-T03（`IEngineeringEvaluator`/`ResultEnvelope`）；WP-05-T04（结果接纳）；WP-08-T02（调度契约引用，装配归 WP-16-T06）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/trajectory/`）
  - 创建：`include/sdurws/ird/trajectory/TrajectoryPlan.hpp`、`ResolvedIkBranchSequence.hpp`；修改 `include/sdurws/ird/trajectory/TrajectoryEvaluator.hpp`（完整装配）与 `src/TrajectoryEvaluator.cpp`（评估主流程）
  - 创建：`src/TrajectoryJson.cpp`、`test/TrajectoryResultTest.cpp`、`evidence/WP-16/`（本卡工件）
  - 修改：`CMakeLists.txt`（新源文件编入 `sdurws_ird_trajectory` 与 `sdurws_ird_trajectory_test`）；不删除文件。
- **禁止修改的文件和公共接口：** WP-15 `KinematicResult`（契约引用，不复制完整 payload）、WP-14 需求类型、WP-05 `ResultEnvelope` 语义与接纳端口、WP-07 复检协议；`TrajectoryPlan` Schema v1 字段集与第二套 DTO 禁令；WP-16-T01～T04 已交付内部类型签名；Qt Widgets、直接文件 IO、读取 UI 会话态；测试运行期禁止写回 `testdata/`。
- **修改前接口：** 无（公共领域类型新增；`TrajectoryEvaluator.hpp` 已有 T01 端口骨架，本卡完成装配不破坏其签名）。
- **修改后接口：** `TrajectoryPlan`（对象 Schema 版本 1）：`segments[]`（`kind ∈ {JointPtp, CartesianLine, Dwell}`；每段路点、`tStart/tDuration`、来源任务点引用；驻留段携带持续时间与 `LoadCase` 工况切换引用）、时间参数（节点时刻表、每轴速度/加速度剖面采样、`motionLawId+version`、含驻留总节拍 `cycleTime`）、时间戳单调有限；`ResolvedIkBranchSequence`：每路点实际采用 IK 解（q、分支标识、残差、候选 ID），引用 `KinematicResult` 候选不复制完整 payload；只读采样求值（q/v/a at t）供 WP-17 复算与 WP-10 播放；`dependencyManifest()` 声明（canonical 物理身份、nameMapId、策略内容身份含 `pathValidationProfile`、上游 `KinematicResult` 引用、需求/负载字段、`algorithmVersion`＋`randomSeed`）；`ResultEnvelope` 填充（合法组合按 evaluation-semantics §2）；JSON 往返（Qt Core `QJson*`）。
- **实施步骤：**
  1. 先写 `TrajectoryResultTest.cpp` 全部 RED 断言，构建确认失败。
  2. 定义 `TrajectoryPlan`/`ResolvedIkBranchSequence` 公共类型（Schema v1 字段与不变式）。
  3. 实现 `TrajectoryEvaluator` 评估主流程装配（T01～T04 输出 → 校验 → envelope）。
  4. 实现只读采样求值与 `dependencyManifest()` 声明。
  5. 实现 `TrajectoryJson.cpp` JSON 往返与快照身份记录。
  6. 按验证命令三形式转绿，写证据并提交。
- **RED 测试：** `ResolvedIkBranchSequenceRecordsAdoptedSolutions`（每路点 q/分支/残差/候选 ID，P0 验收）；`TrajectoryPlanPayloadCompletePerSchemaV1`；`EnvelopePassesLegalCombinationValidation`；`DependencyManifestDeclaresAllSlices`；`JsonRoundTripPreservesPlan`；`CandidatePreviewDoesNotCreateRevision`（AT-04）。
- **最小实现：** 仅结果装配与只读消费转绿所需；取消/恢复/迟到回调与确定性重复运行证明归 WP-16-T06；播放/曲线查看会话态归 WP-10/WP-22（本卡只提供只读求值接口）。
- **正常/边界/失败测试：**
  - 正常：Given T01～T04 完整输出与合法切片，When 评估，Then `TrajectoryPlan`＋`ResolvedIkBranchSequence` 经 WP-05 接纳且 JSON 往返逐字段一致。
  - 边界：Given 双击候选与"用于规划/锁定分支"两种入口，When 操作，Then 前者仅预览不产生修订，后者才产生设计修改修订（AT-04）。
  - 失败：Given 合法组合校验失败或 payload 缺声明字段，When 接纳，Then 构造边界拒绝并返回稳定诊断，不产生部分正式结果。
- **精确验证命令：**（仓库根目录、VS x64 环境；三形式任选其一必须通过）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_trajectory_test$'`；预期退出码 0。
  - `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_trajectory_test`；预期构建成功。
  - `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_trajectory_test$"`；预期全部通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单文件；`TrajectoryPlan` 仅此一处定义（无第二套 DTO）；`ResolvedIkBranchSequence` 未复制 `KinematicResult` 完整 payload；无 Qt Widgets/直接文件 IO；`check-boundaries.ps1` 零违规。
- **证据工件：** `evidence/WP-16/trajectory-result-report.md`（Schema v1 字段清单、`ResolvedIkBranchSequence` 记录案例、JSON 往返哈希对照、AT-04 预览/修订对照）＋测试日志（命令、commit、配置、manifest 哈希）；独立验证者复核 payload 完整性与候选引用不复制。
- **提交格式：** `WP-16-T05: 轨迹结果与候选预览`
- **停止与升级条件：** Schema v1 字段与需求 §7.2/模块详设 §3 冲突、或 WP-05 接纳端口无法承载 payload 时停止并升级架构负责人；实现者不得担任本卡独立验证者。
