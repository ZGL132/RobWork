# WP-11-T02 RFC 4180 CSV 安全读取

- **Task ID / 需求 ID / ADR / 阶段：**WP-11-T02；REQ-05、SEL-01～02、NFR-COR-03、NFR-SEC-01～03、AT-02；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/secure-io.md` v0.3
- **前置任务及必需工件：**WP-11-T01（`SafeProjectPath`/`ImportBudget`/`BudgetGuard`/`IoDiagnostics` 工件合入）；WP-03-T01（单位与有限性校验公共头）；WP-09-T01（`Diagnostic` 公共头）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/CsvReader.hpp`；`io/src/CsvReader.cpp`；`io/test/CsvReaderTest.cpp`；`io/test/IoContractFixture.cpp`（追加本端口三例）；`io/testdata/io/csv/reader/`；`io/out/test-evidence/wp-11/<run-id>/`；`io/CMakeLists.txt`（仅追加本任务文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**WP-11-T01 已合入的路径/预算接口；WP-03/09 公共头；`schemas/`、`architecture/`、`module-design/`；禁止执行公式/宏/命令、非法值自动转零、业务字段映射（列语义归业务 WP）
- **修改前接口：**无（读取器不存在；旧插件各持 CSV 解析）
- **修改后接口：**`CsvReader::open(SafePathHandle,ImportBudget)->expected<CsvRecordStream,IoError>`；每记录输出 `sourceLine`、`fieldName`、`rawText`、`normalizedValue`、`diagnostics[]`；`convert<T>(unit)` 显式单位与有限性校验
- **实施步骤：**1) 仅接受 UTF-8/UTF-8 BOM（其余编码报 `IRD-IO-ENCODING-INVALID`）；2) RFC 4180 引号/逗号/CRLF/LF/内嵌换行状态机；3) 重复表头、空字段、错误引号诊断（按源行号/字段定位）；4) 类型转换不转零（NaN/Infinity/非法单位报 Input 诊断）；5) 公式样式文本只作文本；6) 三例入契约夹具
- **RED 测试：**Given `=1+1`、`+cmd`、`-2+3`、`@SUM` 文本字段，When `convert<string>`，Then 仅返回文本原值且代码路径不触达任何求值/命令接口（`CsvReaderTest` 先行）
- **最小实现：**编码检查＋RFC 4180 状态机＋逐行诊断记录；单位换算仅结构留位（列字典归业务 WP）
- **正常/边界/失败测试：**
  - 正常：Given UTF-8/BOM、引号、逗号、CRLF/LF 与内嵌换行混合文件，When read，Then 行号/字段/原值逐条可追溯
  - 边界：Given 恰达预算上限的记录数/字段数文件，When read，Then 完整读取；超限一项立即停止且不保留部分记录
  - 失败：Given 非 UTF-8、坏 BOM、坏引号或重复表头，When read，Then `IRD-IO-ENCODING-INVALID`（Input/Error）并定位到源行号/字段，正确行不受影响
- **精确验证命令**（仓库根）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_io(_contract)?_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_io_test sdurws_ird_io_contract_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_io(_contract)?_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`rg -n "system\(|eval|QProcess|Shell" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/io/src/CsvReader.cpp; if ($LASTEXITCODE -eq 0) { throw '检测到禁止实现' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中；`rg -n "= *0\b" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/io/src/CsvReader.cpp; if ($LASTEXITCODE -eq 0) { throw '检测到禁止实现' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中（无转零分支）；`rg -n "isnan.*? *0" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/io/src/CsvReader.cpp; if ($LASTEXITCODE -eq 0) { throw '检测到禁止实现' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中
- **证据工件：**`io/out/test-evidence/wp-11/<run-id>/`——原始 CSV 与哈希、规范记录 JSON、行级诊断清单、恶意公式样本处置记录
- **提交格式：**`WP-11-T02: 新增 RFC 4180 安全 CSV 读取器`

  - 新增 编码检查、RFC 4180 状态机与逐行诊断读取实现
  - 新增 公式注入与编码失败测试及目标登记
  - 新增 恶意样本处置与行级诊断证据记录
- **停止与升级条件：**编码策略、记录/字段上限或字段单位口径未定义时暂停；发现需要业务列语义（如目录表列名）时上报——那是 WP-19/14 的列字典职责，本卡不扩展
