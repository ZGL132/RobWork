# WP-02-T04 迁移判定协议

- **Task ID / 需求 ID / ADR / 阶段：** WP-02-T04；NFR-COR-01（WP-02 唯一主包需求：旧实现与新实现共用黄金数据独立判定）；无直接关联 ADR；阶段 A 前提 / R1。契约：`module-design/testkit.md` §8（迁移与扩展）、`architecture/execution-model.md`、`architecture/testing-contract.md` §2。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-02-T01、WP-02-T02、WP-02-T03；工件：`testdata/manifest.json`（`ird-golden-0.7.1` 全量清单）、两层容差断言库（`sdurws_ird_assertion_library_test` 通过日志）与 `sdurws_ird_golden_data_test` 通过日志。
- **允许创建/修改/删除的文件：**
  - 创建：`testkit/include/sdurws/ird/testkit/MigrationVerdict.hpp`；`testkit/src/` verdict 规则与差异报告实现
  - 创建：`testkit/tests/migration/adapters/`（旧算法只读适配器，一算法一 adapter）；`testkit/tests/migration_protocol_test.cpp`
  - 修改：`testkit/CMakeLists.txt`（注册 `sdurws_ird_migration_protocol_test` 与 adapter 编译）
  - 写证据：`out/test-evidence/wp-02/<run-id>/migration/`
- **禁止修改的文件和公共接口：** 旧插件源码与 CMake（adapter 只读调用，不得修改旧算法）；T01～T03 冻结的 manifest、断言 API 与样本；`industrialrobot/` 既有模块；`requirements.md`、CSV；迁移失败不得删除旧实现、只改 verdict。
- **修改前接口：** 无（新增 verdict 类型与 adapter 框架；旧算法入口现状记录为证据，不作接口变更）。
- **修改后接口：** `MigrationVerdict.hpp` 定义三值结论 `Migratable`（满足冻结容差且无旧状态依赖）、`Rewrite`（算法不满足容差或依赖 Widget/私有状态）、`EvidenceOnly`（仅夹具/数据可复用）；adapter 契约：使用同一 manifest、同一容差 profile、同一 seed 与输入切片调用旧算法，记录输入哈希、版本、输出、差异统计、Widget/私有状态依赖审计与结论；证据输出到 `out/test-evidence/wp-02/<run-id>/migration/`。
- **实施步骤：**
  1. 盘点旧算法清单：从 `development-task-breakdown.md` §3 所列四个旧业务插件（robotmodelbuilder、engineeringrequirements、kinematicanalysis、structureoptimizer）中列出可独立调用的算法入口，形成迁移对象表（盘点结果记入证据，不修改旧码）。
  2. 先写 `migration_protocol_test.cpp` 全部 RED 断言并注册目标，构建确认失败。
  3. 实现 `MigrationVerdict.hpp` 与 verdict 规则（容差、可重复性、状态依赖三判据）。
  4. 逐算法实现只读 adapter，复用 T01 loader 与 T02 断言，登记输入哈希与差异报告。
  5. 运行全部 adapter，产出 verdict 表与证据。
  6. 按验证命令（脚本＋原生双形式）转绿并写证据。
- **RED 测试：** 先写的失败断言：`WidgetDependentAdapterIsNotMigratable`（旧算法依赖 Widget/私有状态 → verdict ∈ {Rewrite, EvidenceOnly}，不得 Migratable）、`OutOfToleranceAdapterIsNotMigratable`（输出超冻结容差 → Rewrite）、`InsufficientDataYieldsDataInsufficientVerdict`（缺摩擦/物性/曲线段 → DataInsufficient，不生成精确通过）。
- **最小实现：** 仅实现 verdict 类型、三判据规则与迁移对象表内每个算法的只读 adapter；不改旧算法、不新增黄金数据类别。
- **正常/边界/失败测试：**
  - 正常：Given adapter 使用同一 manifest、profile、seed 与输入切片且输出满足容差、无旧状态依赖，When 运行，Then verdict=Migratable 并保存输入哈希、版本、输出与差异证据（`DeterministicAdapterWithinToleranceIsMigratable`）。
  - 边界：Given 同一 adapter 固定 seed/线程重跑，When 比较，Then 输出与差异统计可重复（`VerdictEvidenceIsReproducible`）。
  - 失败：Given 旧算法依赖 Widget 状态、超容差或不可重复，When 运行 adapter，Then verdict 为 Rewrite/EvidenceOnly 且失败迁移不删除旧 adapter、只改 verdict。
- **精确验证命令：**（仓库根目录、VS x64 环境；脚本＋原生双形式）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_migration_protocol_test$'`；预期退出码 0。
  - 原生回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_migration_protocol_test` 与 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_migration_protocol_test$"`；预期构建成功、测试通过。
  - WP-02 收尾聚合（`module-design/testkit.md` §7 原文双形式）：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_(manifest_integrity|assertion_library|golden_data|migration_protocol)_test$'`；以及 `cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_manifest_integrity_test sdurws_ird_assertion_library_test sdurws_ird_golden_data_test sdurws_ird_migration_protocol_test` 后运行 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_(manifest_integrity|assertion_library|golden_data|migration_protocol)_test$"`；预期四个目标全部构建并通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `testkit/` 内 verdict 头/实现、`tests/migration/` 与 CMake 接入；旧插件目录与 `testdata/` 零变化（adapter 只读）；无 Widget/会话状态进入期望值生成。
- **证据工件：** `out/test-evidence/wp-02/<run-id>/migration/`：迁移对象盘点表、每算法 verdict 表、旧/新输出对照、输入哈希、差异统计、状态依赖审计、命令与提交 SHA、独立迁移评审者签署（非 adapter 实现者）。
- **提交格式：** `WP-02-T04: 新增迁移判定协议`

  - 新增 MigrationVerdict 判定类型与三判据规则实现
  - 新增 旧算法只读 adapter 与迁移协议测试目标登记
  - 新增 verdict 表、差异报告与迁移证据记录
- **停止与升级条件：** 无法区分算法失败、数据不足与仅夹具可复用，或判定必须修改旧算法才能运行 adapter 时，停止并升级给迁移评审者与工作包所有者；adapter 实现者不得批准自己的 verdict。
