# WP-15-T08 跨入口契约回归（运动学侧探针与稳定不变量）

- **Task ID / 需求 ID / ADR / 阶段：** WP-15-T08；ARC-05、NFR-COR-05＋AT-19（运动学入口与 WP-20 静态优化入口碰撞判定一致）；无直接关联 ADR；阶段 B / R1。契约：`module-design/kinematics.md` v0.3 §5.5/§6、`architecture/public-interfaces.md` §3～§4、`architecture/testing-contract.md`；本卡只交付运动学侧探针与 AT-19 三元组/夹具约定，跨入口集成比较由 WP-20-T08 唯一所有（单向依赖本卡，本卡不依赖 WP-20）。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（kinematics.md v0.3、需求 v0.8）
- **前置任务及必需工件：** WP-15-T06（`sdurws_ird_kinematics_contract_test` 骨架目标与评估器装配可用）；WP-15-T07（显示开关与会话入口可用）；WP-07-T02（共享 `CollisionEvaluator` 为两入口共同依赖）。禁止引用、等待或联调 WP-20 任何工件。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/kinematics/`）
  - 创建：`test/CrossEntryTest.cpp`（编入 `sdurws_ird_kinematics_contract_test` 目标；内含运动学入口探针与夹具构造器）、`out/test-evidence/wp-15/<run-id>/`（AT-19 阶段 B 运动学侧记录）
  - 修改：`CMakeLists.txt`（仅当骨架目标缺测试源登记时补一行）；不删除文件。
- **禁止修改的文件和公共接口：** 一切产品源码（本卡只写契约测试）；禁止复制碰撞规则或第二套策略实现（发现即停止）；WP-07 `CollisionEvaluator`、WP-05 `ResultEnvelope`、WP-08 调度接口、WP-20 静态入口内部实现；T01～T07 已交付类型；测试运行期禁止写回 `testdata/`。
- **修改前接口：** 无（契约测试新增；T06 已登记空的 `sdurws_ird_kinematics_contract_test` 骨架目标）。
- **修改后接口：** ①CTest 目标 `sdurws_ird_kinematics_contract_test` 运动学侧 AT-19 不变量用例——Given 同一快照与同一 `CollisionPolicy`，When 经 `ird.kinematics`（T05 碰撞证据）重复求值或翻转显示开关（可见性/透明度等 UI 态），Then 三元组逐字节一致、判定与原因不变、不创建修订。②**AT-19 探针约定（冻结，供 WP-20-T08 单向消费）**：跨入口比较对象为三元组＝按 `(ownerScopeId, objectId)` 排序的对象 ID 对列表＋判定＋原因列表，序列化为稳定字段顺序的 JSON；夹具＝同一 `AnalysisSnapshot`（canonical 身份＋nameMapId＋策略内容 ID）喂两入口。
- **实施步骤：**
  1. 先写 `CrossEntryTest.cpp` 全部 RED 断言，构建确认失败。
  2. 构造夹具构造器与运动学入口探针：同一 `AnalysisSnapshot`（canonical 身份＋nameMapId＋策略内容 ID）经 `ird.kinematics` 求值，输出上述冻结三元组。
  3. 断言重复求值三元组逐字节一致（序列化比较，确定性）。
  4. 断言显示开关翻转前后三元组、判定与修订状态不变。
  5. 按验证命令转绿，写证据并提交；跨入口集成比较不在本卡（WP-20-T08 按本卡约定实现并验证）。
- **RED 测试：** `KinematicsEntryDeterministicTriple`；`CrossEntryFixtureReproducible`；`DisplayToggleDoesNotChangeVerdict`；`DisplayToggleDoesNotCreateRevision`。
- **最小实现：** 仅运动学侧不变量用例与探针约定；不新增任何产品代码；跨入口比较与优化入口调用不在本卡范围。
- **正常/边界/失败测试：**
  - 正常：Given 同一快照/策略下的无碰与碰撞场景各一，When 经运动学入口重复求值，Then 探针三元组逐字节一致且与 WP-07 共享评估器直接输出一致。
  - 边界：Given 允许接触对/排除对组合与安全距离边界候选，When 运动学入口求值，Then 探针透传共享评估器结论，无本地归一化或副本。
  - 失败：Given 本卡测试目录出现第二套策略实现或本地碰撞参数副本（静态可查），When 契约测试运行，Then 记录不一致证据并停止升级，不得以测试侧归一化掩盖差异。
- **精确验证命令：**（仓库根目录、VS x64 环境；第一形式必执行，脚本不可用时按回退顺序执行原生两形式）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_kinematics_contract_test$'`；预期退出码 0。
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_kinematics_contract_test`；预期构建成功。
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_kinematics_contract_test$"`；预期全部通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `test/CrossEntryTest.cpp`（与必要的 CMake 一行登记）与证据文件；无策略/评估逻辑复制；无产品源码改动；`check-boundaries.ps1` 零违规。
- **证据工件：** `out/test-evidence/wp-15/<run-id>/at19-cross-entry-record.md`（运动学侧不变量矩阵、探针三元组样例与序列化约定文本、显示开关记录；约定文本供 WP-20-T08 单向引用）＋测试日志（命令、commit、配置）；独立评审者签署 AT-19 运动学侧记录。
- **提交格式：** `WP-15-T08: 新增运动学入口跨入口探针与稳定不变量测试`

  - 新增 运动学入口探针与 AT-19 三元组/夹具冻结约定（供 WP-20-T08 单向消费）
  - 新增 重复求值确定性与显示开关不变量契约测试
  - 新增 AT-19 阶段 B 运动学侧证据记录
- **停止与升级条件：** 发现第二套碰撞策略实现，或需要变更公共接口/探针约定时停止并升级架构负责人；本卡不等待 WP-20 任何工件，不因对侧进度阻塞；实现者不得担任本卡独立验证者。
