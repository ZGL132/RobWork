# WP-15-T01 FK 与规范模型

- **Task ID / 需求 ID / ADR / 阶段：** WP-15-T01；KIN-01（当前姿态 FK 与世界位姿部分，Jacobian 部分归 WP-15-T03）；无直接关联 ADR；阶段 B / R1。契约：`module-design/kinematics.md` v0.3 §2～§6、`architecture/canonical-kinematics.md`（最高权威）、`architecture/evaluation-semantics.md` §1～§2、`architecture/public-interfaces.md` §3～§4/§7。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（kinematics.md v0.3、需求 v0.7）
- **前置任务及必需工件：** WP-06-T03（`CompiledRobotArtifacts` 全成全败双编译可用）；WP-13-T06（模型→运行时编译链路可用）；WP-14-T01（`EngineeringRequirements` 数据模型与单位校验）；WP-02-T01/T02（manifest/loader 与数值断言库，用于 FK 黄金夹具登记与解析对照）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/kinematics/`）
  - 创建：`CMakeLists.txt`（登记 `sdurws_ird_kinematics`、`sdurws_ird_kinematics_test`，随 `IRD_BUILD_BUSINESS_PLUGINS`）
  - 修改：`../CMakeLists.txt`（plugins 聚合入口，仅新增 kinematics 子目录一行；聚合文件尚不存在时创建）
  - 创建：`include/sdurws/ird/kinematics/FkJacobian.hpp`、`KinematicsDiagnostics.hpp`（IRD-KIN-* 诊断码常量确保存在，kinematics.md §4 矩阵全量；后续任务卡仅确保存在、不重复创建）、`src/FkJacobian.cpp`（FK 部分）
  - 创建：`test/FkTest.cpp`；`testdata/kinematics/fk-golden/`（Zero/Home/边界/固定种子姿态解析对照夹具，登记进 WP-02 manifest）；`evidence/WP-15/`
- **禁止修改的文件和公共接口：** 旧插件 `sdurws_kinematicanalysis` 及一切非本拥有目录源码；`requirements.md`、CSV、文档门禁脚本；WP-06 canonical 模型与 `RuntimeNameMap`、WP-07 `CollisionPolicy`/`CollisionEvaluator`、WP-08 调度接口；不新增 symbol-registry 未登记的公共符号（`KinematicResult` 为已提名补登记项，SYM-KIN-006）；测试运行期禁止写回 `testdata/`。
- **修改前接口：** 无（`plugins/kinematics/` 不存在，无任何 `sdurws_ird_kinematics*` 目标）。
- **修改后接口：** `sdurws_ird_kinematics`（计算核心；允许依赖 WP-03/05/06/09 公共头、RobWork 稳定 API、Qt Core；禁止 Qt Widgets、本地 CollisionPolicy、自建调度）＋CTest `sdurws_ird_kinematics_test`；模块私有 `FkQuery/FkResult`——q 按 qIndex、tcpFrame；`T_WORLD_tcp`、各 Joint/Link 世界位姿、世界关节轴线（以规范链为准，不以 RobWork 适配链为准，canonical-kinematics §7）；FK 容差 §15.3（TCP 1e-9 m/1e-9 rad）。
- **实施步骤：**
  1. 创建 CMake 接入并先写 `FkTest.cpp` 全部 RED 断言，构建确认失败。
  2. 定义 `FkQuery/FkResult` 与 `KinematicsDiagnostics.hpp` 诊断码常量。
  3. 实现 FK：消费 `CompiledRobotArtifacts`，名称经 WP-06 `IRuntimeNameResolver` 解析；引用不可解析即失败，不取默认姿态。
  4. 用确定性生成器产出 fk-golden 解析对照夹具并登记 WP-02 manifest（版本/SHA-256）。
  5. 按验证命令三形式转绿，写证据并提交。
- **RED 测试：** `FkMatchesAnalyticReferencePoses`（Zero/Home/边界/固定种子姿态，TCP 1e-9 m/1e-9 rad）；`FkRejectsUnresolvableReference`（引用失败阻止求值且无结果对象）；`FkWorldJointAxesFollowCanonicalChain`（世界关节轴线按规范链）。
- **最小实现：** 仅 FK 路径与上述断言转绿所需；Jacobian、IK、覆盖、碰撞与评估器装配分别归 WP-15-T03/T02/T04/T05/T06。
- **正常/边界/失败测试：**
  - 正常：Given 合法 canonical 工件与 q，When FK，Then `T_WORLD_tcp`、各 Joint/Link 世界位姿与世界关节轴线与解析对照差 ≤1e-9 m/1e-9 rad。
  - 边界：Given q 取各关节下/上边界与 Zero/Home，When FK，Then 结果有限、逐轴单位正确（转动 rad/移动 m）、qIndex 与可动关节一一对应。
  - 失败：Given 名称/引用不可解析（`RuntimeNameMap` 缺项），When FK，Then 返回 Input 类稳定诊断且无部分结果，不回退默认姿态。
- **精确验证命令：**（仓库根目录、VS x64 环境；三形式任选其一必须通过）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_kinematics_test$'`；预期退出码 0。
  - `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_kinematics_test`；预期构建成功。
  - `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_kinematics_test$"`；预期全部通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `plugins/kinematics/` 新文件与 plugins 聚合 CMake 的一行接入；fk-golden 夹具全部有 source/generationMethod 并入 WP-02 manifest，无手写期望值；无 Qt Widgets/本地碰撞策略/自建线程池引用；`check-boundaries.ps1` 零违规。
- **证据工件：** `evidence/WP-15/fk-analytic-report.md`（姿态清单、解析值、最大误差、种子）＋测试日志（命令、commit、配置、manifest 哈希）；独立验证者复核解析对照。
- **提交格式：** `WP-15-T01: fk with canonical model and analytic fixtures`
- **停止与升级条件：** canonical-kinematics 变换链与 `CompiledRobotArtifacts` 实际输出不一致、或 §15.3 容差无法达成时停止并升级架构负责人；实现者不得担任本卡独立验证者。
