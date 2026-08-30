# WP-01-T03 统一测试入口·验证证据汇总

- Task ID：WP-01-T03（统一测试入口）；阶段 A 前提 / R1；需求锚点：NFR-MNT-01、NFR-DEP-01
- 实现分支 / worktree：`codex/wp-01-t03-test-entry` @ `D:\10_Source_Repos\21_robot\wt-wp01-t03`
- 基线：main `1602c07`（含 WP-01-T02 实现 `2443d99` 与治理修订 `2287b55`）；开工时工作树与提交零差异
- 运行日期：2026-08-31；运行目录：`out/logs/industrial-robot/20260831-062118/`
- 环境：Windows 11 专业版（NT 10.0.26200）；Visual Studio 2022 Community 17.12（VsDevCmd `-arch=x64 -host_arch=x64` 导入，MSVC `19.42.34433`）；Windows SDK `10.0.22621.0`；CMake/CTest `4.3.1`；PowerShell 5.1（Windows PowerShell）
- 操作员环境（脚本仅记录、不设置）：`CMAKE_PREFIX_PATH=D:/software/QT/6.11.1/msvc2022_64;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows`——与 WP-01-T02 独立验证记录一致（无该前缀时全树 Boost 查找失败，属环境前置非代码缺陷）

## 0. 环境桥接记录（机器本地 gitignore 前置，非代码修改）

- 首次全树配置失败于 `RobWorkStudio/src/CMakeLists.txt:112`：`RobWorkStudio.ini.template.static` 不存在（`.gitignore` 规则 `[Bb]in/` 使该遗留模板不入库；主工作区以未跟踪文件存在，T02 配置即依赖它）。处理：从主工作区复制 `RobWorkStudio.ini.template.static` 与 `RobWorkStudio.ini.shared.in` 两个 gitignored 模板到 worktree 同路径（磁盘文件，不进提交、不改任何跟踪内容）；证据：`green/configure-first-attempt-environment-gap.log`（失败）→ `green/configure.log`（成功）。
- t03-no-vs 失败路径使用 PATH 前置的 stub `vswhere.exe`（`out/t03-stub-vswhere/`，csc 编译的空实现，模拟无 VS 实例的机器）；真实机器装有 VS 2022，故以测试双胞胎模拟"vswhere 无实例"环境条件，脚本与扫描器不受影响。

## 1. 四条 RED 断言——实现前（入口文件不存在，断言均不满足）

| 断言 | 命令（worktree 根） | 观测结果 | 判定 |
| --- | --- | --- | --- |
| t03-no-source | `configure.ps1 -SourceDirectory D:\__t03_no_such_source__` | 退出码非零（127）；报错为"-File 参数不存在"，无源目录路径诊断 | ❌ 未满足（无任务语义） |
| t03-no-vs | PATH 仅 System32 后运行 `configure.ps1` | 退出码非零（127）；无 VS x64 发现逻辑与不回退诊断 | ❌ 未满足 |
| t03-regex-miss | `run-tests.ps1 -Regex '^sdurws_ird_nonexistent_test$'` | 退出码非零（127）；入口缺失，无零匹配报告 | ❌ 未满足 |
| t03-offscreen-conflict | 预置 `QT_QPA_PLATFORM=offscreen` 后运行 `run-tests.ps1` | 退出码非零（127）；入口缺失，无冲突检测 | ❌ 未满足 |

证据：`red/t03-*.log`（控制台捕获，含退出码行）。RED 与实现的因果关系：四条断言在仅存在 T01 交付物（`common.ps1`、`check-boundaries.ps1`）的干净 1602c07 树上全部失败，失败形态为"入口不存在"。

## 2. 四条断言——实现后（失败路径按预期非零停止）

| 断言 | 命令 | 退出码 | 关键诊断（脚本自写日志，UTF-8） |
| --- | --- | --- | --- |
| t03-no-source | `configure.ps1 -SourceDirectory D:\__t03_no_such_source__` | 1 | `[入口参数] -SourceDirectory 目录不存在或不可读: D:\__t03_no_such_source__`；先于任何目录创建，构建目录未被创建/删除 |
| t03-no-vs | PATH=stub vswhere+System32，`configure.ps1 -Configuration Debug` | 1 | `VS x64 环境发现失败：vswhere 未发现含 x64 工具集的 Visual Studio 实例；不回退 x86/MinGW。` |
| t03-regex-miss | `run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_nonexistent_test$'` | 1 | `[失败] Regex 零匹配：-R "^sdurws_ird_nonexistent_test$" 未匹配任何已注册测试（共 0 项）。`；test.log 保留 |
| t03-offscreen-conflict | 预置 `QT_QPA_PLATFORM=offscreen`，`run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_core_test$'` | 1 | `[GUI 规则] 冲突变量: QT_QPA_PLATFORM=offscreen`＋"已先报告并停止"；test.log 保留，未执行任何 ctest |

证据：`failpath/t03-*.log`（控制台捕获，GBK 控制台编码）＋ `failpath/t03-*.script.log`/`t03-*.test.log`（脚本自写日志，UTF-8）。

## 3. 正常链路（卡内逐字命令，worktree 根、VS x64 环境）

| # | 命令 | 退出码 | 结果 |
| --- | --- | --- | --- |
| 1 | `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\configure.ps1 -Configuration Debug` | 0 | 全树配置成功；`§4.2` 固定选项、生成器/平台、MSVC/SDK/CMake/CTest 版本与 `CMAKE_PREFIX_PATH` 记录入 `configure.log` |
| 2 | `…\build.ps1 -Configuration Debug` | 0 | `sdurws_ird_core.lib` 与 `sdurws_ird_core_test.exe` 生成；输出与退出码入 `build.log` |
| 3 | `…\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_core_test$'` | 0 | `ctest -N` 预检 Total Tests=1 → `1/1 Test #80: sdurws_ird_core_test … Passed`，`100% tests passed`；`QT_QPA_PLATFORM=windows` 强制与 `-j 1` 单进程记录入 `test.log` |
| 4a | 原生回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_core`（VsDevCmd 包装器内逐字执行） | 0 | 与脚本形式一致；包装器与输出见 `native/native-build-core.{cmd,log}` |
| 4b | 原生回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_core_test$"`（同上） | 0 | 1/1 通过，与脚本形式一致；见 `native/native-ctest-core.{cmd,log}` |

证据：`green/{configure,build,test}.log`（脚本自写日志，含命令行、退出码、环境版本记录）。

## 4. 边界与范围检查

| 检查 | 命令/方法 | 退出码 | 结果 |
| --- | --- | --- | --- |
| -NoConfigure 跳过配置 | `build.ps1 -Configuration Debug -NoConfigure` | 0 | `build.log` 记录"-NoConfigure：跳过配置步骤，直接构建"，未出现配置步骤，两目标直接构建 |
| Debug/Release 日志分离 | 默认 `LogDirectory` 下先后运行 Debug 链与 `configure.ps1 -Configuration Release` | 0 | 各次调用生成独立 `yyyyMMdd-HHmmss` 目录（063810 Debug configure、063836 build、063903 test、063906、063909 Release、063935、064024），互不覆盖 |
| 边界扫描 | `check-boundaries.ps1` | 0 | `Boundary scan passed: 32 files scanned`（R7 已扫描三个新入口脚本：无虚拟平台字面量、无 GUI 并行；本汇总以 `grep -rlE "offscreen|Start-Job|Start-ThreadJob|-Parallel"` 复核为空） |
| 允许文件清单 | `git status --porcelain` | - | 仅 `M common.ps1`＋新增 `configure.ps1`/`build.ps1`/`run-tests.ps1`；`industrialrobot/` 与旧插件零变化 |
| git diff --check | `git diff --check` | 0 | 干净（无空白错误） |

## 5. 证据文件索引与来源对照

| 文件（run 目录内） | 来源 |
| --- | --- |
| `red/t03-*.log` | RED 四条断言控制台捕获（实现前） |
| `green/configure-first-attempt-environment-gap.log` | 原始目录 `20260831-062950`（首次配置失败，环境桥接记录） |
| `green/configure.log`、`green/build.log`、`green/test.log` | 原始目录 `20260831-063810`/`063836`/`063903`（MSVC 版本记录修复后重跑的最终链路） |
| `failpath/t03-no-source.log` | 实现后失败路径控制台捕获 |
| `failpath/t03-no-vs.log`＋`t03-no-vs.script.log` | 同上；脚本日志来自原始目录 `20260831-062939` |
| `failpath/t03-regex-miss.log`＋`t03-regex-miss.test.log` | 同上；脚本日志来自原始目录 `20260831-064024` |
| `failpath/t03-offscreen-conflict.log`＋`t03-offscreen-conflict.test.log` | 同上；脚本日志来自原始目录 `20260831-063538` |
| `boundary/build-noconfigure.script.log` | 原始目录 `20260831-063906` |
| `boundary/configure-release.script.log` | 原始目录 `20260831-063909` |
| `native/native-build-core.{cmd,log}`、`native/native-ctest-core.{cmd,log}` | 原生回退对照（VsDevCmd 包装器逐字执行） |

原始默认时间戳目录按契约保留于 `out/logs/industrial-robot/`（062118/062939/062950/063538/063810/063836/063903/063906/063909/063935/064024）；MSVC 版本记录编码修复（`-match 'Version'` → 首条非空行）前的中间运行目录（063127/063204/063312/063442/063457/063535 及旧控制台捕获）已被最终运行取代并清理，行为结论不变。

## 6. 实现范围与提交

- 实现提交：`c161d6d27b362027ea9dbb7d2219b51504561f1a`（`WP-01-T03: 统一测试入口`，父提交 `1602c07`），仅含四个脚本：`RobWork/scripts/industrial-robot/{common.ps1(追加),configure.ps1,build.ps1,run-tests.ps1}`；运行日志、构建目录与 stub 均不入提交。
- MSVC 版本记录编码修复（`-match 'Version'` → 首条非空行）落地后，已于最终提交状态复跑：`check-boundaries.ps1` 退出码 0（32 文件）、`git show --check` 退出码 0、工作树脚本与 HEAD 零差异、禁词扫描干净。
- `common.ps1` 追加 7 个辅助函数（时间戳、输入目录校验、输出目录创建、vswhere 查找、原生日志执行、VS x64 环境初始化、Qt 冲突检测），WP-01-T01 既有函数及签名零改动（diff 496 行新增、0 删除）。
- `run-tests.ps1` 的 `-Regex` 为必填参数；`build.ps1` 固定构建 `sdurws_ird_core`＋`sdurws_ird_core_test`（任务卡模型测试链路目标集，与 T02 已验证可构建集合一致）；GUI 等其余模块目标属后续任务卡。
