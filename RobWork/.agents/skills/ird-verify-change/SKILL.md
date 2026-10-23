---
name: ird-verify-change
description: 变更验证；文档、治理脚本、产品代码或 GUI 修改后使用。
---

# ird-verify-change

按根 AGENTS §4 的影响矩阵选择命令：文档跑 validate-docs；脚本加正负例；产品跑集成受影响目标、测试、红线及复用检查；构建/接口等变化增加冒烟与对端回归。GUI 使用 ird-qt-gui-windows 的 Windows 真窗口规则。

原始日志、gtest XML/per-exe JSON 沿用现有输出机制；逐项如实写 PASS/FAIL/SKIP/NOT RUN，计数分列 passed/skipped/failed。失败先修复或报告。

全库证据索引用 validate-docs.ps1 -DeepEvidence 显式审计；context pack 只在实际使用时检查该任务包。无需每次 gate-all、重建历史索引或检查所有任务包。验收分级见根 §6.4。

执行根 AGENTS.md，按需取用本技能，不构成强制阶段链。

每轮只检查修改与实际受影响范围；未变且不受影响的目标不编译、重测或独验。先稳定代码和测试，最后一次性同步相关文档/矩阵/凭证。多任务需要文档与契约检查时，在 PowerShell 内调用 `validate-docs.ps1 -TaskFile $taskFiles`（数组），共用一次全量复用检查，不再单独重复其子命令；单任务执行 verify 用 verify-task。证据适用性、失败处理和最终准入以根 §4 为准，不因批次去重缩减检查条件。
