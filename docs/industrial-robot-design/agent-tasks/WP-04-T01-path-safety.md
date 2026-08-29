# WP-04-T01 格式加载与路径安全

- **Task ID / 需求 ID / ADR / 阶段：**WP-04-T01；需求 ARC-01、CON-01、CON-03、NFR-REL-01、NFR-REL-04；ADR-002；阶段 A / R1。
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；文档：requirements v0.8、检查点 `IRD-D2-20260829`、persistence-schema §1～§3、public-interfaces §1、module-design/persistence.md v0.3。
- **前置任务及必需工件：**WP-03-T01～T04（`sdurws_ird_core` 公共类型与 `DomainJson`）；WP-01-T02（CMake 已登记 `sdurws_ird_project`/`sdurws_ird_project_test`）；WP-01-T03（`run-tests.ps1` 入口）。
- **允许创建/修改/删除的文件**（模块根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/project/`）：创建 `include/sdurws/ird/project/ProjectPath.hpp`、`ProjectStore.hpp`、`ProjectRevision.hpp`、`ProjectManifest.hpp`、`ProjectDiagnostics.hpp`、`IProjectQuery.hpp`、`src/ProjectPath.cpp`、`src/ProjectStore.cpp`、`src/ProjectJson.cpp`、`test/PathSafetyTest.cpp`、`test/ContractFixtures.hpp`、`testdata/rwdesign/{schema1-valid,schema1-corrupt,legacy-rwproj}/`、`out/test-evidence/wp-04/<run-id>/`；修改 `CMakeLists.txt`；删除：无。
- **禁止修改的文件和公共接口：**requirements.md 与 architecture/、module-design/、schemas/ 文档；WP-03 core 公共头；WP-05 及以上模块；WP-01 脚本；命令服务与事务写入（T02/T03）；结果/检查点/报告追加协议（WP-05/08/12 使用）。
- **修改前接口：**无（project 模块不存在；仅 WP-03 core 可用）。
- **修改后接口：**`ProjectPath::resolveRelative`（只产 POSIX 相对路径）；`ProjectStore::open/load`（Schema 1：`schemaVersion=1`、`formatVersion=1`）；`IProjectQuery::load`（public-interfaces §1 签名）；诊断码 `IRD-PERSIST-PATH-ESCAPE`、`IRD-PERSIST-HASH-MISMATCH`、`IRD-PERSIST-FUTURE-SCHEMA`、`IRD-PERSIST-LEGACY-FORMAT`。
- **实施步骤：**1) 先写路径矩阵与格式失败测试；2) 实现 `ProjectPath`（拒绝空段、`.`、`..`、绝对/UNC、符号链接逃逸、超长与根外引用，不做字符串前缀判断）；3) 实现 HEAD/project.json/manifest 读取与必填校验；4) 用 `schema1-valid` 黄金包跑通往返；5) `schema1-valid` 与 `schemas/examples/project*.example.json` 同构校验。
- **RED 测试：**路径含 `..`/绝对盘符/UNC/反斜杠/大小写绕过/符号链接 → `IRD-PERSIST-PATH-ESCAPE` 且不打开根外文件；空 HEAD、重复键、未知键、指向缺失修订、坏 JSON、哈希不符 → 对应稳定诊断且不创建 staging。
- **最小实现：**只读加载链路；命令/事务/对象库/草稿/升级分别归 T02～T05。
- **正常/边界/失败测试：**
  - 失败：Given 对象内容被修改，When load，Then `IRD-PERSIST-HASH-MISMATCH`，历史 revision 字节仍可比对；未知未来版本与 `.rwproj` 只读拒绝。
  - 正常：Given 合法 Schema 1 黄金包，When load revision，Then 字段、列表排序、哈希和 ownerScopeId 往返一致；首版项目恰好一个非空 `robotId`（ADR-001）。
  - 边界：POSIX/Windows 分隔符、大小写、超长路径、缺字段、未知字段（保留再序列化）、非法浮点、重复 ID、缺引用、旧 `.rwproj`。
- **精确验证命令：**
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_project_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_project_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_project_test$"`
  - 预期：目标全部用例通过（退出码 0）；脚本未交付时以原生形式执行，不复制临时脚本
- **diff 和禁止项检查：**diff 仅命中允许清单；读取路径不写任何文件；无 Qt Widgets（仅 Qt Core `QFile/QDir/QJson*`）；`schema1-valid` 先过 `.\schemas\validate-schemas.ps1`。
- **证据工件：**`out/test-evidence/wp-04/<run-id>/`：测试日志、夹具哈希、路径矩阵诊断 JSON、往返比对输出。
- **提交格式：**`WP-04-T01: 新增 Schema 加载器与路径安全`

  - 新增 ProjectPath、ProjectStore 与 Schema 1 只读加载实现
  - 新增 路径矩阵与格式失败测试及目标登记
  - 新增 黄金包夹具与往返比对证据记录
- **停止与升级条件：**契约未定义的默认值、路径平台语义无法统一、`run-tests.ps1` 或 WP-03 类型缺失时停止并报告；不自行改变持久化语义。
