# WP-01-T04 GitLab Windows 门禁

- **Task ID / 需求 ID / ADR / 阶段：** WP-01-T04；NFR-DEP-01（Windows x64 正式验收、CI 与本机同命令）、NFR-DEP-02（安装包不依赖开发机路径）；无直接关联 ADR；阶段 A 前提 / R1。契约：`architecture/testing-contract.md`。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-01-T03；工件：`RobWork/scripts/industrial-robot/{common,configure,build,run-tests}.ps1`（模型路径链路可用）与 WP-01-T01 的 `check-boundaries.ps1`、`out/logs/industrial-robot/<timestamp>/test.log`。
- **允许创建/修改/删除的文件：**
  - 创建：`RobWork/gitlab-ci/industrial-robot-windows.yml`
  - 创建：`RobWork/scripts/industrial-robot/package.ps1`（Configuration/BuildDirectory/LogDirectory 参数，产出安装 manifest，不含测试数据/私有头/绝对构建路径）
  - 写运行日志：`out/logs/industrial-robot/<timestamp>/` 与 CI 工件
- **禁止修改的文件和公共接口：** `industrialrobot/` 内 CMake 与源文件；旧插件；`requirements.md`、CSV、文档门禁脚本；T01/T03 既有脚本签名；CI 缓存不得包含正式测试结果、结果数据库或项目快照。
- **修改前接口：** 无（仓库无 industrial-robot 流水线与打包脚本）。
- **修改后接口：** `industrial-robot-windows.yml` 步骤固定为 checkout → VS x64 → configure → build → 模型测试 → GUI 测试 → check-boundaries → package → 上传日志/CTest XML/边界报告/依赖 JSON/安装 manifest；模型与 GUI 分离 job；缓存白名单仅构建依赖与包下载；失败上传工件；集成分支保护；yml 中每个 script 行与本地脚本命令逐字符一致（引用 `configure.ps1/build.ps1/run-tests.ps1/check-boundaries.ps1/package.ps1` 同参数）。
- **实施步骤：**
  1. 先写"RED 测试"断言：本地预演中注入一步非零（如向 configure.ps1 临时传非法 SourceDirectory），确认后续步骤全部阻断。
  2. 实现 `package.ps1`：Release 配置打包＋安装 manifest（内容清单不含测试数据/私有头/绝对路径）。
  3. 编写 `industrial-robot-windows.yml`：固定步骤顺序、模型/GUI 分离 job、缓存白名单、失败工件与分支保护。
  4. 逐行对照 yml script 与本地可执行命令，形成"CI↔本机命令一致性表"。
  5. 本地按 yml 顺序复跑全部命令（脚本形式＋原生回退）并保存日志。
  6. 在 Windows Runner 触发一次流水线，保存 job 日志与工件清单作为证据。
- **RED 测试：** `t04-fail-blocks`：令任一前置步骤退出非零（临时注入），Then 同 job 后续步骤不执行且依赖该 job 的后续 job 全部阻断、失败工件仍上传——流水线未接入前以本地顺序执行复现（前一步非零则链条停止）。
- **最小实现：** 仅创建 yml 与 `package.ps1`；不改 T01/T03 脚本签名，不新增缓存内容类别，不引入 Runner 专属参数。
- **正常/边界/失败测试：**
  - 正常：Given 全链脚本通过，When 本地按 yml 顺序复跑，Then configure/build/模型测试/check-boundaries/package 全部退出码 0。
  - 边界：Given 缓存策略，When 审查 yml，Then 缓存路径仅构建依赖与包下载，不含测试结果/结果数据库/项目快照。
  - 失败：Given 注入的步骤失败，When 流水线执行，Then 后续 job 阻断、日志与失败工件上传、集成分支不接受合并。
- **精确验证命令：**（仓库根目录、VS x64 环境；T03 已交付入口，脚本＋原生双形式）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_.*_test$'`；预期退出码 0（CI 模型测试 job 同命令）。
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1`；预期退出码 0。
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\package.ps1 -Configuration Release`；预期退出码 0 且生成安装 manifest。
  - 原生回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_core_test$"`；预期与脚本形式结果一致。
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `RobWork/gitlab-ci/industrial-robot-windows.yml` 与 `RobWork/scripts/industrial-robot/package.ps1`；既有脚本零变化；yml 无 offscreen、无并行 GUI、缓存项逐一对照白名单；安装 manifest 无测试数据/私有头/绝对构建路径。
- **证据工件：** `out/logs/industrial-robot/<timestamp>/package.log`、CI↔本机命令一致性表、Runner 流水线 job 日志与工件清单、`t04-fail-blocks` 阻断记录。
- **提交格式：** `WP-01-T04: GitLab Windows 门禁`
- **停止与升级条件：** Runner 无 Windows x64 执行机、yml 步骤与本地脚本无法逐字符一致，或打包需要引入未批准依赖时，停止并升级给集成负责人；yml 编写者不得同时担任 WP-01-T05 依赖评审者。
