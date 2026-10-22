# REUSE-T01 / B19 独立验收

结论：**本地局部 PASS，远程未交付**。冻结 `d52fd06aafcb025a7d494d59c6d44bc9c16f3643`，对比基线 `eddb82a8e29f91660b048a69f4ec90c3d0a0e63a`。仅确认治理脚本 pending/proposal 格式补强及拒绝规则；不授全 T01、T02 准入、T06、迁移关闭、算法合规或上游模型权限。主会话报告三次推送与直连均超时，代理无监听，远程仍在基线、本地领先1；验收者未重复网络操作。

所有操作位于验收者独立 Temp 副本；共享现场只读，未实施、提交、推送或合主线。共享现场未提交的 test-governance-lite 与 .cache 不属于 B19。随机夹具内部临时提交只用于真实冻结读取。

| 验证 | 结果 |
| --- | --- |
| 原始 PS7 / PS5.1 完整报告复核 | 各 152 passed / 0 failed / 0 skipped / 0 notRun |
| 原始红跑 / 中间绿跑复核 | 0 passed / 8 failed；8 passed / 0 failed，彼此helper版本已区分 |
| 新鲜独立 PS7 / PS5.1 聚焦重跑 | 各 20 passed / 0 failed / 0 skipped / 0 notRun |
| 冻结函数行级 PS7 / PS5.1 探针 | 各 3 passed / 0 failed / 0 skipped / 0 notRun，单独的函数级范围 |
| verify-task | PASS，3 条机器命令真实执行，含 validate-task / validate-reuse / validate-docs |
| 变动范围 / commit-lint | PASS，340 路径，1 提交；新增允许路径335条，无删除或新增通配 |
| 生产、框架、vcpkg 变更 | 0；冻结已跟踪源码 diff 为空 |

逐例核对四组原始 JSON、JUnit、driver 行及每例日志，全部对应；原始输入 SHA256 与冻结原始字节或 Windows BOM/CRLF 检出字节对应，旧 helper 的红跑摘要亦重建相符。详见 original-evidence-audit.json / hash-variants.log。没有把完整152驱动重跑或旧红跑重跑写成已执行。

聚焦驱动由冻结原 test-reuse-governance.ps1 的隔离设置、Case、报告机制派生：保留新增10项，另测 owner / NONE / closed / audited / business-specific / kind、proposal正例、最终模式拒绝pending及两项冻结读取。每种PowerShell实际20项，报告、JUnit与每例日志齐全；最终helper原始SHA256与送验版本相同。冻结正例后故意破坏工作树矩阵，-Head仍通过，验证读取冻结对象。完整门禁的最终模式先被既有core pending拒绝，因此另用冻结helper及从冻结validator AST原样载入的依赖函数，对空产品基线的单一脚本行做3项隔离检查：pending正例、最终模式精确按该行ID拒绝pending、closed拒绝。两种Shell各3项，独立JSON/JUnit/每例日志保留；这部分仅函数级验证，不称完整validator路径。

CheckFunctions 将 execution 限于 foundation-governance、owner、NONE、直属 .ps1、入口和每个来源全文 inputs、空 targets 及 pending/proposal；产品来源不能借该标签逃逸。状态、能力与伪CMake目标拒绝保持，最终模式仍拒绝未关闭功能。execution不产生grant，也不能形成migrated/closed。没有19脚本实际execution行，当前foundation-governance原21行未变。本批只解决后续表达；command字符串包含entry不证明实际执行链，dot-source调用与真实功能仍待独立语义审计。

普通文档门禁通过，未执行 DeepEvidence、完整152驱动重跑或无关 C++/GUI 测试。diff --check 在 cr-at-eol 下仅报告两条原始15-missing-approval日志的“REUSE-PATH : ”尾空格；它们是真实输出，原样保留，不作代码缺陷。自制PS5.1驱动首轮写出时丢失BOM，解析失败且未启动测试；已保留 focused-ps51-no-bom-attempt.log，恢复BOM后的20项重跑是本结论依据。Temp初次检出长路径错误由本地core.longpaths配置解决。

无新增阻断发现。具体命令、退出码、SHA256和范围限制见 acceptance.json；各命令完整日志保存在同目录。本记录的 verdict 明确为局部格式验收，不能作为整任务迁移阶段 PASS 令牌。