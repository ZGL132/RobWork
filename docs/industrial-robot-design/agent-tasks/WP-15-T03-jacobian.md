# WP-15-T03 Jacobian 与奇异性

- **Task ID / 需求 ID / ADR / 阶段：** WP-15-T03；KIN-01（Jacobian、奇异值、条件数、可操作度与第 15.3 节统一尺度规则部分）；无直接关联 ADR；阶段 B / R1。契约：`module-design/kinematics.md` v0.3 §3/§5.3、`architecture/canonical-kinematics.md`（规范链参考系）、`architecture/evaluation-semantics.md` §1～§2；`L*`/`J_norm` 尺度以需求 §15.3 冻结为准。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（kinematics.md v0.3、需求 v0.7）
- **前置任务及必需工件：** WP-15-T01（`FkJacobian.hpp`/`KinematicsDiagnostics.hpp` 与 FK 路径可用，fk-golden 已入 WP-02 manifest）；WP-14-T03（4/5 轴任务受约束分量声明 REQ-01，供任务子空间 Jacobian 消费）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/kinematics/`）
  - 修改：`include/sdurws/ird/kinematics/FkJacobian.hpp`（新增 `JacobianResult` 类型）、`src/FkJacobian.cpp`（Jacobian 部分）、`include/sdurws/ird/kinematics/KinematicsDiagnostics.hpp`（新增 `IRD-KIN-LSTAR-INVALID` 常量）、`CMakeLists.txt`（新测试源编入 `sdurws_ird_kinematics_test`）
  - 创建：`test/JacobianTest.cpp`、`testdata/kinematics/jacobian/`（解析对照夹具，登记 WP-02 manifest）、`evidence/WP-15/`（本卡工件）；不删除文件。
- **禁止修改的文件和公共接口：** 旧插件 `sdurws_kinematicanalysis` 及一切非本拥有目录源码；T01 已交付的 `FkQuery/FkResult` 语义；T02 的 `IkSolver/CandidateRanking`；WP-06/07/08 公共接口；自行定义 `L*` 尺度或扩大 §15.3 容差；不新增 symbol-registry 未登记公共符号；测试运行期禁止写回 `testdata/`。
- **修改前接口：** 无（Jacobian 能力新增；T01 后 `FkJacobian.hpp` 仅含 `FkQuery/FkResult`）。
- **修改后接口：** `JacobianResult`＝{J、`J_norm`、σmin/σmax、条件数、可操作度、`L*` 及来源/回退诊断}；冻结规则：`J_norm=[J_v/L*; J_ω]`，可操作度＝√det(J_norm·J_normᵀ)；`L*` 默认 Zero 位姿基座原点到 TCP 距离，回退须项目配置正值并记诊断，无法确定→DataInsufficient；4/5 轴任务用受约束分量构成的任务子空间 Jacobian（分量由 REQ-01 声明），可操作度/条件数一律基于（任务）`J_norm`；诊断 `IRD-KIN-LSTAR-INVALID`（Engineering/Error）。
- **实施步骤：**
  1. 先写 `JacobianTest.cpp` 全部 RED 断言，构建确认失败。
  2. 实现 `J_norm` 计算（`J_v/L*` 缩放线速度块）与 σmin/σmax、条件数、可操作度。
  3. 实现 `L*` 默认口径（Zero 位姿基座原点到 TCP 距离）、项目配置正值回退与诊断、DataInsufficient 分支。
  4. 实现任务子空间 Jacobian（按 REQ-01 受约束分量提取）。
  5. 生成 jacobian 解析对照夹具并登记 WP-02 manifest（版本/SHA-256）。
  6. 按验证命令三形式转绿，写证据并提交。
- **RED 测试：** `JacobianNormMatchesAnalyticReference`（解析对照 1e-6）；`ManipulabilityUsesNormalizedJacobian`；`LStarDefaultsToZeroPoseReach`；`LStarFallsBackToConfiguredPositiveWithDiagnostic`；`LStarInvalidReturnsDataInsufficient`；`TaskSubspaceJacobianUsesConstrainedComponents`（4/5 轴）。
- **最小实现：** 仅 Jacobian 路径与上述断言转绿所需；IK 归 WP-15-T02、区域覆盖归 WP-15-T04（本卡不消费）。
- **正常/边界/失败测试：**
  - 正常：Given 合法 canonical 工件与 q，When 计算，Then `J_norm` 与解析对照差 ≤1e-6，可操作度/条件数与黄金值一致。
  - 边界：Given 4/5 轴任务（REQ-01 分量集）与 `L*` 处于配置回退，When 计算，Then 任务子空间 `J_norm` 只含受约束分量且回退诊断可见。
  - 失败：Given `L*` 非有限/非正且无配置回退，When 计算，Then 返回 `IRD-KIN-LSTAR-INVALID` 且判定 DataInsufficient，不输出可操作度。
- **精确验证命令：**（仓库根目录、VS x64 环境；三形式任选其一必须通过）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_kinematics_test$'`；预期退出码 0。
  - `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_kinematics_test`；预期构建成功。
  - `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_kinematics_test$"`；预期全部通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单文件；`L*` 未引入第二套尺度定义；无 Qt Widgets/本地碰撞策略/自建调度；jacobian 夹具全部有 source/generationMethod 并入 WP-02 manifest；`check-boundaries.ps1` 零违规。
- **证据工件：** `evidence/WP-15/jacobian-analytic-report.md`（`J_norm`/`L*`/条件数解析对照、最大误差、回退案例）＋测试日志（命令、commit、配置、manifest 哈希）；独立验证者复核解析对照与 §15.3 尺度一致性。
- **提交格式：** `WP-15-T03: implement jacobian`
- **停止与升级条件：** `J_norm`/`L*` 尺度与需求 §15.3 或 canonical-kinematics 冲突时停止并升级架构负责人；实现者不得担任本卡独立验证者。
