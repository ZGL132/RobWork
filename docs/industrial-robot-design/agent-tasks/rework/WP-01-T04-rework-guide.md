# WP-01-T04 实施者返工指引

> 签发：WP-01 工作包所有者，2026-08-31（用户授权裁决会话）。
> 生效条件：`WP-01-T04` 由治理上下文自 `Blocked` 解除后，实施者按本指引一次性完成返工；
> 本指引与修订后任务卡 `../WP-01-T04-gitlab-gate.md`、工作包计划 `../../work-packages/WP-01-build-dependency-baseline.md`（§5.5/§5.6/§6/§9）冲突时，以任务卡为准。

## 1. 阻塞背景与裁决结果（必读）

独立验证判定 `40dc8e8` 不通过并登记 `Blocked`（报告：`out/test-evidence/wp-01/20260831-40dc8e8-verify/t04-independent-verification.md`）。所有者已按报告 §5 作出四项裁决（冻结于任务卡"所有者裁决"字段）：

1. 模型测试正则收敛为 `^sdurws_ird_core_test$`（与 T03 冻结 build.ps1 两目标集一致；通配正则匹配未构建目标＝`Not Run`＝退出码 8＝门禁失败，此语义不得绕过）；
2. `package.ps1` 放弃全树 `cmake --install`，改为已构建目标集收集（`industrialrobot/` 无 `install(TARGETS)`/COMPONENT，全树安装必缺 `pqp.lib`）；
3. 平台双文件豁免：GitLab yml 保留为契约定义（Runner 接入前豁免执行），新增 GitHub Actions workflow 为执行通道；
4. GUI job 冻结为阶段 A"GUI 规则通道预检"（无已注册 GUI 测试可执行文件是记录事实，不是缺失）。

## 2. 分支、worktree 与环境

- 新建分支 `codex/wp-01-t04-gitlab-gate-r2`，基点＝main 当前 HEAD（≥`248a373`）。**不得 amend、rebase 或复用 `40dc8e8`**——该提交保留为 Blocked 记录参照；其 yml/package.ps1 内容可作为起点人工重落盘后按修订卡修改，返工产出**一个新的实现提交**。
- worktree 按计划 §5.6 五步模板准备：复制 gitignored 双模板 `RobWork/RobWorkStudio/bin/RobWorkStudio.ini.shared.in` 与 `RobWorkStudio.ini.template.static`；会话导出操作员 `CMAKE_PREFIX_PATH`（本机基线：`D:/software/QT/6.11.1/msvc2022_64;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows`，仅记录不设死于任何入库文件）；`out/` 全新构建。
- **禁止跨 worktree 消费主树 `out/build`**（40dc8e8 证据曾违反 §5.6 第 4 条，此次为验收硬项）；所有命令在本 worktree 内产生并消费构建工件。

## 3. 允许修改的文件白名单（超出即验证不通过）

| 动作 | 文件 |
| --- | --- |
| 修改 | `RobWork/gitlab-ci/industrial-robot-windows.yml` |
| 创建 | `.github/workflows/industrial-robot-windows.yml` |
| 修改 | `RobWork/scripts/industrial-robot/package.ps1`（`Configuration/BuildDirectory/LogDirectory` 参数签名不变） |
| 写原始日志 | `out/logs/industrial-robot/<timestamp>/` |
| 登记证据 | `out/test-evidence/wp-01/<run-id>/`（run-id：`<yyyymmdd>-<短SHA>-impl`） |

禁止：T01/T03 五个既有脚本（common/configure/build/run-tests/check-boundaries.ps1）任何变化；`industrialrobot/` 内 CMake 与源文件；旧插件；`requirements.md`、CSV、文档门禁脚本；主工作区用户未提交修改不得混入。

## 4. 执行顺序与逐字命令（预期退出码为验收硬指标）

按 TDD 次序执行；每条命令记录完整命令行、执行目录、退出码与日志路径。

1. **RED `t04-fail-blocks`（先于实现落盘）**：向 configure.ps1 传非法 `-SourceDirectory`（或其他任一前置步骤注入非零），按流水线顺序执行后续命令，逐命令捕获退出码，证明链条停止、失败可上传。预期：注入步骤非零、后续步骤全部未执行。**不得以叙述清单替代执行记录（40dc8e8 因此被判无效）。**
2. `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\configure.ps1 -Configuration Release` → 预期 **0**（前缀已导出且被日志记录）。
3. `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\build.ps1 -Configuration Release -NoConfigure` → 预期 **0**（产出 `sdurws_ird_core.lib`＋`sdurws_ird_core_test.exe`）。
4. `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Release -Regex '^sdurws_ird_core_test$' -NoConfigure` → 预期 **0、1/1 通过**（CI 模型 job 同命令；出现任何 `Not Run` 即实现错误）。
5. GUI 预检 job 同命令在 `QT_QPA_PLATFORM=windows` job 变量下复跑 → 预期 **0**（本地复跑时以会话变量等价模拟）。
6. `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1` → 预期 **0**。
7. `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\package.ps1 -Configuration Release` → 预期 **0**，产出安装 manifest（相对路径＋SHA-256，仅含 `sdurws_ird_core` 产物与白名单头；阶段 A 允许头集为空）与压缩包。
8. 原生回退（VsDevCmd x64 包装逐字执行）：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_core_test$"` → 预期与脚本形式一致（1/1 通过）。

## 5. 流水线文件要求

- 两文件（GitLab yml＋GitHub Actions workflow）步骤固定：checkout → VS x64 → configure → build → 模型测试 → GUI 通道预检 → check-boundaries → package → 上传日志/CTest XML/边界报告/安装 manifest；模型与 GUI 分离 job；脚本行逐字符一致（仅五个入口脚本同参数；两 job 正则均为 `^sdurws_ird_core_test$`）。
- `CMAKE_PREFIX_PATH`：yml `variables:` 与 workflow 顶层 `env` 双声明（§5.5）；脚本仅记录不修改。
- 缓存白名单仅 `.cache/industrial-robot/{dependencies,packages}/`；失败工件 `when: always`；集成默认分支保护。
- CI↔本机命令一致性表：逐 job 对照**两份文件**的脚本行，含"前缀来源"行（本地＝操作员会话导出，CI＝流水线变量）与 GUI 预检 job 精确对照行。
- GitHub Actions 运行记录：推送返工分支触发一次真实运行，归档 job 日志与工件清单；windows-latest 的 Qt/Boost 依赖供给（如 install-qt-action/vcpkg）属 workflow 实现内容；若 Runner 侧供给不可行 → 停止并升级集成负责人，**不得以本地日志冒充 CI 记录**。

## 6. 证据登记（两级口径，计划 §5.4）

- 原始日志：`out/logs/industrial-robot/<timestamp>/`（configure/build/test/package/boundary/red 各步，失败也保留）。
- 证据根：`out/test-evidence/wp-01/<run-id>/` 登记——`t04-fail-blocks` 阻断记录（含注入命令与退出码）、CI↔本机命令一致性表、red-green 前后结果表、package.log 副本、GitHub Actions 运行记录归档。
- 40dc8e8 曾因"无一必执行命令通过、build/边界无日志、RED 无执行记录"被判不通过——本条为复验重点。

## 7. 提交与验收

- 全部必执行命令退出码 0 后，创建**单个**实现提交，标题 `WP-01-T04: CI Windows 门禁`，正文中文分条以"修改/新增/测试/证据"开头；不得混入账本或用户修改。
- 独立验证将在实现 SHA 上复跑 §4 全部命令并核对：白名单范围、T01/T03 脚本零变化、两流水线文件与一致性表逐字符一致、前缀变量双声明、manifest 边界复检、RED 真实执行记录、两级证据根登记、GitHub Actions 运行记录。
- 停止与升级条件见任务卡；验收通过后由治理上下文签署 `Done` 并解锁 WP-01-T05。
