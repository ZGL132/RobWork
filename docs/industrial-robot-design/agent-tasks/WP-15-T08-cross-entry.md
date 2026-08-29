# WP-15-T08 跨入口契约回归

- **Task ID / 需求 ID / ADR / 阶段：** WP-15-T08；ARC-05、NFR-COR-05＋AT-19（运动学入口与 WP-20 静态优化入口碰撞判定一致）；无直接关联 ADR；阶段 B / R1。契约：`module-design/kinematics.md` v0.3 §5.5/§6、`architecture/public-interfaces.md` §3～§4、`architecture/testing-contract.md`；一致性口径与 WP-20-T08 对侧联调确定。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD（kinematics.md v0.3、需求 v0.7）
- **前置任务及必需工件：** WP-15-T06（`sdurws_ird_kinematics_contract_test` 骨架目标与评估器装配可用）；WP-15-T07（显示开关与会话入口可用）；WP-20-T08（静态优化入口与对侧契约用例联调）；WP-07-T02（共享 `CollisionEvaluator` 为两入口共同依赖）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/kinematics/`）
  - 创建：`test/CrossEntryTest.cpp`（编入 `sdurws_ird_kinematics_contract_test` 目标）、`evidence/WP-15/`（AT-19 阶段 B 记录）
  - 修改：`CMakeLists.txt`（仅当骨架目标缺测试源登记时补一行）；不删除文件。
- **禁止修改的文件和公共接口：** 一切产品源码（本卡只写契约测试）；禁止复制碰撞规则或第二套策略实现（发现即停止）；WP-07 `CollisionEvaluator`、WP-05 `ResultEnvelope`、WP-08 调度接口、WP-20 静态入口内部实现；T01～T07 已交付类型；测试运行期禁止写回 `testdata/`。
- **修改前接口：** 无（契约测试新增；T06 已登记空的 `sdurws_ird_kinematics_contract_test` 骨架目标）。
- **修改后接口：** CTest 目标 `sdurws_ird_kinematics_contract_test`：AT-19 静态入口一致性用例——Given 同一快照与同一 `CollisionPolicy`，When 分别经 `ird.kinematics`（T05 碰撞证据）与 WP-20 静态优化入口求值，Then 返回完全相同的对象 ID 对（按 `(ownerScopeId, objectId)` 排序）、判定与原因；显示开关（可见性/透明度等 UI 态）不影响判定、不创建修订。
- **实施步骤：**
  1. 先写 `CrossEntryTest.cpp` 全部 RED 断言，构建确认失败。
  2. 构造跨入口共享夹具：同一 `AnalysisSnapshot`（canonical 身份＋nameMapId＋策略内容 ID）喂两入口。
  3. 断言对象 ID 对/判定/原因三元组逐项一致（序列化比较）。
  4. 断言显示开关翻转前后判定与修订状态不变。
  5. 与 WP-20-T08 对侧用例核对夹具版本与结论，按验证命令三形式转绿，写证据并提交。
- **RED 测试：** `CrossEntryObjectIdPairsIdentical`；`CrossEntryVerdictAndReasonsIdentical`；`DisplayToggleDoesNotChangeVerdict`；`DisplayToggleDoesNotCreateRevision`。
- **最小实现：** 仅 AT-19 一致性契约用例；不新增任何产品代码；对侧（WP-20-T08）镜像用例不在本卡范围。
- **正常/边界/失败测试：**
  - 正常：Given 同一快照/策略下的无碰与碰撞场景各一，When 两入口求值，Then 对象 ID 对、判定与原因完全一致。
  - 边界：Given 允许接触对/排除对组合与安全距离边界候选，When 两入口求值，Then 结论与 WP-07 共享评估器三方一致。
  - 失败：Given 任一入口出现第二套策略实现或本地碰撞参数副本（静态可查），When 契约测试运行，Then 记录不一致证据并停止升级，不得以测试侧归一化掩盖差异。
- **精确验证命令：**（仓库根目录、VS x64 环境；三形式任选其一必须通过）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_kinematics_contract_test$'`；预期退出码 0。
  - `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_kinematics_contract_test`；预期构建成功。
  - `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_kinematics_contract_test$"`；预期全部通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `test/CrossEntryTest.cpp`（与必要的 CMake 一行登记）与证据文件；无策略/评估逻辑复制；无产品源码改动；`check-boundaries.ps1` 零违规。
- **证据工件：** `evidence/WP-15/at19-cross-entry-record.md`（两入口对照矩阵、对象 ID 对/判定/原因三元组、显示开关记录、与 WP-20-T08 联调签署）＋测试日志（命令、commit、配置）；独立评审者签署 AT-19 阶段 B 记录。
- **提交格式：** `WP-15-T08: add cross-entry contract tests`
- **停止与升级条件：** 发现第二套碰撞策略实现、或与 WP-20-T08 对侧口径无法对齐时停止并升级架构负责人；实现者不得担任本卡独立验证者。
