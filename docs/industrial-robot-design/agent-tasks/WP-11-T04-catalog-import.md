# WP-11-T04 目录包与跨表引用

- Task ID：WP-11-T04
- 需求/阶段：REQ-05、SEL-01～02、NFR-REL-04、NFR-SEC-01～03、AT-08；阶段 A / R1
- 架构契约：`architecture/persistence-schema.md`、`architecture/testing-contract.md`；模块方案：`module-design/secure-io.md`
- 前置：WP-11-T01/T02、WP-04 content object。
- 允许：修改 `io/include/.../CatalogPackageReader.hpp`、`src/CatalogPackageReader.cpp`、`test/CatalogImportTest.cpp`、`testdata/io/catalog/`。
- 禁止：实现 WP-19 筛选/淘汰规则、忽略 manifest、多余文件或悬空引用。
- 产出：manifest/文件哈希、列单位、曲线和 compatibility 外键校验，形成不可变 CatalogVersion。

## Given/When/Then

- Given固定六文件目录包，When import，Then校验 schema、目录 ID/版本、文件名、SHA-256、列和单位后生成 CatalogVersion。
- Given缺/多文件、哈希错误、重复型号、曲线无序/不覆盖或悬空外键，When import，Then拒绝且无部分目录版本。
- Given同一包重复导入，When import，Then content identity 和校验结果稳定。

## 测试、证据与提交

命令：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_catalog_import_test$'`。证据：manifest/hash 清单、引用图、失败诊断和版本身份。提交：`WP-11-T04: implement catalog package validation`。

停止：目录字段或跨表语义未冻结时暂停，不替业务层补默认值。
