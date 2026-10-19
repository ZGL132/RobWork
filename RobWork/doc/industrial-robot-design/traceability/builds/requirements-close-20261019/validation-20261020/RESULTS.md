# WP-14-T12：冻结后的提交检查（2026-10-20）

- PASS：`verify-task.ps1 -TaskFile tasks/foundation/WP-14-T12.json`实际执行三条机器命令：集成全量Release、ird_gates、validate-docs；无散文豁免。
- PASS：暂存树范围预检432个路径均在契约75条允许规则内；临时commit-tree仅用于校验候选树，未移动分支。此目录追加的日志均在同一traceability允许范围内，交付提交仍可直接以601ffa0e..HEAD重新执行范围检查。
- PASS：commit-lint对本批完整四段中文提交信息预检通过；显式WIP承认总任务未完成。
- PASS：补充core::BuildRedLine四例均通过，XML与日志在本目录；此既有目标使用gtest_main，没有输出请求的per-exe JSON，不伪造报告。该补充不混入上级README的2256次执行计数。

上级快照保持不变：12个gtest剪贴板失败（两模式各6项）、宿主tour的1项TSV失败，以及OPEN-ISSUES.md登记的产品/验证缺口仍未消除。任务保持ready，不作全部封闭或独立验收PASS声明。

snapshot-sha256.json与source-files.json记录冻结时原始字节哈希。仓库检出可能按.gitattributes转换文本换行；跨检出内容校验以仓库evidence-index.json的CRLF→LF规范化哈希为准，不能将换行转换误报为产品改变。二进制PNG及帮助部署检查仍按原始字节核对。
