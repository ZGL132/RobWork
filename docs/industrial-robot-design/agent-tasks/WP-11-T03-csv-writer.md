# WP-11-T03 CSV 公式注入安全写出

- **Task ID / 需求 ID / ADR / 阶段：**WP-11-T03；REQ-05、SEL-01～02、NFR-SEC-01～03；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/secure-io.md` v0.3
- **前置任务及必需工件：**WP-11-T02（`CsvReader` 记录形态 `sourceLine/fieldName/rawText/normalizedValue` 工件合入）；WP-09-T01（`Diagnostic` 公共头）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/io/include/sdurws/ird/io/CsvWriter.hpp`；`io/src/CsvWriter.cpp`；`io/test/CsvWriterTest.cpp`；`io/test/IoContractFixture.cpp`（追加本端口三例）；`io/testdata/io/csv/writer/`；`io/evidence/WP-11/T03/`；`io/CMakeLists.txt`（仅追加本任务文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**`CsvReader` 已合入接口；WP-03/09 公共头；`schemas/`、`architecture/`、`module-design/`；禁止业务插件自行导出 CSV（转义规则单一实现）、破坏数值列类型、在 writer 内执行公式
- **修改前接口：**无（安全写出不存在；旧插件直接写文本）
- **修改后接口：**`CsvWriter::writeRow(fields)`／`writeTable(headers,rows)`：文本首字符 `=`、`+`、`-`、`@` 统一加安全前缀（前缀常量冻结入证据）；数值类型按数值列写出；`escapeForJsonEvidence` 保留未转义原值供 JSON 证据
- **实施步骤：**1) 冻结转义前缀与判定顺序；2) 实现文本/数值分型写出（真正负数按数值列，不误判公式）；3) RFC 4180 引号/换行写出与读回往返；4) 确定性导出（同输入逐字节相同）；5) 三例入契约夹具
- **RED 测试：**Given 文本 `=1+1`、`+cmd`、`-2+3`、`@SUM(A1)`，When export，Then 输出统一安全前缀且经 `CsvReader` 读回不触发任何执行路径（`CsvWriterTest` 先行）
- **最小实现：**分型写出＋转义分支＋确定性序列化；不做样式/多 sheet 等工作簿特性
- **正常/边界/失败测试：**
  - 正常：Given 普通文本、引号、内嵌换行与 Unicode，When round-trip（write→read），Then 字段内容完整恢复且值语义不变
  - 边界：Given 真正数值 `-3.14` 与文本 `-note` 混合列，When export，Then 数值按数值列（不加前缀）、文本加前缀；空串与空引用字段区分稳定
  - 失败：Given 目标路径不可写（权限/磁盘），When write，Then `IRD-IO-PARSE-FAILED`（System/Error）、已写部分清理或整体不产出、旧文件保持
- **精确验证命令**（仓库根）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_io(_contract)?_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_io_test sdurws_ird_io_contract_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_io(_contract)?_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`grep -rn "system(\|eval\|QProcess" io/src/CsvWriter.cpp` 零命中；两次导出 `git hash-object` 比对逐字节一致（确定性）；`grep -rn "toDouble\|toFixed" io/src/CsvWriter.cpp` 命中处仅数值列路径
- **证据工件：**`io/evidence/WP-11/T03/`——输入/输出 CSV 对照、类型矩阵（文本/数值/边界字符）、转义规则说明与评审签署
- **提交格式：**`WP-11-T03: implement formula-safe CSV writer`
- **停止与升级条件：**转义规则会改变数值语义、或发现业务插件存在旁路 writer 时暂停并上报（旁路属 §13.3 消除项，登记迁移表）；前缀常量需变更时走 secure-io.md 版本升级，不得双规则并存
