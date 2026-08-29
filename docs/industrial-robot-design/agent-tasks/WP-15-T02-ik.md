# WP-15-T02 IK 候选

- **Task ID / 需求 ID / ADR / 阶段：** WP-15-T02；KIN-02（多初值 IK：先过滤残差/关节/碰撞硬条件，再按裕量、可操作度、当前距离和稳定编号排序）；无直接关联 ADR；阶段 B / R1。契约：`module-design/kinematics.md` v0.3 §3/§5.1/§5.2（排序键与初值池模块冻结）、`architecture/canonical-kinematics.md`（q/qIndex、测地角）、`architecture/evaluation-semantics.md` §1～§2。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（kinematics.md v0.3、需求 v0.8）
- **前置任务及必需工件：** WP-15-T01（`FkJacobian.hpp` 的 `FkQuery/FkResult`、`KinematicsDiagnostics.hpp` 诊断码常量、`sdurws_ird_kinematics` 目标与 fk-golden 已入 WP-02 manifest）；WP-15-T03（`JacobianResult` 可操作度接口——排序键 (b) 消费）；WP-14-T03（需求切片：任务容差与 4/5 轴受约束分量声明 REQ-01）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/kinematics/`）
  - 创建：`include/sdurws/ird/kinematics/IkSolver.hpp`、`CandidateRanking.hpp`、`KinematicsSettings.hpp`＋`src/KinematicsSettings.cpp`（初版：IK 初值数、迭代上限、内部收敛阈值、区域采样线程数字段与默认值；T06 扩展）、`src/IkSolver.cpp`、`src/CandidateRanking.cpp`、`test/IkCandidateTest.cpp`、`testdata/kinematics/ik-golden/`（含 3627db1 回归夹具，登记 WP-02 manifest）
  - 修改：`CMakeLists.txt`（模块根 CMakeLists：新源文件编入 `sdurws_ird_kinematics` 与 `sdurws_ird_kinematics_test`）、`testdata/manifest.json`（ik-golden 哈希登记，testkit.md §8 授权）、`include/sdurws/ird/kinematics/KinematicsDiagnostics.hpp`（确保存在 `IRD-KIN-IK-NO-SOLUTION`、`IRD-KIN-JOINT-LIMIT` 常量；T01 已建骨架，仅补缺失项，不重复创建）
  - 创建：`out/test-evidence/wp-15/<run-id>/`（本卡工件）；不删除文件。
- **禁止修改的文件和公共接口：** 旧插件 `sdurws_kinematicanalysis` 及一切非本拥有目录源码；WP-06 canonical 模型/`IRuntimeNameResolver`、WP-07 `CollisionPolicy`/`CollisionEvaluator`、WP-08 调度接口、WP-04 候选应用命令；T01 已交付的 `FkQuery/FkResult` 语义；不新增 symbol-registry 未登记公共符号；解析 IK 夹具不得进生产路径；测试运行期禁止写回 `testdata/`。
- **修改前接口：** 无（模块内新增；基线旧链路 `sdurws_kinematicanalysis` 的候选选择只按"残差最小"排序，git 3627db1 修复前首候选可因 JointLimit 不可应用——仅作只读黄金对照，不迁移代码）。
- **修改后接口：** 模块私有 `IkSolver::solve`（RobWork 数值 IK 阻尼最小二乘多初值）与 `CandidateRanking::rank`（两阶段排序）；`IkCandidate`＝{q、residual{position m, orientation rad(测地角)}、jointMargin、manipulability、distanceToCurrent、applicable{residual,jointLimit,collision}、stableIndex}；诊断 `IRD-KIN-IK-NO-SOLUTION`（Engineering/Warning，列最近残差与失败原因）、`IRD-KIN-JOINT-LIMIT`（候选标不可应用并保留逐轴实际/限值）。
- **实施步骤：**
  1. CMake 接入新源文件，先写 `IkCandidateTest.cpp` 全部 RED 断言，构建确认失败。
  2. 实现 `IkSolver`：初值池固定次序 q_home → 会话当前 q → 关节区间中点 → 固定种子 Latin 超立方补足至默认 16 个（数量入 `KinematicsSettings`）；内部步长阈值 1e-10、每初值迭代上限 200；任务级收敛判据＝§15.3（位置 1 mm、姿态 1 deg 测地角）。
  3. 实现过滤阶段：残差超任务容差、关节超限、要求碰撞证据时碰撞失败者 `applicable=false`，移出主列表、单列分组并保留逐项原因（collision 输入由 WP-15-T05 适配喂入，本卡只留分支）。
  4. 实现去重（逐轴转动/连续差 ≤1e-6 rad、移动 ≤1e-8 m）与排序阶段：关节裕量降序 → 可操作度（基于 `J_norm`）降序 → 加权距离 ‖D⁻¹(q−q_cur)‖₂ 升序 → stableIndex 升序。
  5. 生成 ik-golden（含解析 IK 黄金对照与 3627db1 回归夹具）并登记 WP-02 manifest（版本/SHA-256）。
  6. 按验证命令三形式转绿，写证据并提交。
- **RED 测试：** `IkCandidateOrderingTest.ApplicableBeforeResidual`（3627db1 回归：残差最小但 JointLimit 不可应用的候选不得排首位，必须移出主列表）；`IkMultiStartConvergesWithinTaskTolerance`；`IkFiltersResidualOverToleranceWithReason`；`IkDedupPerAxisWithinTolerance`；`IkOrderingByteStableAcrossRuns`（固定输入/种子/线程数）。
- **最小实现：** 仅单目标多初值 IK、过滤、去重与两阶段排序转绿所需；碰撞证据采集归 WP-15-T05、区域采样与批量任务点归 WP-15-T04/T06、Jacobian 归 WP-15-T03（本卡仅消费其可操作度接口）。
- **正常/边界/失败测试：**
  - 正常：Given 标准六轴构型可达目标与固定种子，When 多初值 IK，Then 候选与解析 IK 黄金解集合一致，可应用候选按全序键排列且 stableIndex 确定。
  - 边界：Given 候选恰在容差边界（1 mm/1 deg、去重 1e-6 rad/1e-8 m）与关节边界，When 过滤/排序，Then 边界内保留、边界外单列，转动/移动各按自身半区间归一。
  - 失败：Given 全部初值未收敛或被过滤，When IK，Then 返回 `IRD-KIN-IK-NO-SOLUTION` 且无部分结果，不回退默认解。
- **精确验证命令：**（仓库根目录、VS x64 环境；第一形式必执行，脚本不可用时按回退顺序执行原生两形式）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_kinematics_test$'`；预期退出码 0。
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_kinematics_test`；预期构建成功。
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_kinematics_test$"`；预期全部通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单文件；无本地碰撞策略/自建线程池/读取 Widget 状态；解析 IK 夹具未进生产路径；ik-golden 全部有 source/generationMethod 并入 WP-02 manifest；`check-boundaries.ps1` 零违规。
- **证据工件：** `out/test-evidence/wp-15/<run-id>/ik-ordering-report.md`（排序键全序说明、种子/线程数、候选序列逐字节对照、3627db1 回归夹具说明）＋测试日志（命令、commit、配置、manifest 哈希）；独立算法验证者复核夹具与全序性。
- **提交格式：** `WP-15-T02: 实现 IK 候选求解与排序`

  - 新增 IK 候选求解与两阶段排序实现
  - 新增 IK 候选单元与契约测试
  - 新增运行证据记录
- **停止与升级条件：** 排序键/初值池与 kinematics.md §5.1/§5.2 冻结口径冲突、或 §15.3 容差无法达成时停止并升级架构负责人；实现者不得担任本卡独立验证者。
