# WP-16-T03 路径平滑与时间参数化

- **Task ID / 需求 ID / ADR / 阶段：** WP-16-T03；TRJ-04（路径简化与平滑）、TRJ-05（至少加速度连续的运动律按关节速度/加速度限制时间参数化，输出总节拍与分段时间）＋§15.3 轨迹限制行；无直接关联 ADR；阶段 C / R1。契约：`module-design/trajectory-planning.md` v0.3 §3/§5.1/§5.2（选型裁决与流程冻结）、`architecture/canonical-kinematics.md`（关节单位）、`architecture/testing-contract.md`。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（trajectory-planning.md v0.3、需求 v0.8）
- **前置任务及必需工件：** WP-16-T01（段 Schema 与 IK 连续性可用）；WP-16-T02（`PlannerAdapter` 无碰路径可用）；WP-02-T01/T02（黄金夹具登记与数值断言库）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/trajectory/`）
  - 创建：`src/PathSimplifier.cpp`、`src/TimeParameterizer.cpp`、`test/SmoothingTimeTest.cpp`、`testdata/trajectory/golden/`（黄金时间参数夹具，登记 WP-02 manifest 版本/哈希）、`out/test-evidence/wp-16/<run-id>/`（本卡工件）
  - 修改：`CMakeLists.txt`（新源文件编入 `sdurws_ird_trajectory` 与 `sdurws_ird_trajectory_test`）、`include/sdurws/ird/trajectory/TrajectoryDiagnostics.hpp`（新增 `IRD-TRJ-TIME-PARAM-FAILED` 常量）；不删除文件。
- **禁止修改的文件和公共接口：** RobWork `ParabolicBlend`/`CubicSplineFactory` 等运动律 API（裁决不复用其运动律，只复用几何插值 `LinearInterpolator`/`CircularInterpolator` 与轨迹容器）；WP-16-T01/T02 已交付类型与签名；时间参数化选型、1e-6/1.1/32 冻结值；一切非本拥有目录源码；测试运行期禁止写回 `testdata/`。
- **修改前接口：** 无（模块内新增）。
- **修改后接口：** 模块私有路径简化器：剔除零位移段并合并关节空间共线路点（直线偏差容差 1e-6 rad/m，模块私有可评审），不改任务点语义、路点保留来源引用；模块私有时间参数化器（冻结流程）：段几何度量 → 同步梯形速度剖面求节点时刻（多轴取最慢轴同步缩放）→ 五次样条重构连续运动律（节点给定位移/速度/加速度边界，C²，`motionLawId+version` 入记录）→ 按 §15.3 轨迹限制行校验（位置/速度/加速度相对限值超差 ≤1e-6）→ 违规按固定因子 1.1 全局延长时间并重构（上限 32 次，超限 `IRD-TRJ-TIME-PARAM-FAILED`）；笛卡尔段先几何插值再离散转关节空间，同时校验笛卡尔速度/加速度限值；输出节点时刻表、每轴速度/加速度剖面采样与含驻留总节拍 `cycleTime`。
- **实施步骤：**
  1. 先写 `SmoothingTimeTest.cpp` 全部 RED 断言，构建确认失败。
  2. 实现 `PathSimplifier`（零位移剔除、共线合并、来源引用保留）。
  3. 实现梯形剖面节点时刻与五次样条重构（C²、边界条件）。
  4. 实现限值校验与 1.1 延长因子 × 32 次迭代循环及超限诊断。
  5. 实现笛卡尔段离散转关节空间与笛卡尔速度/加速度限值校验。
  6. 生成黄金时间参数夹具并登记 WP-02 manifest，按验证命令三形式转绿，写证据并提交。
- **RED 测试：** `SimplifierRemovesZeroDisplacementAndCollinearWaypoints`；`SimplifierKeepsTaskPointSemantics`；`QuinticSplineMatchesC2AtKnots`（节点加速度一致）；`LimitViolationWithinRelativeTolerance1e-6`；`ExtensionFactor1_1UpTo32IterationsThenFail`；`GoldenTimeParameterReproducesExactly`。
- **最小实现：** 仅路径简化与时间参数化转绿所需；碰撞复检归 WP-16-T04、`TrajectoryPlan` 装配归 WP-16-T05（本卡输出节点时刻表/剖面采样/节拍供其消费）。
- **正常/边界/失败测试：**
  - 正常：Given T02 无碰路径与限值切片，When 简化＋时间参数化，Then 输出 C² 运动律、限值超差 ≤1e-6 相对、节拍含驻留，黄金夹具复算逐字节一致。
  - 边界：Given 共线路点偏差恰为 1e-6 rad/m 与最慢轴同步缩放边界，When 处理，Then 合并/保留判定确定、多轴时间同步不越限。
  - 失败：Given 32 次延长后仍超限的限值切片，When 时间参数化，Then `IRD-TRJ-TIME-PARAM-FAILED` 且不输出部分运动律。
- **精确验证命令：**（仓库根目录、VS x64 环境；第一形式必执行，脚本不可用时按回退顺序执行原生两形式）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_trajectory_test$'`；预期退出码 0。
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_trajectory_test`；预期构建成功。
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_trajectory_test$"`；预期全部通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单文件；未复用 `ParabolicBlend`/`CubicSplineFactory` 运动律；1e-6/1.1/32 冻结值未被改写；golden 夹具有 source/generationMethod 并入 WP-02 manifest；`check-boundaries.ps1` 零违规。
- **证据工件：** `out/test-evidence/wp-16/<run-id>/time-parameter-report.md`（C² 节点对照、限值超差统计、迭代收敛曲线、黄金复算记录、简化前后任务点语义对照）＋测试日志（命令、commit、配置、manifest 哈希）；独立验证者复核冻结值与黄金数据。
- **提交格式：** `WP-16-T03: 路径平滑与时间参数化`
- **停止与升级条件：** 五次样条无法同时满足 C² 与限值守恒、或 1e-6/1.1/32 冻结值不可达成时停止并升级规划负责人（冻结值评审须经架构批准，实现不得自行放宽）；实现者不得担任本卡独立验证者。
