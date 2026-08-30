# WP-01-T03 统一测试入口 · 独立验证报告

- Task ID：WP-01-T03；阶段 A 前提 / R1；需求锚点：NFR-MNT-01、NFR-DEP-01
- 验证者：独立验证者/治理协调（ZCode 治理会话，与实施者不同执行上下文）
- 验证日期：2026-08-31；验证运行 ID：`20260831-c161d6d-verify`
- 实现提交：`c161d6d27b362027ea9dbb7d2219b51504561f1a`（`WP-01-T03: 统一测试入口`，父提交 `1602c07`＝main 当时 HEAD）
- 验证上下文：新建隔离 detached worktree `D:\10_Source_Repos\21_robot\wt-wp01-t03-verify` @ c161d6d（本验证结束后删除）；实现者 worktree `wt-wp01-t03` 未复用
- 实现者证据：`out/logs/industrial-robot/20260831-062118/`（主树副本与 worktree 原件经 `diff -rq` 全等核对，含 red/green/failpath/boundary/native 分目录与 `red-green-summary.md` 前后结果表）

## 1. 验证环境

- Windows 11 专业版（NT 10.0.26200）；Visual Studio 2022 Community 17.12（脚本内 vswhere→`VsDevCmd.bat -arch=x64 -host_arch=x64` 自动发现，MSVC 19.42.34433）；Windows SDK 10.0.22621.0；CMake/CTest 4.3.1；Windows PowerShell 5.1.26100.9168（文档门禁另以 pwsh 7 复跑）
- 操作员继承环境（脚本仅记录、不设置）：`CMAKE_PREFIX_PATH=D:/software/QT/6.11.1/msvc2022_64;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows`（与 WP-01-T02 独立验证记录一致；无该前缀时全树 Boost 查找失败，属环境前置非代码缺陷）
- 环境桥接（磁盘文件，非代码修改，均记录于本目录）：① 新 worktree 缺 gitignored 的 `RobWorkStudio.ini.template.static`／`RobWorkStudio.ini.shared.in`（`.gitignore` 规则 `[Bb]in/` 所致，T02 配置同样依赖主树未跟踪副本），自主树复制并记录 SHA-256（`t03-env-bridge-templates.sha256`）；② t03-no-vs 断言用本验证自编译 stub `vswhere.exe`（`vswhere.cs`，空输出模拟无 VS 实例机器），未复用实现者的 stub 二进制

## 2. 卡内精确验证命令复跑（worktree 根目录）

| # | 命令 | 退出码 | 结果 |
| --- | --- | --- | --- |
| 1 | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File ./RobWork/scripts/industrial-robot/configure.ps1 -Configuration Debug` | 0 | 全树配置成功；§4.2 固定选项、生成器/平台、MSVC/SDK/CMake/CTest 版本与 CMAKE_PREFIX_PATH 记录入 `configure.log`（副本 `run-logs/20260831-070644/`） |
| 2 | `…\build.ps1 -Configuration Debug` | 0 | `sdurws_ird_core.lib` 与 `sdurws_ird_core_test.exe` 生成（`out/build/industrial-robot/RobWorkStudio/{libs,bin}/Debug/`） |
| 3 | `…\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_core_test$'` | 0 | `ctest -N` 预检 Total Tests=1 → `1/1 Test #80: sdurws_ird_core_test … Passed`、`100% tests passed`；`QT_QPA_PLATFORM=windows` 强制与 `-j 1` 单进程记录入 `test.log`（副本 `run-logs/20260831-070826/`） |
| 4a | 原生回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_core`（VsDevCmd x64 包装器内逐字执行，包装器 `t03-verify-native-build.cmd`） | 0 | 与脚本形式一致 |
| 4b | 原生回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_core_test$"`（同上，`t03-verify-native-ctest.cmd`） | 0 | 1/1 Passed，与脚本形式一致 |

控制台捕获：`t03-green-{configure,build,runtests}.console.log`、`t03-native-{build-core,ctest}.console.log`。

## 3. 四条失败断言复跑（实现后应非零且诊断稳定）

| 断言 | 复跑方法 | 退出码 | 关键观测 |
| --- | --- | --- | --- |
| t03-no-source | `configure.ps1 -SourceDirectory D:\__t03_verify_no_such_source__` | 1 | `[入口参数] -SourceDirectory 目录不存在或不可读: D:\__t03_verify_no_such_source__`；诊断先于任何目录创建——`out/build` 未被创建（新 worktree 实证），亦无删除行为 |
| t03-no-vs | PATH 仅含 stub vswhere＋System32（无 cl.exe），stub 返回零实例 | 1 | `VS x64 环境发现失败：vswhere 未发现含 x64 工具集的 Visual Studio 实例；不回退 x86/MinGW。`；日志保留（`run-logs/20260831-070633/`），无任何回退/构建尝试 |
| t03-regex-miss | `run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_nonexistent_test$'` | 1 | ctest -N `Total Tests: 0` → `[失败] Regex 零匹配：-R "^sdurws_ird_nonexistent_test$" 未匹配任何已注册测试（共 0 项）。`；test.log 保留 |
| t03-offscreen-conflict | `QT_QPA_PLATFORM=offscreen` 继承后运行 `run-tests.ps1` | 1 | `[GUI 规则] 冲突变量: QT_QPA_PLATFORM=offscreen`＋"已先报告并停止"；脚本日志止于失败诊断，ctest 未执行 |

控制台捕获：`t03-fail-{no-source,no-vs,regex-miss,offscreen}.console.log`。实现者 RED 前证据（`20260831-062118/red/`）显示实现前四断言以"入口不存在"失败（退出码 127），因果成立。

## 4. 边界与静态检查

| 检查 | 结果 |
| --- | --- |
| `build.ps1 -Configuration Debug -NoConfigure` | 退出码 0；build.log 含"跳过配置步骤，直接构建"，无任何 `cmake 配置` 命令行（环境发现＋两目标构建为全部命令），副本 `run-logs/20260831-071011/` |
| Debug/Release 日志分离 | `configure.ps1 -Configuration Release` 退出码 0；本验证 8 次脚本调用生成 8 个互异 `yyyyMMdd-HHmmss` 目录（070633/070644/070723/070826/070841/070844/071011/071016），互不覆盖 |
| `check-boundaries.ps1` | 退出码 0：`Boundary scan passed: 32 files scanned` |
| 提交范围 | `git diff --name-only 1602c07..c161d6d` 仅含允许的 4 个脚本；`industrialrobot/`、旧插件、需求/CSV/门禁脚本零变化 |
| common.ps1 接口保持 | numstat 183 行新增、0 删除——T01 既有三函数（Get-IndustrialRobotRepoRoot/New-IndustrialRobotLogDir/Write-IndustrialRobotLog）逐字未动 |
| `git show --check c161d6d` | 干净（退出码 0）；提交标题与卡内规定逐字一致，正文分条以"新增/修改/测试/证据"起始 |
| 禁词扫描（4 脚本） | `offscreen`／`Start-Job`／`Start-ThreadJob`／`-Parallel` 零命中（offscreen 仅作为"检测继承冲突"的对象出现在行为语义中，无设置虚拟平台字面量）——扫描记录 `t03-commit-scope-scan.txt` |
| PS 7 专属语法 | 四脚本经 Windows PowerShell 5.1 解析器 `Parser::ParseFile` 全部零错误（PS7 专属语法将解析失败）——`t03-ps51-parse-check.txt` |

## 5. 观察项（非阻断）

1. **CMAKE_PREFIX_PATH 固化建议仍开放**：T02 独立验证观察建议"全树配置对环境前缀的依赖由 T03 入口脚本固化"。T03 卡与 WP-01 计划 §5.1/§5.2 的冻结契约均不含该职责，实现者选择"仅记录不设置"并明示"CMAKE_PREFIX_PATH 等仅记录不设置"，符合权威顺序（未自行扩权）。该建议未被任何已 Accepted 文档吸收，移交工作包所有者裁决（候选载体：WP-01 计划修订或后续卡）。
2. **新 worktree 环境桥接**：gitignored 的 `RobWorkStudio.ini` 双模板使全新 worktree 首次配置失败于 `RobWorkStudio/src/CMakeLists.txt:112`，需自主树复制（本验证与实现者同法）。属历史仓库遗留，建议后续治理在 README/交接材料中固化说明。
3. **证据根口径**：实现证据位于 `out/logs/industrial-robot/<timestamp>/`（沿 T01/T02 先例），与 AGENTS §5.3／账本 Done 规则的 `out/test-evidence/wp-xx/<run-id>/` 口径不一致；本验证按规范根出具独立复跑记录桥接（沿用 T01 观察处置）。
4. 实现者中间原始运行目录（062950 等）仅保留于其 worktree 未入汇总目录；汇总目录内"证据文件索引"已注明来源映射，本验证复核关键日志（green/failpath）与自跑结果一致，足以支撑证据链。

## 6. 独立验证结论

卡内全部必执行命令、四条失败断言、边界测试、边界扫描与范围/格式/禁项检查全部通过；实现者证据与本验证独立复跑结果一致。**独立验证通过，签署 WP-01-T03 `Done`。** 下一任务 WP-01-T04 门禁判定另见任务状态账本。
