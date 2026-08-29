# WP-16-T04 轨迹碰撞与运动限制复检

- **Task ID / 需求 ID / ADR / 阶段：** WP-16-T04；TRJ-04（平滑后按 §15.3 冻结碰撞验证协议重新验证碰撞与关节限制）＋AT-19（三入口碰撞一致）；无直接关联 ADR；阶段 C / R1。契约：`module-design/trajectory-planning.md` v0.3 §4/§5.4（引用协议，实施不得偏离）、`architecture/evaluation-semantics.md` §1～2、`architecture/public-interfaces.md` §3；协议权威为需求 §15.3 与 WP-07 交付。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（trajectory-planning.md v0.3、需求 v0.7）
- **前置任务及必需工件：** WP-16-T03（平滑与时间参数化输出可用）；WP-07-T02（共享 `CollisionEvaluator`）；WP-07-T03（`pathValidationProfile` 路径采样与分辨率协议：自适应细分、安全距离、允许接触对、分辨率入快照）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/trajectory/`）
  - 创建：`src/LimitVerifier.cpp`、`test/CollisionLimitsTest.cpp`、`testdata/trajectory/collision/`、`testdata/trajectory/failpoints/`（复检失败点与验证不足夹具，登记 WP-02 manifest）、`evidence/WP-16/`（本卡工件）
  - 修改：`CMakeLists.txt`（新源文件编入 `sdurws_ird_trajectory` 与 `sdurws_ird_trajectory_test`）、`include/sdurws/ird/trajectory/TrajectoryDiagnostics.hpp`（新增 `IRD-TRJ-VALIDATION-REJECTED` 常量）；不删除文件。
- **禁止修改的文件和公共接口：** WP-07 `CollisionPolicy`/`CollisionEvaluator`/`pathValidationProfile`（只调用，不得覆盖启用状态、配对、安全距离或分辨率）；WP-16-T01～T03 已交付类型与签名；结论措辞冻结句；一切非本拥有目录源码；无本地碰撞开关副本；测试运行期禁止写回 `testdata/`。
- **修改前接口：** 无（模块内新增；基线插件私有路径验证参数与碰撞开关副本属删除项，仅作 WP-07 静态扫描对照）。
- **修改后接口：** 模块私有 `LimitVerifier`：限值守恒校验（位置/速度/加速度，含笛卡尔段速度/加速度限值，相对超差 ≤1e-6）＋平滑后协议化复检——调用同一共享 `CollisionEvaluator` 与 `pathValidationProfile`（§15.3 冻结协议：自适应细分、结论措辞冻结"在本策略与分辨率下未发现碰撞"）；验证不足（距离查询不可用/证据缺失）＝DataInsufficient，不得视为无碰撞；复检发现碰撞或限制超标 → `IRD-TRJ-VALIDATION-REJECTED`（Engineering/Error，透传 WP-07 证据：对象 ID 对、段与分辨率）。
- **实施步骤：**
  1. 先写 `CollisionLimitsTest.cpp` 全部 RED 断言，构建确认失败。
  2. 实现 `LimitVerifier` 限值守恒部分（关节与笛卡尔段限值，消费 WP-16-T03 剖面采样）。
  3. 实现平滑后复检：构造共享 `CollisionEvaluator` 调用（同一快照/策略/`pathValidationProfile`），零策略项覆盖。
  4. 实现证据透传与 `IRD-TRJ-VALIDATION-REJECTED`、验证不足 DataInsufficient 分支。
  5. 生成 collision/failpoints 夹具并登记 WP-02 manifest，按验证命令三形式转绿，写证据并提交。
- **RED 测试：** `RecheckUsesSharedCollisionEvaluatorOnly`；`ThreeEntryVerdictsConsistent`（AT-19：运动学/轨迹/静态优化入口同快照同判定）；`InsufficientVerificationYieldsDataInsufficient`；`WordingFrozenToQualifiedNoCollision`；`ValidationRejectedTransfersWp07Evidence`；`CartesianSpeedAccelerationLimitsVerified`。
- **最小实现：** 仅限值校验与协议化复检转绿所需；碰撞策略实现归 WP-07、运动学侧碰撞证据归 WP-15-T05（均只复用同一共享评估器）；结果装配归 WP-16-T05。
- **正常/边界/失败测试：**
  - 正常：Given 平滑后轨迹与合法 `pathValidationProfile`，When 复检，Then 结论与 WP-07 共享评估器一致、措辞为冻结句、限值守恒报告完整。
  - 边界：Given 安全距离恰等与允许接触对命中样本，When 复检，Then 判定与 WP-07 及静态优化入口（AT-19）逐项一致。
  - 失败：Given 复检发现碰撞或限制超标，When 评估，Then `IRD-TRJ-VALIDATION-REJECTED` 附 WP-07 证据与段定位；Given 距离查询不可用，Then DataInsufficient，不输出无碰撞结论。
- **精确验证命令：**（仓库根目录、VS x64 环境；三形式任选其一必须通过）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_trajectory_test$'`；预期退出码 0。
  - `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_trajectory_test`；预期构建成功。
  - `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_trajectory_test$"`；预期全部通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单文件；无策略项覆盖（启用状态/配对/安全距离/分辨率静态扫描零命中）；措辞与冻结句逐字一致；夹具入 WP-02 manifest；`check-boundaries.ps1` 零违规。
- **证据工件：** `evidence/WP-16/collision-recheck-report.md`（平滑前后碰撞报告比对、AT-19 三入口对照、DataInsufficient 案例与措辞记录）＋测试日志（命令、commit、配置、manifest 哈希）；独立验证者复核共享评估器一致性与三入口一致。
- **提交格式：** `WP-16-T04: 轨迹碰撞与运动限制`
- **停止与升级条件：** `pathValidationProfile` 协议未冻结或无法在不覆盖策略项的前提下完成复检时停止并升级 WP-07/架构负责人；实现者不得担任本卡独立验证者。
