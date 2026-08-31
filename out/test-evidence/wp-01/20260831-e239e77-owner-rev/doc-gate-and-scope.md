# WP-01 所有者修订（三项非阻断观察落实）· 证据

- 性质：WP-01 工作包所有者治理修订（用户 2026-08-31 授权会话），非任务卡实现提交；不修改 `task-status.md`，不改变任何任务状态。
- 基线：main HEAD `e239e77`（代码与文档一致）；执行环境：Windows 10 x64、Git Bash 前端调用 powershell.exe 5.1 与 pwsh 7.6.5。
- 观察来源：状态账本 WP-01-T01/T02/T03 `Done` note 的非阻断观察（2026-08-30/31 独立验证记录）。

## 观察与修订对应

1. 证据根口径不一致（T01 提出，T02/T03 沿用）→ WP-01 计划新增 §5.4 两级目录（原始日志 `out/logs/industrial-robot/<timestamp>/` 脚本契约不变；登记证据根 `out/test-evidence/wp-01/<run-id>/`），T01～T05 卡证据工件登记根全部对齐，历史证据效力注记保留。
2. CMAKE_PREFIX_PATH 固化（T02 提出、T03 未吸收、待所有者裁决）→ 计划 §5.5 裁决"环境提供、脚本仅记录"：本地＝操作员会话导出，CI＝T04 yml 流水线变量；脚本行为冻结不改（改签名须另立任务卡）。
3. Worktree 环境前置模板缺失（T03 独立验证发现：新 worktree 需复制 gitignored 的 `RobWorkStudio.ini.shared.in`/`RobWorkStudio.ini.template.static` 双模板、导出操作员前缀、重建 out/）→ 计划 §5.6 五步清单；T04/T05 卡前置行引用。

未纳入本修订：T02 note 第 2 项"08-31 裁决建议补 D15 检查点"属架构注册表所有者职责，超出 WP-01 所有者范围，维持移交。

## 必需验证（修改治理文档 → 双 PowerShell 文档门禁）

| 命令（仓库根目录） | 退出码 | 结果 |
| --- | --- | --- |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./docs/industrial-robot-design/validate-development-docs.ps1` | 0 | `128 requirements, 19 acceptance tests, 25 contracts, 76 symbols, 5 ADRs, 0 trace gaps` |
| `pwsh -NoProfile -File ./docs/industrial-robot-design/validate-development-docs.ps1`（7.6.5） | 0 | 同上成功行 |
| `git diff --check` | 0 | 无空白错误 |

注：`-File` 参数在 Git Bash 下须用正斜杠；反斜杠形式被 shell 转义吞并（首次调用因此报 127，属调用方式错误，非门禁失败）。

## 范围

本次修订实际修改 6 个文档文件（工作树中用户既有的 WP-24 未提交修改与本修订零交集，未暂存）：

- docs/industrial-robot-design/work-packages/WP-01-build-dependency-baseline.md
- docs/industrial-robot-design/agent-tasks/WP-01-T01-boundary-tests.md
- docs/industrial-robot-design/agent-tasks/WP-01-T02-cmake-skeleton.md
- docs/industrial-robot-design/agent-tasks/WP-01-T03-test-entry.md
- docs/industrial-robot-design/agent-tasks/WP-01-T04-gitlab-gate.md
- docs/industrial-robot-design/agent-tasks/WP-01-T05-dependency-baseline.md

卡片 16 字段结构未增删（仅改写字段内容）；校验器内联命令门禁、陈旧基线数字门禁、表格检查均随 validate-development-docs.ps1 双跑通过。
