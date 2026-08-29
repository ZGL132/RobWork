# 安全导入导出模块详细方案

- 方案版本：v0.2；需求基线：v0.7；负责 WP：WP-11；阶段/发布：阶段 A / R1
- 架构契约：`architecture/persistence-schema.md`、`architecture/testing-contract.md`、`architecture/public-interfaces.md`

## 1. 模块职责与目录

模块负责路径边界、字节/解析预算、UTF-8 CSV、JSON 文档、URDF/网格安全读取、目录包验证和 CSV 安全写出。业务适配器只消费安全记录，不重复解析或加载外部资源；项目 revision 由 WP-04 显式命令提交。

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
```

目标：`sdurws_ird_io`、`sdurws_ird_io_test`、`sdurws_ird_io_contract_test`。允许 WP-03 core、WP-04 content object、Qt Core 和标准库；禁止 Qt Widgets、工作簿解析库、业务插件私有 I/O。

## 2. 路径与预算流程

`untrusted path -> UTF-8 decode -> normalize POSIX -> reject absolute/UNC/.. / symlink escape -> canonical boundary check -> budget preflight -> bounded read`。`ImportBudget` 字段为单文件/总字节、XML/JSON 深度、记录/字段/字符串数、网格顶点/三角形、压缩展开比和文件数；任何超限返回 `IRD-IO-BUDGET-EXCEEDED`，不产出部分记录。

## 3. CSV 读取与写出

Reader 只接受 UTF-8/UTF-8 BOM，按 RFC 4180 处理引号、逗号、CRLF/LF 和内嵌换行；记录 sourceLine、fieldName、rawText、normalizedValue、diagnostics。类型转换必须显式单位和有限性校验，非法值不能转零。Writer 对文本值首字符 `=`,`+`,`-`,`@` 加统一安全前缀；真正数值按数值列输出，保留原始值在 JSON 证据而非 CSV 公式。

## 4. 目录包验证

读取 manifest 后检查固定文件集合、额外文件、schema、目录 ID/版本、SHA-256、列/单位/行数预算。校验 motor/reducer 唯一性、curve 点序、覆盖范围和 compatibility 外键；全部通过才生成不可变 `CatalogVersion`。筛选/淘汰规则归 WP-19。

## 5. URDF、网格和 JSON 边界

通用层只读取受预算约束的字节，禁用 XML 外部实体、DOCTYPE、网络 URL、命令、宏和资源根外引用；网格仅检查格式、顶点/三角形上限和有限坐标，语义转换归 WP-13。JSON 拒绝未知未来 schema、非有限数、过深嵌套和重复关键字段。缺失/损坏资源返回稳定诊断，旧项目状态不变。

## 6. 安全失败与证据

输入错误为 Input，跨表悬空/资源缺失为 Engineering/DataInsufficient，磁盘/权限/解析库为 System。证据记录输入哈希、规范路径、预算、源行号、manifest、引用图、导出前后文本和诊断 JSON。任何失败不得创建 revision 或 CatalogVersion。

## 7. 测试与评审

测试覆盖路径矩阵、预算阈值、BOM/引号/换行、公式样式字符串、目录包哈希/引用、XML 实体、网络 URL、网格复杂度和 JSON 深度。评审确认只有本模块处理安全 I/O、业务插件无旁路读取、CSV 公式不执行、项目原状态保持。

## 8. 迁移

旧读取器先由 adapter 包装；未通过路径/预算/公式注入测试标 Rewrite/EvidenceOnly，删除旁路实现前保留差异报告和输入样本。
