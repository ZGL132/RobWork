# WP-09-T02 诊断目录与中文术语

- **Task ID / 需求 ID / ADR / 阶段：**WP-09-T02；ERR-01、UX-02、UX-03、UX-06、NFR-MNT-03；ADR-004（目录为文案唯一权威）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（基线中文文案散落在各插件 tr() 字符串，同义状态词并存）；契约 `architecture/public-interfaces.md` §6（`IDiagnosticCatalog` 签名）；方案 `module-design/diagnostics.md` v0.3 §3/§4
- **前置任务及必需工件：**WP-09-T01（Diagnostic 13 字段与 messageKey）、WP-01-T01（资源打包与 check-boundaries.ps1 装载校验入口）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/` 下 `resources/{diagnostics.zh-CN.json,terminology.zh-CN.json}`、`include/sdurws/ird/diagnostics/IDiagnosticCatalog.hpp`、`src/Catalog.cpp`、`test/CatalogTermsTest.cpp`、`testdata/diagnostics/catalog/`
- **禁止修改的文件和公共接口：**`IDiagnosticCatalog` 冻结签名（lookup/codesByCategory，`expected` 返回）、requirements 术语、在 C++ 硬编码用户文案、重复 code、未登记 action、禁用同义词
- **修改前接口：**无目录资源与 catalog 端口；文案由各插件内联中文字符串提供，无 messageKey
- **修改后接口：**`diagnostics.zh-CN.json` 条目含 `code/messageKey/titleZhCN/detailZhCN/action/severityDefault/allowedTokens[]`；`terminology.zh-CN.json` 含 canonical key、中文词、单位与禁用同义词；`DiagnosticCatalogContractTest` 注册于 `sdurws_ird_diagnostics_contract_test`
- **实施步骤：**按 v0.3 §3 登记表生成中文目录 → 编写术语表（单位、关节、姿态、碰撞、证据、任务）→ 启动校验（重复 code、缺语言项、未知 action、未声明占位符、孤立条目）→ 实现不可变 lookup → 本地化呈现（代码/稳定码英文，用户可见中文仅出自目录与术语表，需求 §10.3）
- **RED 测试：**`test/CatalogTermsTest.cpp`（注册于 `sdurws_ird_diagnostics_test`）：目录含重复 code、缺 messageKey、未知 action、未声明占位符或孤立条目时启动校验必须返回 `IRD-DIA-CATALOG-INVALID`（System）并阻止进入运行态——先确认测试在无实现时失败
- **最小实现：**两份资源＋UTF-8 解析＋启动校验＋`lookup(DiagnosticCode) -> DiagnosticInfo`；首版仅 zh-CN，messageKey 与术语 key 稳定不随文案调整变化
- **正常/边界/失败测试：**正常：合法 code 查询返回稳定 title/detail/action 与单位词；登记表与资源一致。边界：含占位符文案的 token 匹配、同义词禁用表命中、目录较大时的启动耗时记录。失败：上述校验失败项、缺失本地化项（不显示 code 代替文案，返回可定位诊断）
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_diagnostics(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_diagnostics_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_diagnostics(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；资源为 UTF-8 无 BOM；`rg -n 中文 RobWork/RobWorkStudio/src/rwslibs/industrialrobot` 确认业务插件无新增用户文案；目录 code 无重复（jq/PowerShell 校验记录）
- **证据工件：**`diagnostics/out/test-evidence/wp-09/<run-id>/`：目录与术语 hash、术语冲突报告、启动校验日志、资源清单、命令日志与评审签名
- **提交格式：**`WP-09-T02: 新增诊断目录与中文术语`

  - 新增 中文诊断目录与术语表资源及启动校验实现
  - 新增 目录校验失败测试与目标登记
  - 新增 目录/术语 hash 与冲突报告证据记录
- **停止与升级条件：**术语存在未决同义词、或目录与 requirements 词汇不一致时暂停并升级至产品负责人裁决词汇表
