# WP-15-T05 碰撞证据

- **Task ID / 需求 ID / ADR / 阶段：** WP-15-T05；KIN-05（缺碰撞检测器必须返回数据不足，不得视为无碰撞）＋AT-19 阶段 B 子链路；无直接关联 ADR；阶段 B / R1。契约：`module-design/kinematics.md` v0.3 §3/§5.5、`architecture/evaluation-semantics.md` §1～§2、`architecture/public-interfaces.md` §3～§4；碰撞规则与协议以 WP-07 交付为唯一权威。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（kinematics.md v0.3、需求 v0.8）
- **前置任务及必需工件：** WP-15-T01（FK 与 `KinematicsDiagnostics.hpp` 可用）；WP-15-T02（`IkCandidate.applicable.collision` 过滤分支与 `KinematicsSettings`）；WP-07-T02（共享 `CollisionEvaluator`）；WP-07-T03（路径采样与分辨率协议：自适应细分、安全距离、允许接触对、分辨率入快照）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/kinematics/`）
  - 创建：`include/sdurws/ird/kinematics/CollisionEvidenceAdapter.hpp`、`src/CollisionEvidenceAdapter.cpp`、`test/CollisionEvidenceTest.cpp`、`testdata/kinematics/collisions/`（共享评估器一致性夹具，登记 WP-02 manifest）
  - 修改：`CMakeLists.txt`（新源文件编入 `sdurws_ird_kinematics` 与 `sdurws_ird_kinematics_test`）、`include/sdurws/ird/kinematics/KinematicsDiagnostics.hpp`（新增 `IRD-KIN-COLLIDING`、`IRD-KIN-EVIDENCE-MISSING` 常量）
  - 创建：`out/test-evidence/wp-15/<run-id>/`（本卡工件）；不删除文件。
- **禁止修改的文件和公共接口：** 旧插件 `sdurws_kinematicanalysis` 及其插件内重复碰撞适配/开关（迁移表处置为删除，本卡不复制）；一切非本拥有目录源码；WP-07 `CollisionPolicy`/`CollisionEvaluator` 字段与语义（只调用不复制，无本地默认值/安全距离/采样参数副本）；WP-06/08/14 公共接口；不新增 symbol-registry 未登记公共符号；测试运行期禁止写回 `testdata/`。
- **修改前接口：** 无（模块内新增；基线旧链路为插件私有碰撞开关与采样参数副本，属删除项，仅作只读对照）。
- **修改后接口：** 模块私有 `CollisionEvidenceAdapter`：对候选/任务点调 WP-07 共享 `CollisionEvaluator`（同一快照与策略内容 ID），产出证据（对象 ID 对、策略内容 ID、分辨率、最近距离）并回填 `IkCandidate.applicable.collision`；结论措辞冻结"在本策略与分辨率下未发现碰撞"；距离查询不可用不推断安全；诊断 `IRD-KIN-COLLIDING`（Engineering/Error，报对象 ID 对与最近距离）、`IRD-KIN-EVIDENCE-MISSING`（Engineering/Error，判 DataInsufficient）。
- **实施步骤：**
  1. 先写 `CollisionEvidenceTest.cpp` 全部 RED 断言，构建确认失败。
  2. 实现 `CollisionEvidenceAdapter`：解析快照中的 `CollisionPolicy`（含 `pathValidationProfile`），构造 WP-07 评估器调用，不做任何策略默认值替换。
  3. 实现证据结构（对象 ID 对按 `(ownerScopeId, objectId)` 排序、策略内容 ID、分辨率、最近距离）与结论措辞常量。
  4. 实现缺检测器 → `IRD-KIN-EVIDENCE-MISSING` → DataInsufficient 分支，以及发现碰撞 → `IRD-KIN-COLLIDING` → 候选不可应用回填。
  5. 生成共享评估器一致性夹具并登记 WP-02 manifest（版本/SHA-256）。
  6. 按验证命令三形式转绿，写证据并提交。
- **RED 测试：** `EvidenceMatchesSharedEvaluatorVerdict`（同一快照/策略下与 WP-07 结论一致）；`MissingDetectorYieldsDataInsufficient`（KIN-05）；`WordingFrozenToQualifiedNoCollision`（措辞逐字一致）；`DistanceUnavailableDoesNotInferSafety`；`CollidingCandidateMarkedNotApplicable`（回填 T02 过滤分支）。
- **最小实现：** 仅运动学侧碰撞证据适配转绿所需；WP-20 静态入口一致性归 WP-15-T08；轨迹侧复检归 WP-16-T04（各自复用同一共享评估器）。
- **正常/边界/失败测试：**
  - 正常：Given 合法策略与无碰候选，When 查询证据，Then 输出对象 ID 对/策略内容 ID/分辨率/最近距离，措辞为冻结限定句。
  - 边界：Given 允许接触对命中与安全距离恰好相等，When 查询，Then 判定与 WP-07 共享评估器逐项一致（不本地重判）。
  - 失败：Given 快照缺碰撞检测器（KIN-05），When 查询证据，Then `IRD-KIN-EVIDENCE-MISSING` 且判定 DataInsufficient，不得视为无碰撞；Given 距离查询不可用，Then 不输出无碰撞结论。
- **精确验证命令：**（仓库根目录、VS x64 环境；第一形式必执行，脚本不可用时按回退顺序执行原生两形式）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_kinematics_test$'`；预期退出码 0。
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_kinematics_test`；预期构建成功。
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_kinematics_test$"`；预期全部通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单文件；无本地 `CollisionPolicy` 结构/默认值/安全距离/采样参数副本（只 include WP-07 公共头）；措辞常量与冻结句逐字一致；`check-boundaries.ps1` 零违规。
- **证据工件：** `out/test-evidence/wp-15/<run-id>/collision-evidence-report.md`（共享评估器一致性对照、缺检测器案例、措辞记录）＋测试日志（命令、commit、配置、manifest 哈希）；独立验证者复核与 WP-07 结论一致性。
- **提交格式：** `WP-15-T05: 新增碰撞证据回填`

  - 新增共享评估器碰撞证据回填
  - 新增证据一致性测试
  - 新增运行证据记录
- **停止与升级条件：** 共享 `CollisionEvaluator` 接口或 `pathValidationProfile` 协议未冻结、或无法在不复制策略的前提下取得证据时停止并升级 WP-07/架构负责人；实现者不得担任本卡独立验证者。
