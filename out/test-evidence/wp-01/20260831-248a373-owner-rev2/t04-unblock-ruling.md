# WP-01-T04 解除阻塞裁决（所有者修订）· 证据

- 性质：WP-01 工作包所有者治理修订（用户 2026-08-31 授权裁决会话）；不修改 `task-status.md`（T04 的 `Blocked`→解除由治理上下文在本修订后登记）。
- 基线：main HEAD `248a373`（T04 Blocked 登记提交）；裁决输入：`out/test-evidence/wp-01/20260831-40dc8e8-verify/t04-independent-verification.md` §4/§5。
- 裁决产物：计划 §1/§3/§5.5/§6/§9/§10/§验证/退出条件/卡索引修订；任务卡 WP-01-T04 全卡重写（16 字段保持）；新增 `agent-tasks/rework/WP-01-T04-rework-guide.md`（子目录，不入 validate-development-docs 与 generate-traceability 的顶层卡扫描，已核实二者均为非递归扫描）。

## 四项裁决（对应验证报告 §5.1 选项 b＋c、§5.2 豁免、§5.3 细则）

1. 测试正则收敛 `^sdurws_ird_core_test$`（T03 冻结 build.ps1 目标集 `sdurws_ird_core`＋`sdurws_ird_core_test`；`Not Run`＝退出码 8＝门禁失败语义不放宽）。
2. package.ps1 改已构建目标集收集（依据：`IndustrialRobotTargets.cmake` 仅有 `if(EXISTS include)` 守卫的公共头目录安装、无 `install(TARGETS)`/COMPONENT；全树安装必触未构建的 `pqp.lib`）。
3. 平台双文件豁免：GitLab yml 保留为契约定义（修改不删除，Runner 接入前豁免执行），新增 `.github/workflows/industrial-robot-windows.yml` 执行通道（windows-latest、逐字符同命令集）；上游总纲三处 GitLab 措辞（L118/L294/L325）经"契约定义＋执行通道"口径满足、不改写总纲；NFR-DEP-01 原文无 CI 平台字样，裁决不触及需求层。
4. GUI job 阶段 A 冻结为 GUI 规则通道预检（唯一已构建测试＋`QT_QPA_PLATFORM=windows` 显式声明）。

## 验证

| 命令（仓库根目录） | 退出码 | 结果 |
| --- | --- | --- |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./docs/industrial-robot-design/validate-development-docs.ps1` | 0 | `128 requirements, 19 acceptance tests, 25 contracts, 76 symbols, 5 ADRs, 0 trace gaps` |
| `pwsh -NoProfile -File ./docs/industrial-robot-design/validate-development-docs.ps1`（7.6.5） | 0 | 同上成功行 |
| `git diff --check`（工作树与暂存区） | 0 | 无空白错误 |

## 范围

本次修订实际修改/新增：`work-packages/WP-01-build-dependency-baseline.md`、`agent-tasks/WP-01-T04-gitlab-gate.md`（全卡重写）、`agent-tasks/rework/WP-01-T04-rework-guide.md`（新增）、本证据文件。工作树中用户既有 WP-24 未提交修改零交集、未暂存。

待治理上下文：复核本裁决后将账本 WP-01-T04 的 `Blocked` 解除（note 引用本提交 SHA 与返工指引），实施者按 `agent-tasks/rework/WP-01-T04-rework-guide.md` 在新分支 `codex/wp-01-t04-gitlab-gate-r2` 重做；`WP-01-T05` 维持 `Planned` 至 T04 签署 `Done`。
