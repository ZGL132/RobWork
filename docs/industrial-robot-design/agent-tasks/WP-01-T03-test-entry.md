# WP-01-T03 统一测试入口

- **Task ID / 需求 ID / ADR / 阶段：** WP-01-T03；NFR-MNT-01（计算内核可由模型测试直接调用）、NFR-DEP-01（Windows x64 正式验收）；无直接关联 ADR；阶段 A 前提 / R1。契约：`architecture/testing-contract.md`、`architecture/public-interfaces.md`。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-01-T02；工件：可配置构建的 `out\build\industrial-robot`（含 `sdurws_ird_core` 目标与 `sdurws_ird_core_test` CTest 注册）与 `industrialrobot/CMakeLists.txt` 选项集。
- **允许创建/修改/删除的文件：**
  - 创建：`RobWork/scripts/industrial-robot/configure.ps1`、`build.ps1`、`run-tests.ps1`
  - 修改：`RobWork/scripts/industrial-robot/common.ps1`（追加参数/环境/日志函数，不改 T01 既有函数签名）
  - 写运行日志：`out/logs/industrial-robot/<timestamp>/`（原始日志）；证据工件登记于 `out/test-evidence/wp-01/<run-id>/`（2026-08-31 所有者修订，两级口径见 WP-01 计划 §5.4）
- **禁止修改的文件和公共接口：** `industrialrobot/` 内任何 CMake 与源文件；旧插件；`requirements.md`、CSV、文档门禁脚本；脚本不得自动删除既有构建目录、自动修复源码或环境变量。
- **修改前接口：** `common.ps1` 仅有 T01 交付的路径解析与日志函数；configure/build/run-tests 三个入口不存在。
- **修改后接口：** 三入口共用参数（WP-01 计划 §5.1）：`Configuration`（Debug/Release）、`BuildDirectory`（缺省 `out/build/industrial-robot`）、`SourceDirectory`、`Generator`、`Platform`、`NoConfigure`、`LogDirectory`（缺省 `out/logs/industrial-robot/<timestamp>`），`run-tests.ps1` 另有 `-Regex`；行为契约：仓库根解析绝对 `-S/-B` → VS x64 环境发现（cl/cmake/ctest 检查、vswhere→VsDevCmd `-arch=x64 -host_arch=x64`、找不到即失败不回退 x86/MinGW）→ configure/build/CTest 日志 → 退出码透传；GUI 路径强制 `QT_QPA_PLATFORM=windows`、单进程、绝对路径，检测到继承 `QT_*`/`QML_*` 冲突先报告并停止。
- **实施步骤：**
  1. 先执行"RED 测试"四条断言，记录入口缺失时的失败。
  2. 在 `common.ps1` 实现参数、`Resolve-Path` 校验、日志目录创建与版本记录（MSVC、Windows SDK、CMake、CTest）。
  3. 实现 `configure.ps1`：§4.2 固定配置命令＋关键选项写入构建证据。
  4. 实现 `build.ps1`：按 Configuration 构建目标，透传失败并保留日志。
  5. 实现 `run-tests.ps1`：CTest 调用、默认 `-j1`、Regex 精确筛选、GUI 规则（QT_QPA_PLATFORM、单进程、冲突变量检测）。
  6. 按验证命令分别验证模型路径与 GUI 规则失败路径，写证据。
- **RED 测试：** 先写的失败断言（入口未实现时均失败）：`t03-no-source`：`configure.ps1 -SourceDirectory <不存在路径>` 必须非零并输出路径诊断；`t03-no-vs`：PATH 无 cl.exe 时必须非零且不回退；`t03-regex-miss`：`run-tests.ps1 -Regex '^sdurws_ird_nonexistent_test$'` 必须非零并报告零匹配；`t03-offscreen-conflict`：预置继承 `QT_QPA_PLATFORM=offscreen` 后运行 `run-tests.ps1` 必须先报告并停止（非零）。
- **最小实现：** 仅实现三入口与 `common.ps1` 扩展，使上述四条断言与模型路径链路（configure→build→run-tests `^sdurws_ird_core_test$` 通过）成立；打包脚本不在本任务范围（WP-01-T04）。
- **正常/边界/失败测试：**
  - 正常：Given T02 构建目录，When 依次运行 configure/build/run-tests（`-Regex '^sdurws_ird_core_test$'`），Then 三步退出码 0、`sdurws_ird_core_test` 通过、日志落盘。
  - 边界：Given `-NoConfigure`，When 运行 build，Then 跳过配置直接构建；Given Debug/Release 两配置，Then 日志目录按时间戳分离。
  - 失败：Given VS x64 缺失、源目录缺失、Regex 零匹配或继承 offscreen 冲突变量，When 运行对应入口，Then 非零、稳定诊断、日志保留、既有构建目录不被删除。
- **精确验证命令：**（仓库根目录、VS x64 环境；本任务交付测试入口后，脚本与原生双形式并用）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\configure.ps1 -Configuration Debug`；预期退出码 0。
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\build.ps1 -Configuration Debug`；预期退出码 0。
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_core_test$'`；预期退出码 0、1/1 通过。
  - 原生回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_core` 与 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_core_test$"`；预期与脚本形式结果一致。
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `RobWork/scripts/industrial-robot/` 四个脚本；`industrialrobot/` 与旧插件零变化；脚本不含 PowerShell 7 专属语法（除非显式声明前置）、不含 offscreen 设置或并行 GUI 启动。
- **证据工件：** `out/test-evidence/wp-01/<run-id>/configure.log、build.log、test.log`（含命令行、退出码、环境版本记录与 `CMAKE_PREFIX_PATH` 实际取值）与四条 RED 断言前后结果表；原始脚本日志按脚本契约落盘 `out/logs/industrial-robot/<timestamp>/` 并复制入证据根。2026-08-31 所有者修订登记根，两级口径见 WP-01 计划 §5.4；已交付证据按账本记录保留原位置，效力不变。
- **提交格式：** `WP-01-T03: 统一测试入口`
- **停止与升级条件：** VS x64 环境发现、Qt 平台规则或参数契约无法从 WP-01 计划 §5/§6 推导，或必须修改 CMake 目标才能跑通测试时，停止并升级给工作包所有者；脚本实现者不得同时担任 WP-01-T04 验证者。
