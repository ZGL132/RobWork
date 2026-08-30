# WP-11-T05 URDF、网格与 JSON 安全边界

- **Task ID / 需求 ID / ADR / 阶段：**WP-11-T05；REQ-05、SEL-01～02、NFR-COR-03、NFR-SEC-01～03；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/secure-io.md` v0.3、`architecture/public-interfaces.md` §6（`ResourceImportService` 登记为资源读取唯一入口）
- **前置任务及必需工件：**WP-11-T01（`SafeProjectPath`/`ImportBudget` 工件）；WP-04-T04（资源副本经内容对象端口——契约引用，集成期交付，本卡不写 `objects/`）；WP-01-T03（测试入口）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/JsonDocumentReader.hpp`、`ResourceImportService.hpp`、`SafeResource.hpp`；`io/src/JsonDocumentReader.cpp`、`ResourceImportService.cpp`；`io/test/ResourceBoundaryTest.cpp`；`io/test/IoContractFixture.cpp`（追加本端口三例）；`io/testdata/io/resources/`（DOCTYPE/实体/URL/深嵌套/非有限/超大网格恶意样本）；`io/out/test-evidence/wp-11/<run-id>/`；`io/CMakeLists.txt`（仅追加本任务文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**WP-11-T01～T04 已合入接口；WP-04 内容对象端口；`architecture/`、`module-design/`；禁止 URDF 语义转换（归 WP-13-T03）、执行外部实体/网络/命令/宏、加载资源根外文件、修改项目 revision
- **修改前接口：**无（资源读取无统一入口；旧插件可直读任意路径）
- **修改后接口：**`JsonDocumentReader::parse(SafePathHandle,ImportBudget)->expected<JsonDocument,IoError>`（禁未知未来 schema、非有限数、过深嵌套、重复关键字段）；`ResourceImportService::readResource(resourceRef)->expected<SafeResource,IoError>`（预算内安全字节＋路径句柄，业务适配器再做语义解析）；`SafeResource`（字节、规范路径、内容哈希）
- **实施步骤：**1) XML/URDF 层禁用 DOCTYPE/外部实体/网络 URL/命令/宏（解析库安全配置集中一处）；2) JSON 深度/有限性/schema 版本检查；3) 网格仅检查格式、顶点/三角形上限与有限坐标；4) 逐行错误报告（Error/Warning/Info、字段路径、源值、采用值、原因、动作）；5) 三例入契约夹具
- **RED 测试：**Given 含 DOCTYPE/外部实体、网络 URL、命令宏、符号链接或资源根外引用的文件，When `readResource`，Then 拒绝并返回 `IRD-IO-PATH-ESCAPED` 或对应稳定诊断（`ResourceBoundaryTest` 先行）
- **最小实现：**安全配置＋边界检查＋安全字节返回；不做任何 URDF 关节/轴语义解释
- **正常/边界/失败测试：**
  - 正常：Given 合法有限资源，When read，Then 返回安全字节/路径句柄与内容哈希，供业务适配器解析
  - 边界：Given 深度恰达上限的 JSON 与顶点数恰达上限的网格，When read，Then 通过；超限一项立即停止、零部分对象
  - 失败：Given 损坏 JSON/URDF 或非有限坐标，When parse，Then Input/System 诊断（`IRD-IO-PARSE-FAILED` 归 System）且旧项目不变；资源缺失按 Engineering/DataInsufficient 表达（`IRD-PERSIST-SOURCE-MISSING` 透传），不伪装成 System
- **精确验证命令**（仓库根）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_io(_contract)?_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_io_test sdurws_ird_io_contract_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_io(_contract)?_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`rg -n "setNetworkAccessible|QNetworkAccessManager|CommandLine" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/io/src/; if ($LASTEXITCODE -eq 0) { throw '检测到禁止实现' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中；`rg -n "DOCTYPE|ExternalEntity" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/io/src/` 命中处仅限禁用配置；`rg -n "objects/" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/io/src/; if ($LASTEXITCODE -eq 0) { throw '检测到禁止实现' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中
- **证据工件：**`io/out/test-evidence/wp-11/<run-id>/`——恶意样本清单与哈希、预算消耗、路径规范化结果、逐行诊断 JSON、命令日志
- **提交格式：**`WP-11-T05: 新增 URDF/网格/JSON 安全边界`

  - 新增 Xml/JSON 安全解析配置与 ResourceImportService 统一入口实现
  - 新增 恶意样本边界测试及目标登记
  - 新增 样本清单与逐行诊断证据记录
- **停止与升级条件：**解析库默认启用外部实体且无法关闭、或安全层被要求承担业务语义（如 URDF 轴解释）时暂停并上报；安全配置需分散到多文件时升级架构评审（单一入口原则）
