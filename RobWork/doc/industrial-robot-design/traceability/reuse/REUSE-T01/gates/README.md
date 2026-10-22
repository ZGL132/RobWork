# REUSE-T01 存量待审登记门禁修正

范围仅为 reuse-contract 的审计存量分支、自测夹具及契约，不修改产品或上游。固定基线、未变化且 decision/status 均未决的源码只能输出 REUSE-AD-PENDING，不创建权限；已解决/变动/新增生产文件及最终模式仍拒绝未授权路径。

实施者真实运行：

| 轮次 | expected/executed | passed | failed | skipped/notRun | 说明 |
| --- | --- | --- | --- | --- | --- |
| red（PS7） | 140/140 | 135 | 5 | 0/0 | 原规则拒绝存量未决登记，另暴露旧夹具对 T01 planned 状态的假设 |
| r2（PS7、PS5.1 各） | 140/140 | 139 | 1 | 0/0 | 阶段拒绝夹具被 manifest 不一致抢先拦截 |
| pass（PS7、PS5.1 各） | 140/140 | 140 | 0 | 0/0 | 显式设置测试前提，canonical manifest 与冻结输入保持配套 |

命令分别为 `pwsh` / `powershell -NoProfile -File RobWork/scripts/industrialrobot/test-reuse-governance.ps1 -OutputDir <独立目录>`，JSON 记录校验器/辅助脚本/驱动摘要及每例输出，JUnit 保留逐例结果。失败报告保留，不回写为通过。最终两次运行目录为 `.cache/t01-gate-r3-ps7`、`.cache/t01-gate-r3-ps51`；归档为 pass-* 文件。

`validate-docs.ps1`、`validate-task.ps1 -TaskFile .../REUSE-T01.json` 和 `git diff --check` 通过。全库历史证据深查、远程 CI/保护、产品数值迁移未执行。本报告是实施验证，不是独立验收，也不宣告 T01 全功能审计完成。
