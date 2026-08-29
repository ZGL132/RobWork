# WP-12-T04 JSON/CSV 证据数据包

- **Task ID / 需求 ID / ADR / 阶段：**WP-12-T04；需求 NFR-COR-04、EVI-01、REQ-06、OPT-07～09、NFR-SEC-03、NFR-SEC-07；ADR-004；阶段 A / R1。
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；文档：requirements v0.8、检查点 `IRD-D2-20260829`、module-design/reporting.md v0.3 §3（数据包与 CSV 六类）；WP-11 `CsvWriter` 端口契约（集成期代码交付，先以契约替身开发）。
- **前置任务及必需工件：**WP-12-T01（`ReviewReport` 与追加协议）；WP-11 端口契约（`CsvWriter` 公共签名，非代码前置）；WP-09-T03（诊断码目录，供诊断 CSV 列）；WP-01-T03（测试入口）。
- **允许创建/修改/删除的文件**（模块根同 WP-12-T01）：创建 `include/sdurws/ird/reporting/EvidenceDataExporter.hpp`、`src/EvidenceDataExporter.cpp`、`test/EvidencePackageTest.cpp`、`test/CsvWriterStub.hpp`（契约替身，集成期替换为 WP-11 实现）、`testdata/packages/`、`out/test-evidence/wp-12/<run-id>/`；修改 `CMakeLists.txt`；删除：无。
- **禁止修改的文件和公共接口：**T01～T03 冻结接口与工件格式；requirements.md 与 architecture/、module-design/ 文档；WP-11 `CsvWriter` 公共端口（只消费不改）；自研 CSV 转义（必须经 `CsvWriter` 端口）；其他 WP 公共头。
- **修改前接口：**T01 的 `reports/<report-id>/` 只含 `report.json`＋工件索引，无分类 CSV。
- **修改后接口：**`EvidenceDataExporter`：JSON 完整保存 `ReviewReport` 与引用身份（规范 JSON：UTF-8/LF/无 BOM、键序固定、有限 number）；CSV 六类独立输出——设计参数、需求结果、候选指标、硬约束、器件淘汰、诊断；公式注入转义统一委托 `CsvWriter` 端口，原始值保存在 JSON。
- **实施步骤：**1) 先写非有限数与转义失败测试；2) 实现 JSON 数据包序列化；3) 按六类实现 CSV 行装配（列名与顺序固定）；4) 接 `CsvWriter` 替身做公式注入转义；5) 断言 JSON 原值与 CSV 展示值可对照。
- **RED 测试：**JSON 遇非有限浮点 → 拒绝产出（不得产生 `NaN/Infinity` 字面量）；CSV 单元格以 `=`/`+`/`-`/`@` 开头而未经转义 → 测试失败；六类之外出现混装 CSV → 失败。
- **最小实现：**JSON 包＋六类 CSV＋替身转义；HTML 一致性归 T03/T06。
- **正常/边界/失败测试：**
  - 失败：Given 字段非有限或引用身份缺失，When export，Then Input 诊断且不写部分工件。
  - 正常：Given 合法报告，When export，Then JSON 逐字段保存（含引用身份）且 CSV 六类各自成文件、列序稳定。
  - 边界：公式样输入（`=cmd`、`@sum`）、逗号/引号/换行单元格、中文列头、空类别（输出仅表头）；CSV 展示值与 JSON 原值一一对照。
- **精确验证命令**（无 GUI 测试）：
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_reporting_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_reporting_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_reporting_test$"`
  - 预期：目标全部用例通过（退出码 0）；脚本未交付时以原生形式执行，不复制临时脚本
- **diff 和禁止项检查：**diff 仅命中允许清单；无本地转义实现（全部经端口）；替身仅存在于 test/，不进产品目标；无未登记依赖。
- **证据工件：**`out/test-evidence/wp-12/<run-id>/`：JSON/CSV 样例工件与哈希、转义用例清单、非有限拒绝日志、与 WP-11 集成衔接说明。
- **提交格式：**`WP-12-T04: 新增 JSON/CSV 证据数据包`

  - 新增 EvidenceDataExporter 规范 JSON 包与六类 CSV 输出实现
  - 新增 非有限拒绝与转义失败测试及目标登记
  - 新增 样例工件哈希与转义用例证据记录
- **停止与升级条件：**`CsvWriter` 端口签名未冻结或转义规则与 WP-11 不一致时停止并报告；集成期替换替身需与 WP-11 交付同步，不提前私有实现。
