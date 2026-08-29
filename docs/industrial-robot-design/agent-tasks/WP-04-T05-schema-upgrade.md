# WP-04-T05 Schema 升级与外部源重新关联

- **Task ID / 需求 ID / ADR / 阶段：**WP-04-T05；需求 ARC-01、CON-01、NFR-REL-04、NFR-DEP-04；ADR-002；阶段 A / R1。
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；文档：requirements v0.8、检查点 `IRD-D2-20260829`、architecture/persistence-schema.md §5、module-design/persistence.md v0.3。
- **前置任务及必需工件：**WP-04-T01（格式加载）、WP-04-T03（staging/原子提交复用）、WP-03 core；WP-01-T03（测试入口）；`schemas/examples/project*.example.json` 基线夹具。
- **允许创建/修改/删除的文件**（模块根同 WP-04-T01）：创建 `include/sdurws/ird/project/ProjectUpgradeRegistry.hpp`、`ProjectSourceRelinker.hpp`、`src/ProjectUpgradeRegistry.cpp`、`src/ProjectSourceRelinker.cpp`、`test/SchemaUpgradeTest.cpp`、`test/ProjectQueryContractTest.cpp`（编入 `sdurws_ird_project_contract_test`）、`out/test-evidence/wp-04/<run-id>/`；修改 `CMakeLists.txt`、`testdata/rwdesign/schema1-*`；删除：无。
- **禁止修改的文件和公共接口：**当前需求语义与 manifest 基线字段；旧 `.rwproj` 原文件（只读识别）；WP-05 快照接口；手工 CSV；T01～T04 冻结接口；文档与 schemas/。
- **修改前接口：**T01 对未知版本只读拒绝（`IRD-PERSIST-FUTURE-SCHEMA`）；无升级链与重新关联命令。
- **修改后接口：**`UpgradeStep{fromVersion,toVersion,升级函数}` 显式注册 `1→2…`（禁跳级/降级猜测）；升级写新 staging 并产生修订记录，原目录只读；`ProjectSourceRelinker` 重新关联命令（sourceUri→规范路径→当前哈希→比对记录哈希→显式确认→新命令/修订）。
- **实施步骤：**1) 先写未来版本/跳级/旧格式失败测试；2) 实现 Schema 1 注册/读取基线；3) 实现逐版本升级框架（每步校验输入输出、最终原子提交）；4) 实现重新关联流程与确认门禁；5) 契约断言入 `sdurws_ird_project_contract_test`。
- **RED 测试：**未知未来 schemaVersion → `IRD-PERSIST-FUTURE-SCHEMA` 只读拒绝且原目录字节不变；`.rwproj` → `IRD-PERSIST-LEGACY-FORMAT` 不自动转换；缺失升级器（如 1→3 直跳）→ 拒绝。
- **最小实现：**注册表＋单步升级框架＋拒绝路径；不实现任何具体 1→2 字段迁移（Schema 2 语义未定义时保持空注册并通过拒绝测试）。
- **正常/边界/失败测试：**
  - 失败：升级过程任一 failpoint，When restart，Then 旧 HEAD 可读、无半升级 revision、产生 System 诊断。
  - 正常：Given 合法 Schema 1 黄金包，When open，Then 加载成功且字段与 manifest 哈希稳定；Given 注册的相邻升级器，When upgrade，Then 每步校验并最终原子提交。
  - 边界：外部源缺失/哈希变化且未确认 → 不创建 revision；确认后仅生成新命令，历史对象不变；未知字段保留往返；源移动与源篡改各一例。
- **精确验证命令：**
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_project_test$'`
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_project_contract_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_project_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_project_test$"`（contract 目标同法替换目标名）
  - 预期：目标全部用例通过（退出码 0）；脚本未交付时以原生形式执行，不复制临时脚本
- **diff 和禁止项检查：**diff 仅命中允许清单；无兼容层/双写路径；未登记默认值零新增（rg 校验隐式默认）；`.rwproj` 夹具未被写入。
- **证据工件：**`out/test-evidence/wp-04/<run-id>/`：升级前后 JSON/哈希、版本链日志、失败恢复比对、重新关联命令审计与契约测试输出。
- **提交格式：**`WP-04-T05: 新增 Schema 升级与外部源重新关联`

  - 新增 逐版本升级注册表与 ProjectSourceRelinker 重新关联实现
  - 新增 未来版本/跳级失败测试与 contract 目标登记
  - 新增 升级前后哈希与重新关联审计证据记录
- **停止与升级条件：**升级需要隐式默认值、无法保留历史对象或新字段语义未在需求/契约定义时停止并提交 ADR 请求；不得以代码提交替代决策记录。
