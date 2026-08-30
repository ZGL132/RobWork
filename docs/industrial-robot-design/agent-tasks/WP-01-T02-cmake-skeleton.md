# WP-01-T02 建立目标骨架

- **Task ID / 需求 ID / ADR / 阶段：** WP-01-T02；ARC-02（主）、NFR-MNT-01（领域内核不依赖 Qt Widgets、模型测试可直接调用）、NFR-MNT-02（插件无 Widget 头互依）；无直接关联 ADR；阶段 A 前提 / R1。契约：`architecture/public-interfaces.md`、`architecture/testing-contract.md`。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-01-T01；工件：`RobWork/scripts/industrial-robot/check-boundaries.ps1`（四类违规扫描可用）与 `out/logs/industrial-robot/<timestamp>/boundary-fixtures.log`、`boundary-clean-tree.log`。
- **允许创建/修改/删除的文件：**
  - 修改：`RobWork/RobWorkStudio/src/rwslibs/CMakeLists.txt`（仅新增 `add_subdirectory(industrialrobot)` 一行）
  - 创建：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/CMakeLists.txt`、`industrialrobot/cmake/IndustrialRobotTargets.cmake`（模块目标工厂、依赖断言、include 安装白名单）
  - 创建 10 个模块骨架：`industrialrobot/{core,project,evidence,runtime,policy,execution,diagnostics,io,reporting,ui}/src/<module>_anchor.cpp`（仅含模块命名空间锚点常量，供链接与依赖断言）与 `include/sdurws/ird/<module>/` 目录
  - 创建测试骨架：`industrialrobot/tests/<module>/<module>_smoke.cpp`（链接对应模块目标、仅验证链接与注册，返回 0）
- **禁止修改的文件和公共接口：** 旧插件任何 CMake 与源码（除上述一行 `add_subdirectory` 外不得改变 `rwslibs/CMakeLists.txt` 既有内容）；`requirements.md`、CSV、文档门禁脚本、`scripts/industrial-robot/` 既有脚本；不创建任何领域头文件或业务实现。
- **修改前接口：** `rwslibs/CMakeLists.txt` 无 industrialrobot 子目录；不存在任何 `sdurws_ird_*` 目标。
- **修改后接口：** 新增 10 个目标 `sdurws_ird_{core,project,evidence,runtime,policy,execution,diagnostics,io,reporting,ui}`（类型/允许依赖按 WP-01 计划 §2.2 表）与 CTest 目标 `sdurws_ird_<module>_test`；12 个选项 `IRD_BUILD_CORE/PROJECT/EVIDENCE/RUNTIME/POLICY/EXECUTION/DIAGNOSTICS/IO/REPORTING/UI/BUSINESS_PLUGINS`（默认值按 §4.1 表，`IRD_BUILD_UI` 随 `WITH_RWS`，`IRD_BUILD_BUSINESS_PLUGINS=OFF`）；关闭选项必须跳过全部依赖目标或配置失败，不允许半目标。
- **实施步骤：**
  1. 先执行"RED 测试"原生构建断言，记录基线失败（目标不存在）。
  2. 在 `rwslibs/CMakeLists.txt` 追加一行 `add_subdirectory(industrialrobot)`。
  3. 编写 `IndustrialRobotTargets.cmake`：模块目标工厂＋单向依赖断言（只允许 §2.2 表依赖，反向依赖在配置期失败）＋include 安装白名单（公共头含 `include/sdurws/ird/<module>/`，不含私有头/测试数据/绝对构建路径）。
  4. 编写顶层 `industrialrobot/CMakeLists.txt`：12 个选项、10 个模块目标与锚点源文件、`tests/<module>` 独立 CTest 注册（`BUILD_TESTING=ON` 时）。
  5. 按验证命令完成默认配置、单目标构建与 CTest；再验证选项关闭路径无半目标。
  6. 运行 `check-boundaries.ps1` 确认新骨架零违规。
- **RED 测试：** 先写的失败断言（原生命令，仓库根目录）：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_core` 失败（目标不存在）且 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_core_test$"` 无匹配测试（非零）——骨架落地后两条必须转绿。
- **最小实现：** 仅创建上述 CMake 文件、锚点源文件与 smoke 测试；不实现任何领域头文件、公共 API 或业务逻辑；选项默认值外的行为不添加。
- **正常/边界/失败测试：**
  - 正常：Given §4.2 默认配置，When 构建 `sdurws_ird_core` 并运行其 CTest，Then 构建成功、`sdurws_ird_core_test` 通过。
  - 边界（2026-08-31 治理裁决修订）：Given `IRD_BUILD_UI=OFF`（`WITH_RWS=ON`），When 配置构建，Then UI 被跳过、其余 9 个模块目标与测试正常生成，无半目标。Given `WITH_RWS=OFF`，When 配置，Then `industrialrobot` 随 RobWorkStudio（§3 布局中其父目录）一并跳过：IRD 目标生成数为 0、零半目标、配置收敛；`sdurws_ird_core` 独立构建的要求由默认路径（`WITH_RWS=ON`）覆盖。结构背景：顶层 `RobWork/CMakeLists.txt` 以 `if(WITH_RWS)` 门控 `add_subdirectory(RobWorkStudio)`，`industrialrobot` 位于其内，使 IRD 全树随 `WITH_RWS=OFF` 跳过；使 core 脱离该门控的顶层 CMake 调整超出本卡文件授权，如需独立构建须由 WP-01 所有者另立治理任务。
  - 失败：Given 手工注入反向依赖（如让 `sdurws_ird_core` 链接 `sdurws_ird_ui`），When 配置，Then 配置期失败并指出违规目标对。选项冲突检查 `IRD_BUILD_UI=ON requires WITH_RWS=ON` 保留为配置期保护；在当前 `WITH_RWS` 门控下该分支不可达（树被跳过），不作为可演示失败用例。
- **精确验证命令：**（仓库根目录、VS x64 环境；本任务早于统一测试入口，只用原生形式）
  - `cmake -S RobWork -B out\build\industrial-robot -G "Visual Studio 17 2022" -A x64 -DWITH_RWS=ON -DWITH_RWSIM=ON -DBUILD_TESTING=ON -DIRD_BUILD_BUSINESS_PLUGINS=OFF`；预期配置成功、无半目标告警。
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_core`；预期构建成功。
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_core_test$"`；预期 1/1 通过。
  - 选项关闭路径（2026-08-31 治理裁决修订）：`cmake -S RobWork -B out\build\industrial-robot-optoff -G "Visual Studio 17 2022" -A x64 -DWITH_RWS=OFF -DBUILD_TESTING=ON -DIRD_BUILD_UI=OFF -DIRD_BUILD_BUSINESS_PLUGINS=OFF`；预期配置收敛（退出码 0）、`industrialrobot` 随 Studio 跳过（`sdurws_ird_*.vcxproj` 生成数为 0，零半目标）；`sdurws_ird_core` 的单目标构建由默认路径验证覆盖。
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1`；预期退出码 0。
- **diff 和禁止项检查：** `git diff --name-only` 中 `rwslibs/CMakeLists.txt` 的变更仅一行 `add_subdirectory(industrialrobot)`；新增文件全部位于 `industrialrobot/` 内；旧插件目标属性零变化；公共头目录内不出现 QWidget/QApplication 包含。
- **证据工件：** `out/logs/industrial-robot/<timestamp>/configure-default.log`、`build-core.log`、`ctest-core.log`、`configure-optoff.log`、`boundary-clean-tree.log`（含目标清单 `cmake --build out\build\industrial-robot --config Debug --target help` 摘录与依赖图）。
- **提交格式：** `WP-01-T02: 建立目标骨架`
- **停止与升级条件：** 目标依赖图与 §2.2 表冲突、`WITH_RWS=OFF` 时顶层配置无法收敛，或必须修改旧插件 CMake 才能配置成功时，停止并升级给工作包所有者；本任务实现者不得同时担任 WP-01-T03 验证者。
