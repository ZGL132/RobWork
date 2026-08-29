# WP-09-T02 诊断目录与中文术语

- Task ID：WP-09-T02
- 需求/阶段：ERR-01、UX-02、UX-03、UX-06、NFR-MNT-03；阶段 A / R1
- 架构契约：`architecture/public-interfaces.md`、`architecture/testing-contract.md`；模块方案：`module-design/diagnostics.md`
- 前置：WP-09-T01、WP-01 资源打包脚本。
- 允许：修改 `diagnostics/resources/diagnostics.zh-CN.json`、`terminology.zh-CN.json`、`include/.../IDiagnosticCatalog.hpp`、`src/Catalog.cpp`、`test/CatalogTermsTest.cpp`。
- 禁止：在 C++ 硬编码用户文案、重复 code、改变需求术语或添加未登记 action。
- 产出：唯一中文目录、术语表和启动完整性校验。

## 数据流

`catalog resources -> parse UTF-8 -> validate code/messageKey/action/tokens/terms -> immutable lookup -> localized presentation`。目录加载失败必须阻止运行态。

## Given/When/Then

- Given重复 code、缺 messageKey、未知 action、未声明占位符或孤立条目，When load，Then返回 System 诊断并拒绝目录。
- Given合法 code 和术语，When lookup，Then返回稳定 title/detail/action 和单位词。
- Given缺失本地化项，When display，Then不显示 code 代替文案，返回可定位诊断。

## 测试、证据与提交

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_catalog_terms_test$'
```
证据：目录 hash、术语冲突报告、启动校验日志。提交：`WP-09-T02: implement diagnostic catalog and terminology`。

停止：术语存在未决同义词或目录与需求词汇不一致时暂停。
