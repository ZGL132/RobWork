# WP-01 构建与依赖基线实施计划

> 阶段/发布：阶段 A 前提 / R1。
> 负责范围：新工业机械臂代码的 CMake 边界、Windows x64 构建、测试入口、CI、依赖和许可证基线。
> 责任分离：脚本实现者、构建验证者、依赖评审者必须是不同执行上下文。

## 1. 目标、非目标与完成定义

**目标：**
- 建立 sdurws_ird_* 命名空间下的 CMake 目标骨架和单向依赖图。
- 在干净 Windows x64 + Visual Studio 2022 环境中，用统一 PowerShell 入口完成配置、构建、模型测试和 GUI 测试。
- 固化 Qt 平台、单进程 GUI、CTest、边界扫描、打包和 GitLab Windows Runner 规则。
- 建立 RobWork、RobWorkStudio、RobWorkSim、Qt、MSVC、碰撞后端和第三方组件的版本/许可证/哈希基线。

**非目标：**
- 不实现领域对象、评估器、插件业务逻辑或 GUI 功能。
- 不修改旧插件内部 CMake，除 rwslibs/CMakeLists.txt 增加新入口外不得改变旧目标。
- 不通过脚本自动修复源码、依赖清单或环境变量。
- 不把 PowerShell 7 专属语法带入文档门禁；构建脚本若确需 7，必须显式声明前置。

## 2. 架构与依赖方向

### 2.1 目标拓扑

sdurws_ird_core（无 Qt Widgets、无 RobWork 指针）
  → sdurws_ird_project（core、Qt Core）
  → sdurws_ird_evidence（core、project）
  → sdurws_ird_runtime（core、RobWork；DWC 可选 RobWorkSim）
  → sdurws_ird_policy（core、runtime）
  → sdurws_ird_execution（core、project、evidence、Qt Core）
  → sdurws_ird_diagnostics（core、Qt Core）
  → sdurws_ird_io（core、diagnostics、Qt Core）
  → sdurws_ird_reporting（evidence、diagnostics、Qt Gui/PrintSupport）
  → sdurws_ird_ui（公开服务、Qt Widgets、RobWorkStudio）

允许依赖只能从上向下；核心目标禁止链接旧 robotmodelbuilder、engineeringrequirements、kinematicanalysis、structureoptimizer 等业务插件目标。业务插件不得反向成为核心库依赖。

### 2.2 目标属性

| 目标 | 类型 | 允许依赖 | 禁止 |
| --- | --- | --- | --- |
| sdurws_ird_core | STATIC/OBJECT | C++ 标准库、RobWork Math | Qt Widgets、Widget、插件私有头 |
| sdurws_ird_project | STATIC | core、Qt Core | 工作进程写项目 |
| sdurws_ird_evidence | STATIC | core、project | 当前 UI 状态 |
| sdurws_ird_runtime | STATIC | core、RobWork/Sim | 消费者散落名称拼接 |
| sdurws_ird_policy | STATIC | core、runtime、Proximity | 插件私有碰撞默认值 |
| sdurws_ird_execution | STATIC | core、project、evidence、Qt Core | 直接写项目文件 |
| sdurws_ird_diagnostics | STATIC | core、Qt Core | 重复诊断码 |
| sdurws_ird_io | STATIC | core、diagnostics、Qt Core | 业务语义解析 |
| sdurws_ird_reporting | STATIC | evidence、diagnostics、Qt Gui | 读取 Widget |
| sdurws_ird_ui | STATIC/SHARED | 公开服务、Qt Widgets、RWS | 私有领域对象状态 |

## 3. 文件目录与所有权

RobWork/
├─ RobWorkStudio/src/rwslibs/CMakeLists.txt（仅新增 industrialrobot 子目录）
├─ RobWorkStudio/src/rwslibs/industrialrobot/
│  ├─ CMakeLists.txt
│  ├─ cmake/IndustrialRobotTargets.cmake
│  ├─ core/include/sdurws/ird/core/
│  ├─ project/include/sdurws/ird/project/
│  ├─ evidence/include/sdurws/ird/evidence/
│  ├─ runtime/include/sdurws/ird/runtime/
│  ├─ policy/include/sdurws/ird/policy/
│  ├─ execution/include/sdurws/ird/execution/
│  ├─ diagnostics/include/sdurws/ird/diagnostics/
│  ├─ io/include/sdurws/ird/io/
│  ├─ reporting/include/sdurws/ird/reporting/
│  ├─ ui/include/sdurws/ird/ui/
│  └─ tests/<module>/
├─ scripts/industrial-robot/
│  ├─ common.ps1、configure.ps1、build.ps1、run-tests.ps1
│  ├─ check-boundaries.ps1、package.ps1
├─ dependencies/industrial-robot-baseline.json
└─ gitlab-ci/industrial-robot-windows.yml

WP-01 可以创建目录和脚本骨架；不得修改其他 WP 所有的公共头、领域实现、需求 CSV 或业务插件源文件。

## 4. CMake 配置契约

### 4.1 选项和默认值

| 选项 | 默认 | 作用 |
| --- | --- | --- |
| IRD_BUILD_CORE | ON | 构建 WP-03 核心 |
| IRD_BUILD_PROJECT | ON | 构建 WP-04 |
| IRD_BUILD_EVIDENCE | ON | 构建 WP-05 |
| IRD_BUILD_RUNTIME | ON | 构建 WP-06 |
| IRD_BUILD_POLICY | ON | 构建 WP-07 |
| IRD_BUILD_EXECUTION | ON | 构建 WP-08 |
| IRD_BUILD_DIAGNOSTICS | ON | 构建 WP-09 |
| IRD_BUILD_IO | ON | 构建 WP-11 |
| IRD_BUILD_REPORTING | ON | 构建 WP-12 |
| IRD_BUILD_UI | WITH_RWS=ON 时 ON | 构建公共 UI |
| IRD_BUILD_BUSINESS_PLUGINS | OFF | 阶段 B 以后显式打开 |
| BUILD_TESTING | 门禁时 ON | 注册独立 CTest |

关闭选项必须跳过所有依赖目标或明确配置失败，不允许生成半目标。

### 4.2 配置命令

cmake -S RobWork -B out/build/industrial-robot -G Visual Studio 17 2022 -A x64 -DWITH_RWS=ON -DWITH_RWSIM=ON -DBUILD_TESTING=ON -DIRD_BUILD_BUSINESS_PLUGINS=OFF

脚本必须从仓库根目录解析绝对 -S、-B，不依赖当前工作目录；生成器、架构和关键选项写入构建证据。

## 5. PowerShell 脚本逻辑

### 5.1 公共参数

所有入口支持 Configuration（Debug/Release）、BuildDirectory、SourceDirectory、Generator、Platform、NoConfigure 和 LogDirectory。默认构建目录为 out/build/industrial-robot，日志目录为 out/logs/industrial-robot/<timestamp>。

参数路径先 Resolve-Path；输出父目录由脚本创建。每一步只消费上一步成功工件；失败保留日志并停止。脚本不得自动删除既有构建目录。

### 5.2 VS x64 环境初始化

1. 检查 cl.exe、cmake.exe、ctest.exe。
2. 缺少 MSVC 环境时按顺序查找 vswhere.exe 和 VsDevCmd.bat -arch=x64 -host_arch=x64。
3. 在子进程加载开发者环境并导出 PATH、INCLUDE、LIB、VSINSTALLDIR。
4. 记录 MSVC、Windows SDK、CMake 和 CTest 版本。
5. 找不到 VS x64 时立即失败，不回退到 x86 或 MinGW。

### 5.3 configure/build/test/package 数据流

参数 → 绝对路径 → VS x64 环境 → CMake configure → cache/configure.log → build target → build.log → CTest/GUI 测试 → test.log → 边界扫描/依赖清单 → package manifest。

## 6. 测试入口与 GUI 规则

模型测试使用 CTest、默认 -j1、Regex 精确筛选，每个核心库注册独立目标；模型测试可使用 QCoreApplication。

GUI 测试必须在 VS x64 环境设置 QT_QPA_PLATFORM=windows，一次只启动一个 GUI 可执行文件且使用绝对路径；禁止 offscreen、禁止 Widget/Meta 并行。发现继承 QT_* 或 QML_* 冲突时先报告并停止；Qt 平台插件初始化失败时停止进程，清理冲突变量后按相同规则重启。

## 7. 边界扫描规则

check-boundaries.ps1 违反任一规则即非零：核心公共头包含 QWidget/QApplication/旧插件头；核心链接旧插件或未登记 target；业务插件自行拼接运行时名称；业务插件声明碰撞默认值/安全距离/排除规则；安装规则含测试数据/私有头/绝对构建路径；依赖缺版本/来源/许可证/哈希/审批；脚本出现 offscreen 或 GUI 并行。

夹具位于 docs/industrial-robot-design/fixtures/wp-01/boundaries/，每个夹具只注入一种违规。

## 8. 依赖与许可证基线

dependencies/industrial-robot-baseline.json 必须包含 schemaVersion、projectCommit、components、compiler、qt、buildOptions。每个组件包含 name、version、source、license、sha256、usage、approved、approvalRef。

缺来源、许可证、哈希或 approved=false 的组件不得进入 CMake。新增依赖流程固定为 ADR → 安全/许可证评审 → 更新 JSON → CMake 引用 → 扫描和 CI。

## 9. CI 门禁和工件

industrial-robot-windows.yml 步骤固定为 checkout → VS x64 → configure → build → 模型测试 → GUI 测试 → check-boundaries → package → 上传日志/CTest XML/边界报告/依赖 JSON/安装 manifest。

缓存只允许构建依赖和包下载，不缓存正式测试结果、结果数据库或项目快照。集成分支必须同时通过构建、模型测试、GUI 测试、边界扫描和依赖审计。

## 10. 任务清单和详细实施步骤

### WP-01-T01 构建边界失败测试
创建旧插件依赖、Widget 头、未登记库、名称拼接四类夹具；先确认规则未实现时非零；实现扫描器和稳定诊断；再次运行确认合法样例通过。

### WP-01-T02 建立目标骨架
仅在 rwslibs/CMakeLists.txt 增加 industrialrobot；创建模块 targets、include 安装白名单、依赖断言和独立 CTest；验证选项关闭不会产生半目标。

### WP-01-T03 统一测试入口
实现参数和绝对路径；实现 VS x64 发现和版本记录；实现 configure/build/CTest 日志；实现 Windows Qt、单进程和冲突变量检查；分别验证模型与 GUI 路径。

### WP-01-T04 GitLab Windows 门禁
创建 Windows Runner job、缓存白名单、模型/GUI 分离 job、失败工件和分支保护；注入脚本失败确认后续 job 阻断。

### WP-01-T05 依赖与 API 基线
采集 RobWork 家族、Qt、MSVC、SDK、碰撞后端和第三方版本；生成 JSON；对未批准依赖编写阻断测试和 ADR；独立评审 CMake 与基线一致性。

## 11. 测试矩阵

| 场景 | 输入 | 预期 |
| --- | --- | --- |
| 干净配置 | Windows x64、VS 2022、基线 | 生成 x64 构建和配置日志 |
| 目标构建 | 配置成功 | 启用目标成功、依赖无环 |
| 核心边界 | QWidget/旧头/未登记 target | 非零并列出文件行 |
| 模型测试 | CTest 目标 | 单目标日志和正确退出码 |
| GUI 测试 | 绝对 exe 路径 | windows 平台、单进程完成 |
| 平台冲突 | offscreen/QML 变量 | 先报告并停止 |
| CI 失败 | 任一步非零 | 后续阻断且上传日志 |
| 安装边界 | 私有头/测试数据 | 非零且不出正式包 |
| 依赖缺项 | 缺 hash/license/approval | 非零并指出组件 |

## 12. 验证

在仓库根目录执行：

powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\configure.ps1 -Configuration Debug
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\build.ps1 -Configuration Debug
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_.*_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\package.ps1 -Configuration Release

GUI 测试必须遵守 Windows Qt 启动规则；不得使用 offscreen 或同时启动多个 GUI 可执行文件。

## 验证

WP-01 验证必须在仓库根目录、Visual Studio x64 开发环境中执行。配置、构建、模型测试、GUI 测试、边界扫描和打包分别保存日志；GUI 测试一次只启动一个绝对路径可执行文件。

## 13. 迁移、回滚与删除

旧插件目标在 R1 前保持可构建但不成为新核心依赖。新目标稳定后再删除重复 CMake、旧测试入口和旧安装规则，并提交迁移证据。脚本升级失败保留上一版脚本和构建目录，禁止自动删除用户目录或仓库根目录。依赖升级先更新 JSON、跑扫描和全套测试；失败回退到上一条已批准基线。

## 14. 独立评审与证据

构建评审者不得参与脚本实现，必须复核目标依赖图、Qt/旧插件边界、CMake 选项、安装白名单、5.1/7 环境入口、VS x64、Qt windows 单进程、CI 同命令和依赖 JSON。

必须提交 configure/build/test/scan/package 日志、CTest XML、边界夹具结果、依赖基线 JSON、安装 manifest、环境版本和独立评审记录。

## 15. 退出条件

- 干净 Windows x64 环境可用统一入口完成配置、构建和测试。
- 核心目标无 Qt Widgets、旧业务插件和未审批依赖。
- 模型测试与 GUI 测试按 Windows 规则单独可执行。
- GitLab Runner 与本机使用相同脚本、参数和工件格式。
- 依赖/API/许可证基线完整，未审批新增依赖为 0。
- 边界夹具、失败传播、安装白名单和回滚测试全部通过。

## 退出条件

以上退出条件必须由独立构建评审者根据日志、扫描报告和依赖清单签署确认。

## 任务卡索引

- [WP-01-T01 构建边界失败测试](../agent-tasks/WP-01-T01-boundary-tests.md)
- [WP-01-T02 建立目标骨架](../agent-tasks/WP-01-T02-cmake-skeleton.md)
- [WP-01-T03 统一测试入口](../agent-tasks/WP-01-T03-test-entry.md)
- [WP-01-T04 GitLab Windows 门禁](../agent-tasks/WP-01-T04-gitlab-gate.md)
- [WP-01-T05 依赖与 API 基线](../agent-tasks/WP-01-T05-dependency-baseline.md)
