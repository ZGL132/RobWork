# WP-21-T02 约束与指标判定

- **Task ID / 需求 ID / ADR / 阶段：** WP-21-T02；OPT-03（拓扑/输入→运动学→碰撞→轨迹→动力→器件硬约束先行再软目标）、OPT-04（Pareto 非支配、无加权总分）、OPT-06（Quick 筛选不作正式证据）、OPT-07（八项指标与目标分层）、AT-09、NFR-PERF-04～06；ADR-004（谓词与枚举只消费权威定义）。阶段 D / R2。契约：`architecture/evaluation-semantics.md` §3～§4（`RequiredEvidenceProfile` 与 `isFormallyFeasible()` 唯一权威）、`architecture/symbol-registry.md`（SYM-OPT-007～010）、`architecture/candidate-compilation.md`；模块详设 `module-design/optimization.md` v0.3 §4 错误矩阵、§5.3。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-21-T01（`JointSearchOrchestrator` 公共头与批次候选流可用，T01 用例通过）；WP-08-T01～T05（调度与评估器注册）；WP-15～T01～T08、WP-16/17/19 各评估器交付（经 WP-08 返回 `ResultEnvelope`）；WP-05-T03（`FeasibilityVerdict` 谓词公共头）；WP-02-T03 硬约束失败候选样本。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/optimization/`）创建 `joint/include/sdurws/ird/opt/FeasibilityLayers.hpp`、`joint/src/FeasibilityLayers.cpp`、`test/FeasibilityLayersTest.cpp`、`testdata/optimization/` 本任务夹具；修改 `joint/src/JointSearchOrchestrator.cpp`（接入四层判定挂点）与模块 CMakeLists；写 `out/test-evidence/wp-21/<run-id>/`。不删除文件。
- **禁止修改的文件和公共接口：** WP-05 谓词实现（只消费 `FeasibilityVerdict/gaps`，不复制判定逻辑）；WP-20 definition/candidate 公共头；WP-16～19 评估器；WP-08 execution；`requirements.md`、CSV、`schemas/`；不新增 CMake 目标。
- **修改前接口：** 无四层判定类型；T01 编排产出的候选流未经可行集准入判定。
- **修改后接口：** `FeasibilityLayers.hpp` 提供 HardConstraint（Must 违反→`IRD-OPT-HARD-CONSTRAINT` 附 kind、实际值/阈值，候选标不可行不入可行集与 Pareto）、SoftConstraint（只警告/次级排序，不参与支配）、Metric（八项指标逐项带 unit 与来源 `ResultRef`）、Objective（需求 §9.3 四字段声明）四层判定；可行集准入统一调用 WP-05 `isFormallyFeasible()`——硬约束失败、证据不足（`DataInsufficient`）或 `Partial` 候选不得进入可行 Pareto 集，`RequiredEvidenceProfile` 缺口按 `gaps` 逐项列出。
- **实施步骤：**
  1. 写 RED 测试（四层判定矩阵、`Partial`/证据不足拒入、Quick 不作正式通过证据、gaps 内容、OPT-03 次序）。
  2. 按 symbol-registry §3 SYM-OPT-007～010 定义四层类型（引用不复述）。
  3. 实现硬约束链与 `IRD-OPT-HARD-CONSTRAINT` 映射（拓扑/输入→运动学→碰撞→轨迹→动力→器件）。
  4. 可行集准入接入 WP-05 谓词，拒入候选保留 `gaps` 诊断。
  5. Quick 结果仅按 evaluation-semantics §3 例外（可证明保守的硬淘汰）参与，不得作正式通过证据。
  6. 挂接 T01 编排，执行验证命令，写证据。
- **RED 测试：** 实现前 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_joint_test$"` 中 FeasibilityLayers 用例不存在/失败；落地后全部通过。
- **最小实现：** 四层类型＋硬约束链＋谓词准入＋gaps 输出；不做 Pareto 支配计算（T04 复用 WP-20 口径）、审计与鲁棒性（T04）。
- **正常/边界/失败测试：**
  - 正常：Given 全部必需评估器 `Completed + Pass/Warning(允许类别) + Complete`，When 判定，Then 候选进入可行集且 `formallyFeasible=true`，指标按 OPT-07 八项逐项带单位输出。
  - 边界：Given 某必需评估器仅 Quick 结果或证据等级差一档或警告类别未获允许，When 判定，Then 不进入可行集且 `gaps` 逐项列出（模式/等级/类别）。
  - 失败：Given 候选违反 Must 硬约束，When 判定，Then `IRD-OPT-HARD-CONSTRAINT` 附 kind 与实际值/阈值、候选不入可行集与 Pareto；Given `Partial`/`NotEvaluated` 诊断性结果，When 判定，Then 拒入（永不进入正式可行 Pareto 集）。
- **精确验证命令：**（仓库根、VS x64 环境）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_joint_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_joint_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_joint_test$"`
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；`FeasibilityLayers` 不复制 `isFormallyFeasible` 谓词体、不复制正交枚举定义；无加权总分；对 WP-16～19 无直接代码依赖。
- **证据工件：** `out/test-evidence/wp-21/<run-id>/t02-feasibility-layers.log`：判定矩阵结果（正常/边界/失败三线）、gaps 样例、命令原文与 commit。
- **提交格式：** `WP-21-T02: 约束与指标判定`
- **停止与升级条件：** evaluation-semantics §4 谓词签名与编排数据流无法对接、或 SYM-OPT-007～010 登记缺失时，停止并升级工作包所有者；实现者不得担任本卡独立验证者。
