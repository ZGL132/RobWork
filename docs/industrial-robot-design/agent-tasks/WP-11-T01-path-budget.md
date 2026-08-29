# WP-11-T01 路径规范化与资源预算

- **Task ID / 需求 ID / ADR / 阶段：**WP-11-T01；REQ-05、SEL-01～02、NFR-REL-04、NFR-SEC-01～03；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（`industrialrobot/io/` 尚不存在）；语义源 `module-design/secure-io.md` v0.3
- **前置任务及必需工件：**WP-01-T02（CMake 骨架）；WP-01-T03（`run-tests.ps1` 测试入口）；WP-03-T01（单位/有限性公共头）；WP-09-T01（`Diagnostic` 公共头）；WP-09-T03（`IRD-IO-*` 错误码映射）；WP-04-T04（内容对象端口——契约引用，集成期交付，本卡不写 `objects/`）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/ImportBudget.hpp`、`SafeProjectPath.hpp`、`IoDiagnostics.hpp`；`io/src/SafeProjectPath.cpp`、`BudgetGuard.cpp`；`io/test/PathBudgetTest.cpp`、`io/test/IoContractFixture.cpp`（本端口失败/正常/边界三例）；`io/testdata/io/paths/`；`io/evidence/WP-11/T01/`；`io/CMakeLists.txt`（登记 `sdurws_ird_io`、`sdurws_ird_io_test`、`sdurws_ird_io_contract_test`）。禁止删除任何文件
- **禁止修改的文件和公共接口：**WP-03/04/09 公共接口；`schemas/catalog/*.schema.json`（D3 拥有）；`architecture/`、`module-design/`；禁止业务语义解析、直接写项目 revision 或 `objects/`、提供绕过预算的读取旁路
- **修改前接口：**无（安全路径与预算类型不存在；旧插件各持私有路径拼接）
- **修改后接口：**`SafeProjectPath::normalize(raw)->expected<CanonicalPath,IoError>`、`resolve(root,relative)->expected<SafePathHandle,IoError>`（统一 POSIX `/` 相对形式）；`ImportBudget` 显式字段（单文件/总字节、JSON/XML 最大深度、最大记录/字段/字符串数、网格顶点/三角形数、压缩展开比、目录文件数）；`BudgetGuard::preflight(budget,path)` 读取前预检
- **实施步骤：**1) 冻结 `ImportBudget` 字段与默认值（值随证据归档）；2) 实现规范化（拒空段、`.`、`..`、绝对/UNC、反斜杠、大小写绕过）与符号链接逃逸检查；3) `BudgetGuard` 预检与超限即停（不保留部分记录）；4) 三例入契约夹具；5) 登记目标
- **RED 测试：**Given `..`、绝对路径、UNC、反斜杠、大小写变体、符号链接逃逸或超长路径，When `resolve`，Then 拒绝并返回 `IRD-IO-PATH-ESCAPED`（Input/Error，恢复动作＝提示合法相对路径）（`PathBudgetTest` 先行）
- **最小实现：**规范化＋逃逸检查＋预检三条路径，仅够 RED 转绿；不实现任何格式解析
- **正常/边界/失败测试：**
  - 正常：Given 合法项目相对路径与预算，When read，Then 返回规范路径、预算消耗计数与安全句柄
  - 边界：Given 单文件/总量/深度/记录/几何/展开比任一等于阈值上限的输入，When preflight，Then 通过（等于不超限）；超限一项即整体拒绝
  - 失败：Given 任一预算超限，When preflight，Then `IRD-IO-BUDGET-EXCEEDED`、立即停止、零部分记录产出、旧项目状态不变
- **精确验证命令**（仓库根）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_io(_contract)?_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_io_test sdurws_ird_io_contract_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_io(_contract)?_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`grep -rn "ifstream\|QFile" io/src/SafeProjectPath.cpp io/src/BudgetGuard.cpp` 零命中（预检不偷读内容）；`grep -rn "objects/" io/src/` 零命中（不直写对象库）
- **证据工件：**`io/evidence/WP-11/T01/`——路径矩阵（拒绝/接受逐项）、预算配置与消耗曲线、诊断 JSON、命令日志
- **提交格式：**`WP-11-T01: implement safe paths and import budgets`
- **停止与升级条件：**平台边界（符号链接/大小写）无法在本机证明、预算默认值未冻结时暂停；阈值需变更时上报 WP-11 负责人更新 secure-io.md 后再实现，不得现场调参
