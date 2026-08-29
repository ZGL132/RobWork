# WP-20-T03 静态硬约束

- **Task ID / 需求 ID / ADR / 阶段：**WP-20-T03；OPT-03（OPT-B 仅运动学与碰撞静态子集，需求 §8.7.1/ADR-003）、AT-09 静态子集；阶段 B / R1。契约：`architecture/candidate-compilation.md` §6、`architecture/evaluation-semantics.md` §3、`module-design/optimization.md` v0.3 §5.3
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源同 T01
- **前置任务及必需工件：**WP-20-T02（`CompiledCandidateArtifact` 工件）；WP-08-T05（调度装配——评估一律经 WP-08 调度注册评估器）；WP-15-T01（运动学评估器 FK）、WP-07-T02（共享 `CollisionEvaluator`）
- **允许创建/修改/删除的文件：**创建 `plugins/optimization/candidate/include/sdurws/ird/opt/StaticConstraints.hpp`、`candidate/src/StaticConstraints.cpp`、`test/StaticConstraintTest.cpp`；修改 `plugins/optimization/CMakeLists.txt`（仅追加本任务文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**WP-07/WP-15 评估器实现与签名（只经 WP-08 调度调用）；T01/T02 冻结语义；requirements/CSV/architecture/module-design；禁止轨迹/动力/器件求值（OPT-D 归 WP-21）
- **修改前接口：**无硬约束执行器
- **修改后接口：**`StaticConstraints`（模块私有执行器）：按"拓扑/输入→运动学→碰撞"固定顺序执行（optimization.md §4，不得重排）；OPT-B 硬约束仅运动学与碰撞静态子集（candidate-compilation §6；`kind` 值域按 §1：`MustCoverage/JointLimit/Collision/PathContinuity/DriveCapability/EvidenceProfile/StructuralBound`，OPT-B 只激活静态子集）；Must 违反 → `IRD-OPT-HARD-CONSTRAINT`（Engineering/Error，附 kind、实际值/阈值），候选不入可行集合与 Pareto；Quick 结果不得伪装为 Verified（Quick 不作正式通过证据，例外仅 §9.4 可证明保守的硬淘汰）；OPT-B 只允许可证明保守的直接淘汰，非保守规则不得启用（误淘汰审计实现归 WP-21）
- **实施步骤：**1) RED：写 `StaticConstraintTest` 失败矩阵断言；2) 定义执行器接口与 kind 值域（仅激活静态子集）；3) 按固定顺序接线 WP-08 调度（运动学→碰撞，拓扑/输入先行）；4) 实现不可行候选排除与诊断（kind＋实际值/阈值）；5) 三形式命令转绿并写证据
- **RED 测试：**`StaticConstraintTest`（先写先败）：`MustViolationExcludedFromFeasibleSet`（→ `IRD-OPT-HARD-CONSTRAINT` 附 kind/实际值/阈值，不入可行集）、`FixedEvaluationOrderNotReordered`（拓扑/输入→运动学→碰撞，顺序破坏即失败）、`QuickNeverReportsVerified`（Quick 模式结果不标记 `EvidenceLevel`/`EvaluationMode` 为正式通过）、`OnlyProvablyConservativeDirectEliminations`（非保守 Quick 规则启用即失败）、`StageDConstraintKindsRejected`（OPT-D 类 kind 在 OPT-B 激活 → `IRD-OPT-STAGE-LOCKED`）
- **最小实现：**静态硬约束执行器转绿；静态指标与 Pareto 在 T04；WP-21 联合约束不得实现
- **正常/边界/失败测试：**
  - 正常：Given 通过全部静态 Must 的候选，When 评估，Then 进入可行集合、逐约束结果与来源 `ResultRef` 完整
  - 边界：Given 恰在阈值的约束值，Then 按"实际值 vs 阈值"记录判定并保持确定性；Given Quick 预筛后需 Verified 的约束，Then 仅可证明保守项直接淘汰、其余升级求值
  - 失败：Given 任一 Must 失败或候选编译失败，When 评估，Then 候选不可行/淘汰并保留诊断原因，无部分可行状态
- **精确验证命令**（仓库根、VS x64；三形式，仅用登记目标）：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_definition_test$'`；`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_definition_test`；`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_definition_test$"`；预期退出码 0
- **diff 和禁止项检查：**diff 仅含允许清单；`grep -rniE "trajectory|inverseDynamics|selection" candidate/src/StaticConstraints.cpp` 零命中（无轨迹/动力/器件求值）；无本地碰撞算法（碰撞只经 WP-07 共享评估器）；评估调用只经 WP-08 调度端口
- **证据工件：**`plugins/optimization/out/test-evidence/wp-20/<run-id>/`——硬约束失败矩阵（kind×候选×实际值/阈值）、执行顺序记录、AT-09 静态子集证据、测试日志
- **提交格式：** `WP-20-T03: 实现静态硬约束执行器`

  - 新增静态硬约束执行与调度接线
  - 新增硬约束拒绝测试
  - 新增运行证据记录
- **停止与升级条件：**硬约束顺序与需求 §8.7.1/optimization.md §4 不一致、或需要非保守 Quick 淘汰才能满足性能时，停止并升级（非保守规则属 WP-21 审计管辖）；WP-07/WP-15 评估器端口不足时升级对应所有者
