# 安全导入导出模块详细方案

- 方案版本：v0.3；需求基线：v0.8；架构检查点：`IRD-D2-20260829`
- 负责 WP：WP-11；阶段/发布：阶段 A / R1；任务卡：`agent-tasks/WP-11-T01～T05`
- 架构契约：`architecture/persistence-schema.md`（§1、§2.4）、`architecture/public-interfaces.md`（§6）、`architecture/testing-contract.md`；Schema：`schemas/catalog/column-dictionary.schema.json`、`schemas/catalog/catalog-manifest.schema.json`

## 1. 模块职责

模块拥有路径边界、字节/解析预算、UTF-8 CSV 安全读取与写出、JSON 文档边界、URDF/网格安全读取、目录包验证和不可变资源副本交付。业务适配器（WP-13/14/19）只消费安全记录，不重复解析或加载外部资源；项目 revision 由 WP-04 显式命令提交，目录筛选/淘汰规则归 WP-19。模块不实现机器人模型语义、需求校验和第三方工作簿引擎。

## 2. 目录与构建

```text
RobWork/RobWorkStudio/src/rwslibs/industrialrobot/io/
  include/sdurws/ird/io/
    ImportBudget.hpp SafeProjectPath.hpp
    CsvReader.hpp CsvWriter.hpp JsonDocumentReader.hpp
    ResourceImportService.hpp CatalogPackageReader.hpp
    SafeResource.hpp IoDiagnostics.hpp
  src/SafeProjectPath.cpp BudgetGuard.cpp CsvReader.cpp CsvWriter.cpp
      JsonDocumentReader.cpp ResourceImportService.cpp CatalogPackageReader.cpp
  test/PathBudgetTest.cpp CsvReaderTest.cpp CsvWriterTest.cpp
      CatalogImportTest.cpp ResourceBoundaryTest.cpp
  testdata/                      # 证据统一写 out/test-evidence/wp-xx/<run-id>/（AGENTS §3）
```

CMake target：`sdurws_ird_io`、`sdurws_ird_io_test`、`sdurws_ird_io_contract_test`。允许依赖：WP-03 core、WP-09 诊断（代码前置 WP-03、09，总纲 §5.2）、Qt Core/标准库；禁止 Qt Widgets、未登记解析库、业务插件第二套路径/CSV/JSON 读取和直接写 revision。资源副本写入经 WP-04 内容对象端口（契约引用，集成期交付，本模块不直接写 `objects/`）。

## 3. 数据与接口

- `ImportBudget`：单文件/总字节、XML/JSON 最大深度、最大记录/字段/字符串数、网格顶点/三角形数、压缩展开比、目录文件数；读取前预检，超限立即停止且不保留部分领域对象。
- CSV：Reader 只接受 UTF-8/UTF-8 BOM，按 RFC 4180 处理引号、逗号、CRLF/LF 与内嵌换行；每记录输出 `sourceLine`、`fieldName`、`rawText`、`normalizedValue`、`diagnostics`；类型转换必须显式单位与有限性校验，非法值不转零（REQ-05、NFR-COR-03）。Writer 对文本值首字符 `=`、`+`、`-`、`@` 加统一安全前缀（NFR-SEC-03）；真正数值按数值列输出，原始值保存在 JSON 证据而非 CSV 公式。
- 目录包：manifest 按 `catalog-manifest.schema.json` 校验（固定文件集合、额外文件拒绝、`catalogId`/版本、来源、SHA-256、行数预算、`declaredUnits`）；各表按 `column-dictionary.schema.json` 校验（列名大小写敏感、类型、单位一致、必填、数值范围、`primaryKeys` 唯一、`foreignKeys` 无悬空）；另校验 motor/reducer 唯一性、curve 点序与覆盖范围。全部通过才生成不可变 `CatalogVersion`（由业务命令提交）。
- URDF/网格/JSON：URDF 规则明细以 requirements §8.1.2 为准，本模块实现解析预算（禁用 DOCTYPE、外部实体、网络 URL、命令、宏和资源根外引用）与逐行错误报告（Error/Warning/Info、字段路径、源值、采用值、原因、动作）；网格仅检查格式、顶点/三角形上限和有限坐标，语义转换归 WP-13；JSON 拒绝未知未来 schema、非有限数、过深嵌套和重复关键字段。
- 资源副本：产出 `objects/<sha256>/payload.bin + meta.json` 形态的不可变副本交 WP-04 对象库（persistence-schema §2.4），副本进入 Verified/正式报告前受引用保护。

## 4. 调用与状态

```text
untrusted path -> UTF-8 decode -> normalize POSIX -> reject absolute/UNC/../symlink escape
  -> canonical boundary check -> budget preflight -> bounded read
  -> normalized record -> business adapter -> explicit command
```

| 码 | 触发条件 | 类别 | severity | 恢复动作 |
| --- | --- | --- | --- | --- |
| IRD-IO-PATH-ESCAPED | 绝对/UNC/`..`、符号链接逃逸、资源根外引用、大小写绕过 | Input | Error | 拒绝读取并提示合法相对路径 |
| IRD-IO-BUDGET-EXCEEDED | 任一预算超限 | Input | Error | 不产出部分记录；调整预算后重试 |
| IRD-IO-ENCODING-INVALID | 非 UTF-8/坏 BOM、坏引号/换行、重复表头 | Input | Error | 按源行号/字段定位修复 |
| IRD-IO-CATALOG-INVALID | 清单/哈希/列/单位/行数/唯一性/外键/曲线错误 | Input | Error | 修复目录包后重新导入 |
| IRD-PERSIST-SOURCE-MISSING / -CHANGED | 外部资源缺失或源哈希变化（WP-04 码透传，首现 module-design/persistence.md §4） | Engineering | Error | 走重新关联入口（NFR-REL-04） |
| IRD-IO-PARSE-FAILED | 磁盘/权限/解析库故障 | System | Error | 项目旧状态保持，可重试 |

任何失败不得创建部分项目 revision 或正式 `CatalogVersion`；跨表悬空/资源缺失按 Engineering/DataInsufficient 表达，不伪装成 System。

## 5. 关键实现约定

- 路径统一 POSIX `/` 相对形式，拒绝空段、`.`、绝对/UNC、符号链接逃逸与大小写绕过（NFR-SEC-01）。
- 确定性导出：同输入产生逐字节相同的转义 CSV 与 JSON 证据；转义规则单一实现，业务插件不得各自转义。
- worker 与业务插件不得绕过本模块读取不可信文件；`ResourceImportService` 是资源读取唯一入口（public-interfaces §6 登记）。

## 6. 测试与证据

| 测试 | 覆盖 | 目标 |
| --- | --- | --- |
| PathBudgetTest | 路径矩阵（`..`、UNC、symlink、超长、大小写）、预算阈值 | `sdurws_ird_io_test` |
| CsvReaderTest / CsvWriterTest | BOM、引号、内嵌换行、公式样式字段、转义往返 | `sdurws_ird_io_test` |
| CatalogImportTest | manifest 哈希/文件集合、列字典单位/范围/外键、行数预算 | `sdurws_ird_io_test` |
| ResourceBoundaryTest | XML 实体、网络 URL、网格复杂度、JSON 深度、非有限数 | `sdurws_ird_io_test` |
| IoContractFixture | 安全读写端口失败/正常/边界三例供业务 WP 复用 | `sdurws_ird_io_contract_test` |

验证命令（脚本与原生双形式，均在仓库根执行）：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_io(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_io_test sdurws_ird_io_contract_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_io(_contract)?_test$"
```

证据必须含输入文件哈希、路径规范化结果、预算值、源行号/字段诊断、manifest 哈希、跨表引用图、导出前后文本和诊断 JSON。

## 7. 迁移与删除表

| 旧资产 | 处置 | 说明 |
| --- | --- | --- |
| 旧插件各自 CSV/JSON/路径读取 | Rewrite | 先以 adapter 只读接入，通过路径/预算/公式注入测试后替换 |
| Widget 内直接文件读取 | Delete | 收敛到 `ResourceImportService`（需求 §13.3） |
| 旧目录包解析旁路实现 | Delete | 删除前保留差异报告与输入样本 |
| 未登记工作簿解析依赖 | Delete | 禁止进入新构建（WP-01 依赖基线） |
