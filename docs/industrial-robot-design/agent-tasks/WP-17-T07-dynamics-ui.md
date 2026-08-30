# WP-17-T07 动力学工作台界面

- **Task ID / 需求 ID / ADR / 阶段：** WP-17-T07；DYN-01～08、UX-01～08、NFR-PERF-03；阶段 C / R1。契约：`architecture/testing-contract.md` §3～§5、`architecture/public-interfaces.md` §7；模块详设 `module-design/dynamics.md` v0.4 §8.1～§8.6、`module-design/session-ui.md` v0.4 §8。
- **基线 commit：** 代码基线 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；文档语义源 `dynamics.md` v0.4。
- **前置任务及必需工件：** WP-17-T03（`DynamicResult`）、WP-17-T05（数据不足语义）、WP-17-T06（传动映射契约）、WP-10-T06（工作台外壳）、WP-01-T03（测试入口）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/dynamics/`）创建/修改 `gui/` 下关节包络、曲线、统计口径、可信度、设置和状态面板；创建 `test/DynamicsGuiTest.cpp`；修改本插件 `CMakeLists.txt` 仅登记 GUI 文件与 `sdurws_ird_dynamics_gui_test`；创建 `out/test-evidence/wp-17/<run-id>/`。禁止删除计算核心文件。
- **禁止修改的文件和公共接口：** `src/`、`DynamicResult` 公共头、WP-18 传动实现、WP-10 外壳、其他插件、架构、Schema 和黄金数据；禁止 GUI 自行换算或补齐缺失动力学量。
- **修改前接口：** 动力学结果与缺失数据诊断可用，但没有 §8 规定的关节包络、曲线、统计口径和可信度界面。
- **修改后接口：** GUI 只读投影 `DynamicResult`，显示关节包络、曲线、峰值/RMS/能量统计、口径和可信度，并经调度端口提交、取消或重试校核。
- **实施步骤：** 1) 写字段、单位、可信度和按钮 RED；2) 实现包络表与曲线模型；3) 接入统计口径、设置和状态；4) 接入数据不足和失败态；5) 登记并运行 GUI 目标。
- **RED 测试：** `EnvelopeColumnsAndUnitsMatchSpecification`、`CurveAndTableUseSameSampleIdentity`、`ConfidenceExplainsPartialResult`、`RecalculateDisabledWithoutTrajectory`、`FailureDoesNotOverwriteAcceptedResult`。
- **最小实现：** §8 要求的动力学结果浏览与运行控制；不实现动力学算法、传动映射或新的统计量。
- **正常/边界/失败测试：**
  - 正常：Given 完整轨迹和 `DynamicResult`，When 选择关节，Then 表格、曲线、统计卡和三维高亮使用同一关节身份与 SI 单位。
  - 边界：Given 部分采样和 150% 缩放，When 查看结果，Then 明示口径、覆盖率与可信度，关键状态和操作保持可见。
  - 失败：Given 输入不足或求解失败，When 渲染，Then 区分“数据不足”和“计算失败”，显示建议动作，保留上次已接纳结果。
- **精确验证命令：**（仓库根、Visual Studio x64 环境；GUI 平台固定为 windows，单次只运行本目标）
  - `$env:QT_QPA_PLATFORM='windows'; powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_dynamics_gui_test$'`
  - 回退构建：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_dynamics_gui_test`
  - 回退执行：`$env:QT_QPA_PLATFORM='windows'; $testExe=(Resolve-Path '.\out\build\industrial-robot\bin\Debug\sdurws_ird_dynamics_gui_test.exe').Path; & $testExe`
- **diff 和禁止项检查：** diff 仅含本插件 `gui/`、测试、CMake 和证据；`rg -n "inverseDynamics|forwardDynamics|DriveTrain|QFile" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/dynamics/gui; if ($LASTEXITCODE -eq 0) { throw '检测到 GUI 越权实现' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中；边界脚本零违规。
- **证据工件：** `out/test-evidence/wp-17/<run-id>/dynamics-ui.md`，包含列和单位矩阵、可信度示例、三档缩放截图、正常/边界/失败日志与命令退出码。
- **提交格式：** `WP-17-T07: 新增动力学工作台界面`

  - 新增 关节包络、曲线与可信度面板
  - 新增 数据不足和失败态 GUI 测试
  - 证据 记录单位、缩放与状态投影结果
- **停止与升级条件：** 结果契约缺少 §8 必需字段、界面必须自行计算物理量或改动传动映射时停止并升级所有者；实现者不得担任独立验证者。
