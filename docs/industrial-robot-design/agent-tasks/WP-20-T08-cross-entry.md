# WP-20-T08 跨入口契约

- **Task ID / 需求 ID / ADR / 阶段：**WP-20-T08；ARC-05、NFR-COR-05、AT-19（运动学入口与静态优化入口一致）；ADR-003（OPT-B 权威范围）；阶段 B / R1。契约：`architecture/public-interfaces.md` §3～§4、`architecture/testing-contract.md`、`module-design/optimization.md` v0.3 §6、`module-design/kinematics.md` §5.5（对侧口径）。本卡是 AT-19 跨入口集成用例**唯一所有者**，单向依赖 WP-15-T08（WP-15-T08 不依赖本卡）
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源同 WP-20-T01
- **前置任务及必需工件：**WP-20-T03（静态硬约束执行器与 WP-08 调度接线工件）、WP-20-T07（面板骨架与 `_gui_test` 可用）；WP-15-T08（运动学侧探针、三元组/夹具冻结约定与运动学侧不变量记录，单向消费，按其约定自行构造对侧夹具，不要求其文件）；WP-07-T02（共享 `CollisionEvaluator` 为两入口共同依赖）
- **允许创建/修改/删除的文件**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/optimization/`）：
  - 创建：`test/CrossEntryTest.cpp`（编入 `sdurws_ird_optimization_definition_contract_test`）、`out/test-evidence/wp-20/<run-id>/`
  - 修改：`plugins/optimization/CMakeLists.txt`（登记 `sdurws_ird_optimization_definition_contract_test`，T01 预留的登记位）。禁止删除任何文件
- **禁止修改的文件和公共接口：**复制 `CollisionEvaluator` 或第二套策略实现；WP-07/WP-15 评估器实现与签名；T01～T07 冻结语义与已冻结公共接口（扩展接口不得改签名）；产品源码（本卡只写契约测试）；测试运行期禁止写回 `testdata/`
- **修改前接口：**无 `_contract_test` 目标（T01 登记位预留："`_contract_test` 由 WP-20-T08 登记"）；跨入口一致性无自动化证据
- **修改后接口：**CTest 目标 `sdurws_ird_optimization_definition_contract_test`：AT-19 用例——Given 同一 `AnalysisSnapshot`（canonical 身份＋nameMapId＋策略内容 ID）与同一 `CollisionPolicy`，When 分别经运动学入口（WP-15-T05 碰撞证据）与 WP-20 静态优化入口（T03 经 WP-08 调度）求值，Then 返回完全相同的对象 ID 对（按 `(ownerScopeId, objectId)` 排序）、判定与原因；只翻转显示开关（可见性/透明度等 UI 态）→ 输入切片与结果当前性不变、不创建修订；另交付供 WP-21 复用的稳定扩展接口（不改变已冻结签名，WP-21 复用走 `joint/` 新实现）
- **实施步骤：**1) RED：写 `CrossEntryTest` 四项断言并登记 `_contract_test` 目标，构建确认失败；2) 按 WP-15-T08 冻结的三元组/夹具约定构造对侧夹具（同一快照/策略喂两入口）；3) 断言对象 ID 对/判定/原因三元组逐项一致（序列化比较）；4) 断言显示开关翻转前后切片、当前性与修订状态不变；5) 命令转绿并写证据
- **RED 测试：**`CrossEntryTest`（先写先败）：`CrossEntryObjectIdPairsIdentical`、`CrossEntryVerdictAndReasonsIdentical`、`DisplayToggleKeepsSliceAndCurrentness`（显示开关不影响输入切片与结果当前性）、`DisplayToggleDoesNotCreateRevision`
- **最小实现：**仅 AT-19 一致性契约用例＋目标登记；不新增任何产品代码；对侧（WP-15-T08）镜像用例不在本卡
- **正常/边界/失败测试：**
  - 正常：Given 同一快照/策略下的无碰与碰撞场景各一，When 两入口求值，Then 对象 ID 对、判定与原因完全一致
  - 边界：Given 允许接触对/排除对组合与安全距离边界候选，Then 与 WP-07 共享评估器三方一致
  - 失败：Given 任一入口出现第二套策略实现或本地碰撞参数副本，When 契约测试运行，Then 记录不一致证据并停止升级，不得以测试侧归一化掩盖差异
- **精确验证命令**（仓库根、VS x64；第一形式必执行，脚本不可用时按回退顺序执行原生两形式）：
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_definition_contract_test$'`；预期退出码 0
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_definition_contract_test`；预期构建成功
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_definition_contract_test$"`；预期全部通过
- **diff 和禁止项检查：**diff 仅含允许清单；`rg -ni "collision|margin|distance" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/optimization/test/CrossEntryTest.cpp; if ($LASTEXITCODE -gt 1) { throw '扫描命令执行失败' }` 无本地碰撞规则副本（只比较两入口输出）；无产品源码改动；`check-boundaries.ps1` 零违规
- **证据工件：**`out/test-evidence/wp-20/<run-id>/`——AT-19 阶段 B 记录（两入口对照矩阵、对象 ID 对/判定/原因三元组、显示开关记录、引用 WP-15-T08 探针约定版本）＋测试日志（命令、commit、配置）
- **提交格式：**`WP-20-T08: 新增 AT-19 跨入口集成契约测试`

  - 新增 跨入口 AT-19 一致性用例（按 WP-15-T08 探针约定构造对侧夹具）
  - 新增 `sdurws_ird_optimization_definition_contract_test` 目标登记
  - 新增 AT-19 阶段 B 证据记录与探针约定版本引用
- **停止与升级条件：**与 WP-15-T08 口径无法对齐或需变更公共接口时，先走 ADR/contract-registry 契约变更并升级架构负责人；发现第二套策略实现立即停止
