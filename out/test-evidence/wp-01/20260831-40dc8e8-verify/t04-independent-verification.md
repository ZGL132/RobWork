# WP-01-T04 GitLab Windows 门禁 · 独立验证报告（不通过）

- Task ID：WP-01-T04；阶段 A 前提 / R1；需求锚点：NFR-DEP-01、NFR-DEP-02
- 验证者：独立验证者/治理协调（ZCode 治理会话，与实施者不同执行上下文）
- 验证日期：2026-08-31；验证运行 ID：`20260831-40dc8e8-verify`
- 实现提交：`40dc8e877ea43748917e2869e174c54390c430db`（`WP-01-T04: GitLab Windows 门禁`，父提交 `e239e77`）
- **基线分歧**：实现提交基点为 `e239e77`；main 现 HEAD 为所有者修订 `35acbd7`（2026-08-31 09:55，晚于实现提交 09:06）。修订修改了本卡（吸收 CI `CMAKE_PREFIX_PATH` 流水线变量、一致性表前缀来源行、两级证据根）。本验证按权威顺序以 main 当前 HEAD 的修订后卡为准。
- 验证上下文：新建隔离 detached worktree @ 40dc8e8（验证后删除）；环境按 WP-01 计划 §5.6 五步模板准备（双 ini 模板复制、`CMAKE_PREFIX_PATH=D:/software/QT/6.11.1/msvc2022_64;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows` 会话导出、out/ 重建）。

## 1. 独立复跑结果（隔离 worktree 根目录）

| # | 命令（逐字） | 退出码 | 结果 |
| --- | --- | --- | --- |
| yml-1 | `configure.ps1 -Configuration Release` | 0 | Release 配置成功（前缀已导出，脚本记录该前缀） |
| yml-2 | `build.ps1 -Configuration Release -NoConfigure` | 0 | `sdurws_ird_core.lib`＋`sdurws_ird_core_test.exe`（Release）生成 |
| yml-3 | `run-tests.ps1 -Configuration Release -Regex '^sdurws_ird_.*_test$' -NoConfigure` | **8** | **1/10 通过，9 个 `***Not Run`**：通配 regex 匹配 10 个已注册测试（core/project/evidence/runtime/policy/execution/diagnostics/io/reporting/ui），其中 9 个可执行文件从未被构建（T03 冻结的 build.ps1 仅构建 `sdurws_ird_core`＋`sdurws_ird_core_test` 两个目标） |
| yml-4 | `check-boundaries.ps1` | 0 | 32 文件扫描通过 |
| yml-5 | `package.ps1 -Configuration Release` | **1** | `cmake --install` 全树安装失败：`file INSTALL cannot find …/RobWork/libs/Release/pqp.lib`（build.ps1 两目标构建不产生 PQP 等全树安装工件）；未产出 manifest/压缩包 |
| 卡-1 | `run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_.*_test$'`（先 `build.ps1 -Configuration Debug` 退出码 0） | **8** | 与 yml-3 同一机制：9/10 Not Run |
| 卡-4 | 原生回退 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_core_test$"`（VsDevCmd x64 包装逐字执行） | 0 | 1/1 通过——窄 regex 形式与 T03 结果一致 |

控制台捕获与脚本自写日志副本见本目录（`t04-yml-*.console.log`、`t04-card-*.console.log`、`t04-native-ctest.console.log`、`run-logs/20260831-10*/`）。

## 2. 实现者证据审计（worktree `wt-wp01-t04` out/logs，未提交、未登记证据根）

| 证据 | 内容 | 判定 |
| --- | --- | --- |
| `20260831-083734/configure.log` | Release 配置 **退出码 1**：`CMAKE_PREFIX_PATH=<未设置>`（未按 §5.5/§5.6 导出前缀） | 失败，无后续成功记录 |
| build 步骤 | 无任何 build.log | 缺失 |
| `20260831-084744/test.log` | Debug 通配 regex **零匹配失败**（构建目录无注册测试） | 失败 |
| `20260831-084928/test.log` | Release `-NoConfigure` **退出码 8**；测试输出路径位于**主树** `D:\...\RobWork\out\build\industrial-robot`（跨 worktree 消费，违反计划 §5.6 第 4 条） | 失败＋范围违规 |
| 边界扫描记录 | 无 | 缺失 |
| `20260831-084123/package.log` | **退出码 1**（`cmake --install` 失败；BuildDirectory 指向主树构建目录） | 失败，且无成功运行 |
| `20260831-091000/ci-command-consistency.log` | 5 行 Release 命令"exact"对照表 | 无修订卡要求的"前缀来源"行；GUI job 无精确对照行 |
| `20260831-091000/t04-fail-blocks.log` | 任务链"present/BLOCKED by"叙述清单 | **无实际注入执行命令与退出码捕获**，不构成卡内 RED"本地顺序执行复现"的有效记录 |
| Runner 流水线 job 日志与工件清单 | 不存在（仓库 remote 为 GitHub，无 GitLab 实例与 Windows Runner） | 缺失，触发卡内停止条件 |
| 证据根登记 | 全部留在 `out/logs/industrial-robot/<ts>/`，未登记 `out/test-evidence/wp-01/<run-id>/`（修订卡与计划 §5.4） | 缺失 |

## 3. 静态检查

- 提交范围：仅 `RobWork/gitlab-ci/industrial-robot-windows.yml`＋`RobWork/scripts/industrial-robot/package.ps1` 两个允许文件，215 行新增零删除，T01/T03 既有脚本逐字未动 ✓；提交标题与卡一致 ✓。
- package.ps1：参数符合卡（Configuration/BuildDirectory/LogDirectory）、相对路径 manifest＋SHA-256、禁装 blacklist（testdata/tests/_test/私有头/pdb/ilk）与生成后复检、PS 5.1 解析零错误 ✓（但端到端未能成功运行，见上表）。
- yml：禁项扫描干净（无 offscreen/并行 GUI）✓；缓存白名单仅 `.cache/industrial-robot/{dependencies,packages}/` ✓；失败工件 `when: always` ✓；模型/GUI 分离 job ✓。
- **yml 无 `CMAKE_PREFIX_PATH` 流水线变量**（仅 GUI job 的 `QT_QPA_PLATFORM`）——修订后卡与计划 §5.5 第 3 条明确要求 ✗。

## 4. 阻塞原因（不签署 Done 的依据）

1. **卡内必执行命令在本任务允许范围内不可满足（上游契约缺陷）**：模型测试命令 regex `^sdurws_ird_.*_test$` 匹配 10 个已注册测试，但 T03 冻结的 build.ps1 仅构建 2 个目标（9 个 `Not Run`，退出码 8）；package 的 `cmake --install` 需要全树安装工件而两目标构建不产生（缺 `pqp.lib`，退出码 1）。修复路径均越出 T04 允许范围：改 build.ps1 目标集/改 CMake 安装规则均被"既有脚本零变化、industrialrobot/ 禁改"禁止；缩窄 regex 则违反"CI 模型测试 job 同命令、逐字符一致"契约。需工作包所有者修订卡/计划或另立卡裁决。
2. **Runner 流水线证据不可产出**：remote 为 GitHub（无 GitLab 实例与 Windows x64 Runner），卡内步骤 6 与证据工件"Runner 流水线 job 日志与工件清单"无法满足，且实现者未按卡内停止条件"Runner 无 Windows x64 执行机 → 停止并升级集成负责人"执行。
3. **修订后卡要求未实现**（实现提交早于 `35acbd7`）：yml 无 `CMAKE_PREFIX_PATH` 流水线变量、一致性表无前缀来源对照行、证据未按两级口径登记 `out/test-evidence/wp-01/<run-id>/`。
4. **实现者证据中无一必执行命令通过**且 build/边界步骤无证据、存在跨 worktree 消费主树构建目录的范围违规；RED `t04-fail-blocks` 仅有叙述清单、无实际执行记录。

## 5. 返工条件（解除 Blocked 需同时满足）

1. 工作包所有者裁决上游冲突并在卡/计划中冻结其一：a) T03 build.ps1 目标集扩展（另立任务卡，禁止本卡携带）；b) 模型测试命令 regex 与 CI job 命令改为与已构建目标集一致；c) package 改为按已构建目标集可行的安装/收集方式（或 CI 链增加全树构建步骤）。修订须同步"CI 模型测试 job 同命令"与一致性表口径。
2. 集成负责人就 Runner 给出裁决：提供 GitLab Windows x64 Runner 环境（则补跑步骤 6 证据），或正式决定步骤 6 降级/延后并以书面豁免入档（豁免须指明替代证据口径）。
3. 实现者按修订后卡补齐：yml `CMAKE_PREFIX_PATH` 流水线变量（§5.5）、一致性表前缀来源行、真实执行的 `t04-fail-blocks`（含注入命令与退出码）、build/边界步骤日志、两级证据根登记；全部必执行命令退出码 0 后按 §5.6 于干净 worktree 重出实现提交（现行 40dc8e8 可 amend 或重开分支，由所有者指定）。

## 6. 结论

**独立验证不通过：不予签署 Done，登记 `Blocked`。** WP-01-T05 保持 `Planned`。实现工件（yml 骨架与 package.ps1）静态面基本合规且 PS 5.1 可解析，但卡内两条必执行命令（模型测试通配、package）与上游契约结构性冲突，在 T04 允许范围内无解，须先行裁决。
