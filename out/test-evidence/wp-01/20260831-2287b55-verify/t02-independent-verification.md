# WP-01-T02 独立验证记录

- Task ID：WP-01-T02（建立目标骨架）
- 验证日期：2026-08-31
- 被验实现提交：`2443d996b246192b3869c97d1ceffa5471c74d70`（WP-01-T02: 建立目标骨架）
- 结构修订提交：`2287b55a0974cb6d8eab82efb4140346a7f69c16`（build: 治理授权使 IRD 目标脱离 WITH_RWS 门控；用户 2026-08-31 选择阻塞报告方案 1，同步修订任务卡边界预期——本验证按修订后卡文执行）
- 验证所在 HEAD：`2287b55`（与结构修订提交一致；验证相关文件工作树与提交零差异）
- 前置状态：WP-01-T01 `Done`（c7c8b41）；WP-01-T02 账本状态 `Ready`
- 实现者证据：`out/logs/industrial-robot/20260830-201938/`（red-baseline.log、configure-default.log、build-core.log、ctest-core.log、configure-fail-reverse-dep.log、configure-optoff.log、boundary-clean-tree.log、target-help-excerpt.log 共 8 件，齐全）
- 验证环境：Windows 11 专业版（NT 10.0.26200）；CMake 4.3.1；Visual Studio 2022 Community 17.12（vswhere 定位于 `D:\software\Microsoft Visual Studio\2022\Community`，MSBuild 17.12.6）；`CMAKE_PREFIX_PATH=D:/software/QT/6.11.1/msvc2022_64;D:/10_Source_Repos/21_robot/RobWork/vcpkg/installed/x64-windows`（按实现者 configure-default.log 头部记录的环境复现——无该前缀时全树 Boost 查找失败，属环境差异非代码缺陷）
- 结论：**全部必执行验证通过，实现与修订后卡文、WP-01 计划 §2.2/§4.1 一致。**

## 1. 精确验证命令复跑（仓库根；Git Bash 下以正斜杠路径等价执行卡内反斜杠形式）

| 命令 | 退出码 | 结果 |
| --- | --- | --- |
| `cmake -S RobWork -B out/build/industrial-robot -G "Visual Studio 17 2022" -A x64 -DWITH_RWS=ON -DWITH_RWSIM=ON -DBUILD_TESTING=ON -DIRD_BUILD_BUSINESS_PLUGINS=OFF` | 0 | 配置成功，无半目标告警（默认全 ON，无依赖闭包关闭行） |
| `cmake --build … --config Debug --target sdurws_ird_core` | 0 | `sdurws_ird_core.lib` 生成 |
| `cmake --build … --target sdurws_ird_core_test`（CTest 可执行文件构建；实现者日志同款补充步骤） | 0 | `sdurws_ird_core_test.exe` 生成 |
| `ctest --test-dir out/build/industrial-robot -C Debug -R "^sdurws_ird_core_test$"` | 0 | **1/1 通过**（100% tests passed） |
| `cmake -S RobWork -B out/build/industrial-robot-optoff … -DWITH_RWS=OFF -DBUILD_TESTING=ON -DIRD_BUILD_UI=OFF -DIRD_BUILD_BUSINESS_PLUGINS=OFF` | 0 | 配置成功 |
| `cmake --build …optoff… --target sdurws_ird_core` | 0 | 构建成功 |
| `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1` | 0 | `Boundary scan passed: 32 files scanned` |

## 2. 目标清单核对（vcxproj 枚举，与实现者 target-help-excerpt.log 同法）

- 默认树（WITH_RWS=ON）：`sdurws_ird_{core,project,evidence,runtime,policy,execution,diagnostics,io,reporting,ui}` ＋ 各 `_test` 共 **20 个**目标，UI 在列；
- optoff 树（WITH_RWS=OFF）：**18 个**目标（9 模块＋9 测试），`sdurws_ird_ui*` 零命中，无半目标；
- 边界补验（卡内边界第一条，实现者日志未含该变体）：`WITH_RWS=ON -DIRD_BUILD_UI=OFF` 临时树配置退出码 0，**18 个**目标、`sdurws_ird_ui*` 零命中——UI 被跳过、其余 9 模块正常、无半目标。

## 3. 失败行为与 RED/GREEN 佐证复核

- RED 基线（red-baseline.log）：骨架落地前 `--target sdurws_ird_core` 与 CTest 均因目标/目录不存在退出码 1——先于实现执行，RED 成立；
- 反向依赖注入（configure-fail-reverse-dep.log）：配置期于 `ird_validate_module_links`（industrialrobot/CMakeLists.txt:162）失败，退出码 1，诊断指出违规目标对——卡内"失败"预期成立；
- ctest-core.log 如实记录中间态（仅构建 core 后 CTest `Not Run` 退出码 8，构建测试目标后 1/1 通过）——与本次复跑路径一致，非美化记录。

## 4. 结构与范围检查

- `RobWork/RobWorkStudio/src/rwslibs/CMakeLists.txt` 与 T02 实现前（c7c8b41）**零差异**：原单行注册已按治理授权移至顶层；
- 顶层 `RobWork/CMakeLists.txt` 仅 +5 行（WITH_RWS 门控外的 `add_subdirectory(RobWorkStudio/src/rwslibs/industrialrobot)` 及授权注释）；`industrialrobot/CMakeLists.txt` 以单站点注册约定防 WITH_RWS=ON 双重绑定；
- 12 选项默认值与计划 §4.1 逐项一致（`IRD_BUILD_UI` 随 `WITH_RWS`、`IRD_BUILD_BUSINESS_PLUGINS=OFF`、`UI=ON requires WITH_RWS=ON` 冲突检查保留为配置期保护）；
- `IndustrialRobotTargets.cmake` 的 §2.2 ird 依赖集、显式源收集（禁 GLOB）、include 安装白名单（仅公共头、排除 .gitkeep）、逐模块 CTest 注册与计划一致；
- `industrialrobot/` 内 QWidget/QApplication 命中 0；锚点/仅链接 smoke 与卡内"不实现领域头文件或业务逻辑"一致；
- 工作树用户既有修改（WP-24 相关等）未触碰；`git diff --check` 干净。

## 5. 治理轨迹核对与观察（非阻断）

1. **证据根口径**：实现证据在 `out/logs/industrial-robot/<timestamp>/`，与 AGENTS §5.3/账本 Done 规则的 `out/test-evidence/wp-xx/<run-id>/` 不一致——T01 验证已登记同款观察，本验证以本文件（规范根）桥接；建议卡片所有者后续修订对齐。
2. **08-31 两次治理裁决的基线记录**：选项关闭路径预期修订（4ad012d）与顶层注册授权（2287b55）记录于卡文修订与提交信息；DOCUMENT-BASELINE 检查点最新为 D14（2026-08-30），未见对应检查点行。建议治理会话补记 D15 检查点以闭合权威变更流程痕迹。
3. CMake 4.3.1 移除 FindBoost 模块（CMP0167 警告）：当前以 `CMAKE_PREFIX_PATH` 指向 vcpkg Boost 1.91 化解；全树配置对环境前缀的依赖建议由 WP-01-T03 统一测试入口脚本固化（其 VS x64 环境发现职责）。

## 6. 验证签署

独立验证者/治理协调（ZCode 治理会话，2026-08-31）：于结构修订提交 `2287b55` 复跑上表全部命令并核对范围、证据与提交格式，**全部通过**；据此在治理提交中将 WP-01-T02 登记为 `Done`。本文件为验证证据，不构成对下一任务的实施授权——下一任务解锁见账本。
