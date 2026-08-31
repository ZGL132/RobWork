# WP-01-T04 CI Windows 门禁

- **Task ID / 需求 ID / ADR / 阶段：** WP-01-T04；NFR-DEP-01（Windows x64 正式验收、CI 与本机同命令）、NFR-DEP-02（安装 manifest 不含开发机路径）；无直接关联 ADR；阶段 A 前提 / R1。契约：`architecture/testing-contract.md`。
- **基线 commit：** 代码基线：main 当前 HEAD（2026-08-31 返工基点 `248a373`）；文档基线：main 当前 HEAD（含 2026-08-31 所有者两次修订与本返工修订）
- **前置任务及必需工件：** WP-01-T03；工件：`RobWork/scripts/industrial-robot/{common,configure,build,run-tests}.ps1`（模型路径链路可用，build 目标集冻结为 `sdurws_ird_core`＋`sdurws_ird_core_test`）与 WP-01-T01 的 `check-boundaries.ps1`、`out/logs/industrial-robot/<timestamp>/test.log`。实施与验证所用 worktree 按 WP-01 计划 §5.6 环境准备模板准备（双 ini 模板复制、操作员 `CMAKE_PREFIX_PATH` 按 §5.5 会话导出、out/ 重建、禁止跨 worktree 消费主树 out/build）。
- **所有者裁决（2026-08-31，用户授权；解除 Blocked 的上游依据，对应验证报告 §5.1/§5.2）：**
  1. **测试正则收敛**（验证报告 §5.1 选项 b）：T04 阶段模型测试正则冻结为 `^sdurws_ird_core_test$`，与 T03 冻结的 build.ps1 目标集一致；正则匹配未构建目标产生的 CTest `Not Run`（退出码 8）属门禁失败语义，不得放宽退出码判定。目标集扩展的后续任务同步扩展正则并保持 CI↔本机逐字符一致（计划 §6）。
  2. **打包边界收敛**（验证报告 §5.1 选项 c）：`package.ps1` 放弃全树 `cmake --install`（`industrialrobot/` 仅有带 `if(EXISTS include)` 守卫的公共头目录安装规则，无 `install(TARGETS)` 与 COMPONENT 声明；全树安装必然触碰未构建的 RobWork 工件如 `pqp.lib`）；改为按已构建目标集收集——仅收集 `sdurws_ird_core` 链接库产物与 core 模块 `include/` 白名单公共头（阶段 A 允许头集为空、目录缺失视为空集，不得因空头集失败），manifest 相对路径＋SHA-256、禁装黑名单与生成后复检契约不变。NFR-DEP-02 的整机安装包验收在阶段 E（WP-24）；本卡 manifest 为阶段 A 门禁工件，收集边界必须与已构建目标集一致。
  3. **平台双文件豁免**（验证报告 §5.2，计划 §9）：仓库 remote 为 GitHub、无 GitLab Windows Runner。`RobWork/gitlab-ci/industrial-robot-windows.yml` 保留为门禁契约定义（修改而非删除：补前缀变量、正则收敛、GUI 预检语义），其 Runner 实际执行在接入前豁免；新建 `.github/workflows/industrial-robot-windows.yml`（GitHub Actions、windows-latest）为执行通道，与 yml 步骤顺序/job 划分/缓存白名单/脚本行逐字符一致。门禁证据＝本地按流水线步骤逐字符复跑（主）＋ GitHub Actions 运行记录（旁证）。上游 `development-task-breakdown.md` 的"双重自动门禁"措辞经此口径满足，不修改总纲。
  4. **GUI job 阶段 A 语义**：阶段 A 无已注册 GUI 测试可执行文件（10 个已注册测试中仅 core 被构建，ui smoke 非 Widget 测试）。GUI job 冻结为"GUI 规则通道预检"：以唯一已构建测试执行 run-tests.ps1，job 变量显式声明 `QT_QPA_PLATFORM=windows`，验证 windows 平台强制、冲突变量先停、单进程链路；禁止 offscreen 与并行。交付真实 GUI 测试目标的任务须同步切换该 job 正则。
- **允许创建/修改/删除的文件：**
  - 修改：`RobWork/gitlab-ci/industrial-robot-windows.yml`（门禁契约定义：`CMAKE_PREFIX_PATH` variables、模型 job 正则 `^sdurws_ird_core_test$`、GUI job 改通道预检并保留精确对照行）
  - 创建：`.github/workflows/industrial-robot-windows.yml`（GitHub Actions 执行通道，与 yml 逐字符一致）
  - 修改：`RobWork/scripts/industrial-robot/package.ps1`（安装边界改为已构建目标集收集；`Configuration/BuildDirectory/LogDirectory` 参数签名不变）
  - 写运行日志：`out/logs/industrial-robot/<timestamp>/`（原始日志）；证据工件登记于 `out/test-evidence/wp-01/<run-id>/`（两级口径见 WP-01 计划 §5.4）
- **禁止修改的文件和公共接口：** `industrialrobot/` 内 CMake 与源文件；旧插件；`requirements.md`、CSV、文档门禁脚本；T01/T03 既有脚本（common/configure/build/run-tests/check-boundaries.ps1）零变化；CI 缓存不得包含正式测试结果、结果数据库或项目快照。
- **修改前接口：** `RobWork/gitlab-ci/industrial-robot-windows.yml`（40dc8e8，Blocked 未签署）：GitLab 语法六 job needs 链，无 `CMAKE_PREFIX_PATH` 变量，模型 job 通配正则与 GUI job `^sdurws_ird_ui_test$` 均匹配未构建目标（结构性退出码 8）。`package.ps1`：以全树 `cmake --install` 出包（缺未构建工件即退出码 1）。仓库无 `.github/workflows/`。
- **修改后接口：** 两份流水线文件步骤固定为 checkout → VS x64 → configure → build → 模型测试 → GUI 通道预检 → check-boundaries → package → 上传日志/CTest XML/边界报告/安装 manifest；模型与 GUI 分离 job；`CMAKE_PREFIX_PATH` 于 yml `variables:` 与 workflow 顶层 `env` 双声明（Runner 构建依赖前缀，WP-01 计划 §5.5），脚本仅记录不修改；缓存白名单仅 `.cache/industrial-robot/{dependencies,packages}/`；失败上传工件（`when: always`）；集成默认分支保护。两文件每个脚本行与本地命令逐字符一致（引用 `configure.ps1/build.ps1/run-tests.ps1/check-boundaries.ps1/package.ps1` 同参数；模型 job 与 GUI 预检 job 正则均为 `^sdurws_ird_core_test$`）。CI↔本机命令一致性表逐 job 对照两文件脚本行，含"前缀来源"对照行（本地＝操作员会话导出，CI＝流水线变量）与 GUI 预检 job 精确对照行。`package.ps1` 产出安装 manifest（相对路径＋SHA-256；不含测试数据/私有头/绝对构建路径）与压缩包。
- **实施步骤：**
  1. 于干净 worktree（计划 §5.6 五步模板准备）先执行"RED 测试"并保留注入执行记录（每条注入命令与退出码）。
  2. 修改 `package.ps1`：移除全树 `cmake --install`，实现两目标构建集收集（`sdurws_ird_core` 链接库产物＋core include 白名单头，头集允许为空），manifest 生成与黑名单复检逻辑保留。
  3. 修改 `RobWork/gitlab-ci/industrial-robot-windows.yml`：`CMAKE_PREFIX_PATH` variables、模型 job 正则收敛、GUI job 改通道预检；创建 `.github/workflows/industrial-robot-windows.yml` 与其逐字符一致（needs 链、缓存白名单、失败工件、分支保护）。
  4. 逐 job 对照两文件脚本行与本地命令，形成"CI↔本机命令一致性表"（含前缀来源行）。
  5. 本地按流水线顺序复跑全部命令（脚本形式＋原生回退）并保存日志；原始日志落 `out/logs/industrial-robot/<timestamp>/`，汇总登记证据根。
  6. 推送返工分支至 GitHub 触发一次 Actions 运行，归档 job 日志与工件清单作为流水线执行证据；Runner 侧依赖供给不可行时停止并升级集成负责人，不得以本地日志冒充 CI 记录。
- **RED 测试：** `t04-fail-blocks`：令任一前置步骤退出非零（如向 configure.ps1 传非法 `-SourceDirectory`），Then 同链后续步骤不执行且依赖该 job 的后续 job 全部阻断、失败工件仍上传——以本地顺序执行真实复现，逐命令捕获退出码；不得以叙述清单替代执行记录。
- **最小实现：** 仅修改 yml、创建 workflow、修改 `package.ps1` 三个文件；不改 T01/T03 脚本，不新增缓存内容类别，不引入 Runner 专属参数，不放宽任何退出码判定。
- **正常/边界/失败测试：**
  - 正常：Given 全链脚本通过，When 本地按流水线顺序复跑，Then configure/build/模型测试/GUI 预检/check-boundaries/package 全部退出码 0。
  - 边界：Given 缓存策略与 manifest 复检，When 审查两份流水线文件与 package.ps1，Then 缓存路径仅 `.cache/industrial-robot/{dependencies,packages}/`；manifest 无测试数据/私有头/绝对构建路径；包内容仅含已构建目标集产物与白名单头（阶段 A 允许仅 core 链接库）。
  - 失败：Given 注入的步骤失败，When 流水线执行，Then 后续 job 阻断、日志与失败工件上传、集成默认分支不接受合并。
- **精确验证命令：**（仓库根目录、VS x64 环境；T03 已交付入口，脚本＋原生双形式；本地执行前按计划 §5.5/§5.6 导出操作员前缀）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_core_test$'`；预期退出码 0、1/1 通过（CI 模型测试 job 同命令）。
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1`；预期退出码 0。
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\package.ps1 -Configuration Release`；预期退出码 0 且生成安装 manifest 与压缩包（含 `sdurws_ird_core` 产物）。
  - 原生回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_core_test$"`；预期与脚本形式结果一致（1/1 通过）。
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `RobWork/gitlab-ci/industrial-robot-windows.yml`、`.github/workflows/industrial-robot-windows.yml`、`RobWork/scripts/industrial-robot/package.ps1`；T01/T03 脚本与 `industrialrobot/` 零变化；两文件无 offscreen、无并行 GUI、缓存项逐一对照白名单；一致性表含前缀来源行与 GUI 预检行。
- **证据工件：** `out/test-evidence/wp-01/<run-id>/package.log`（原始日志自脚本契约目录复制入根）、CI↔本机命令一致性表（含前缀来源行）、`t04-fail-blocks` 阻断记录（含注入命令与退出码）、GitHub Actions 运行记录（job 日志与工件清单归档）。
- **提交格式：** `WP-01-T04: CI Windows 门禁`

  - 修改 GitLab yml 门禁契约并新增 GitHub Actions 执行通道（同命令集）
  - 修改 package.ps1 为已构建目标集收集出包
  - 新增一致性校验（含前缀来源）、失败传播与运行证据记录
- **停止与升级条件：** GitHub Actions Windows 执行机不可用或 Runner 侧依赖供给无法满足、流水线文件步骤与本地脚本无法逐字符一致，或打包需要引入未批准依赖时，停止并升级给集成负责人；流水线编写者不得同时担任 WP-01-T05 依赖评审者。

> 所有者修订记录：2026-08-31 返工修订（用户授权裁决）。原"GitLab Windows 门禁"卡及实现提交 `40dc8e8`（基点 `e239e77`）经独立验证判定不通过并登记 `Blocked`（`out/test-evidence/wp-01/20260831-40dc8e8-verify/t04-independent-verification.md`）；本卡按其 §5.1 选项 b＋c、§5.2 豁免口径与 §5.3 返工细则修订，上游总纲 GitLab 措辞经"契约定义＋执行通道"双文件口径满足、不改写总纲。返工执行口径见 `agent-tasks/rework/WP-01-T04-rework-guide.md`。
