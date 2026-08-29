# WP-14-T06 项目命令集成

- **Task ID / 需求 ID / ADR / 阶段：**WP-14-T06；ARC-01、CON-05、REQ-06、AT-05（相关面）；阶段 B / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/requirements-definition.md` v0.3 §4、`architecture/public-interfaces.md` §1
- **前置任务及必需工件：**WP-14-T05（就绪判定工件）；WP-04-T02（`IProjectCommandService`/`DomainCommand`/`CommandResult` 公共头与命令服务实现——代码前置）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/requirements/include/sdurws/ird/requirements/RequirementsCommands.hpp`；`requirements/src/RequirementsCommands.cpp`；`requirements/test/CommandIntegrationTest.cpp`；`requirements/out/test-evidence/wp-14/<run-id>/`；`requirements/CMakeLists.txt`（仅追加本任务文件；WP-04 端口契约三例入 `sdurws_ird_requirements_contract_test`）。禁止删除任何文件
- **禁止修改的文件和公共接口：**`DomainCommand` 基类与 `IProjectCommandService` 签名（public-interfaces §1）；WP-04 持久化实现（undo/redo 由 WP-04 承担）；T01～T05 冻结接口；`architecture/`、`module-design/`；禁止插件内平行命令基类、直接写项目文件
- **修改前接口：**无（需求编辑命令不存在）
- **修改后接口：**`ApplyRequirementsEdit`（`DomainCommand` 子类）：`commandId()` 幂等、`commandKind()="requirements.edit"`、`targetObjects()` 返回受影响 `objectId`、`validate(target)`（含 T05 就绪校验）、`buildMutations(target)` 纯函数（按字段产出 `MutationSet`）
- **实施步骤：**1) 命令 DTO 承载 T01～T04 编辑结果；2) `validate` 接 T05 就绪（正式应用前聚合必须"可计算"）；3) `buildMutations` 按字段生成失效（任务点/区域/负载数据各自切片）；4) 应用＝`IProjectCommandService.apply`（恰好一个新修订）；5) 未应用草稿零副作用断言
- **RED 测试：**Given 未应用的需求草稿，When 修改会话状态（筛选/视图/再编辑），Then 修订数不变、任何输入切片零变化（CON-05）（`CommandIntegrationTest` 先行）
- **最小实现：**一个编辑命令的 validate/buildMutations 与 apply 接线；undo/redo 直接依赖 WP-04 不自实现
- **正常/边界/失败测试：**
  - 正常：Given 合法编辑，When apply，Then 恰好一个新修订＋按字段失效（下游依赖切片标记需重算）；同 `commandId` 重复 apply 为 no-op 返回既有修订
  - 边界：Given undo 后 redo，When 经 WP-04，Then 历史 payload 不改写、状态一致（AT-05 相关面）
  - 失败：Given base revision 过期（`IRD-PROJ-STALE-REVISION`）或校验失败（`IRD-PROJ-VALIDATION-FAILED`），When apply，Then 零修订、旧状态保持、诊断透传
- **精确验证命令**（仓库根；含 WP-04 端口契约测试）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_requirements(_contract)?_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_requirements_test sdurws_ird_requirements_contract_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_requirements(_contract)?_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`rg -n "class.*DomainCommand" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/requirements/include` 命中处仅继承；`QFile\|QFile\|writeProject" requirements/src/RequirementsCommands.cpp` 零命中（不直写项目目录）；`buildMutations` 无 I/O 调用（纯函数）
- **证据工件：**`requirements/out/test-evidence/wp-14/<run-id>/`——修订/失效矩阵（编辑类型×下游切片）、幂等记录、undo/redo 状态表、命令日志
- **提交格式：** `WP-14-T06: 集成需求命令服务`

  - 新增需求命令装配与修订集成
  - 新增命令幂等测试
  - 新增运行证据记录
- **停止与升级条件：**命令所有权（与 WP-04 端口语义）不明确、或失效粒度与依赖切片规则冲突时暂停并升级架构评审；不得为绕过冲突自建持久化路径
