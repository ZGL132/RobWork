# WP-01 构建与依赖基线实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:executing-plans` and complete this plan task-by-task.

**Goal:** 建立新产品代码的 CMake 边界、Windows x64 可重复构建、GitLab CI 门禁以及依赖/API/许可证基线。

**Architecture:** 所有新目标位于 `rwslibs/industrialrobot`，使用 `sdurws_ird_*` 前缀。一个 PowerShell 入口负责进入 VS x64 环境、配置、构建和测试；GitLab Windows Runner 调用同一入口。

**Tech Stack:** CMake、MSVC 2022 x64、Qt、PowerShell 7、GitLab CI、CTest。

---

## 范围与所有权

**创建：**

- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/CMakeLists.txt`
- `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/cmake/IndustrialRobotTargets.cmake`
- `RobWork/scripts/industrial-robot/configure.ps1`
- `RobWork/scripts/industrial-robot/build.ps1`
- `RobWork/scripts/industrial-robot/run-tests.ps1`
- `RobWork/scripts/industrial-robot/check-boundaries.ps1`
- `RobWork/dependencies/industrial-robot-baseline.json`
- `RobWork/gitlab-ci/industrial-robot-windows.yml`

**修改：** `RobWork/RobWorkStudio/src/rwslibs/CMakeLists.txt`，只增加一个 `add_subdirectory(industrialrobot)`。

**覆盖需求：** ARC-02，NFR-MNT-02、04～07，NFR-DEP-01～03、05，NFR-SEC-04、05。

## 构建契约

```text
sdurws_ird_core              无 Qt Widgets
sdurws_ird_project           依赖 core
sdurws_ird_evidence          依赖 core、project
sdurws_ird_runtime           依赖 core、RobWork；DWC 适配可依赖 RobWorkSim
sdurws_ird_policy            依赖 core、runtime
sdurws_ird_execution         依赖 core、project、evidence、Qt Core
sdurws_ird_diagnostics       依赖 core、Qt Core
sdurws_ird_io                依赖 core、diagnostics、Qt Core
sdurws_ird_ui                依赖公开端口、Qt Widgets、RobWorkStudio
sdurws_ird_reporting         依赖 evidence、diagnostics、Qt Gui/PrintSupport
```

核心库不得链接旧四插件目标。新增第三方依赖必须先更新基线 JSON，记录版本、来源、用途、许可证、哈希、离线副本和审批记录。

## 任务

### Task 1：先写构建边界失败测试

- [ ] 编写 `check-boundaries.ps1` 的夹具，证明旧插件依赖、Widget 头进入核心、未登记第三方库和业务目录中的名称前缀拼接都会失败。
- [ ] 运行夹具并确认脚本在规则未实现前返回非零。
- [ ] 实现最小扫描规则，再确认全部夹具通过。

### Task 2：建立新目标骨架

- [ ] 增加 `industrialrobot` 单一入口和按工作包关闭/开启的 CMake 选项。
- [ ] 创建空的接口库/最小静态库目标，保证依赖方向可由 CMake 验证。
- [ ] 开启 `BUILD_TESTING` 时为每个核心库注册独立 CTest 目标。
- [ ] 安装规则只包含新目标的公开头、插件和资源，不包含测试数据与旧插件私有文件。

配置与构建命令：

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\configure.ps1 -Configuration Debug
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\build.ps1 -Configuration Debug
```

脚本内部固定使用：

```text
cmake -S RobWork -B out/build/industrial-robot
      -G "Visual Studio 17 2022" -A x64
      -DWITH_RWS=ON -DWITH_RWSIM=ON -DBUILD_TESTING=ON
```

### Task 3：统一测试入口

- [ ] `run-tests.ps1` 自动进入 Visual Studio x64 开发环境。
- [ ] 模型测试使用 CTest；GUI 测试设置 `QT_QPA_PLATFORM=windows` 并按绝对路径一次启动一个可执行文件。
- [ ] 脚本拒绝 `QT_QPA_PLATFORM=offscreen`，发现冲突的 `QT_*`/`QML_*` 环境变量时先报告并停止。
- [ ] 支持 `-Regex` 精确选择一个或多个模型测试，GUI 模式拒绝并行启动。

### Task 4：GitLab Windows 门禁

- [ ] 新 CI 文件调用相同 configure/build/run-tests/check-boundaries 脚本。
- [ ] 缓存仅保存构建依赖，不缓存正式测试结果。
- [ ] 上传 CTest 日志、边界扫描报告和依赖清单作为作业工件。
- [ ] 集成分支要求构建、模型测试、GUI 测试和边界扫描全部成功。

### Task 5：依赖和 API 基线

- [ ] 记录 RobWork、RobWorkStudio、RobWorkSim commit、Qt、MSVC、碰撞后端、构建选项和使用的稳定 API。
- [ ] 生成第三方组件、版本、许可证和哈希清单。
- [ ] 对新增依赖审批流程编写 ADR 模板；未批准依赖不能进入 CMake。

## 验证命令

```powershell
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1
pwsh -NoProfile -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_.*_test$'
```

## 退出条件

- 干净 Windows x64 环境可用单一入口完成配置、构建和测试。
- 新核心目标无 Qt Widgets 和旧业务插件依赖。
- GitLab Windows Runner 与本机执行相同门禁。
- 依赖/API/许可证基线完整，未审批新增依赖为 0。
