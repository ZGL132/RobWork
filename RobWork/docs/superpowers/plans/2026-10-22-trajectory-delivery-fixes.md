# 轨迹规划交付闭环与交互优化实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. 本任务按单会话执行，不使用子代理。

**Goal:** 修复轨迹审核的 8 项问题，完成正式宿主、界面交互和三维联动验收闭环。

**Architecture:** trajectory 计算库继续保持零 Qt；正式宿主通过 `DomainPluginAssembly` 注入 trajectory 面板服务和 View3D 帧适配器。算法缺陷以最小公共接口扩展和端到端序列编排修复，UI 只做状态投影、查表和命令提交。

**Tech Stack:** C++17、Qt6 Widgets、GoogleTest、RobWorkStudio View3D、CMake Visual Studio 17 2022 x64。

---

### Task 1: 命令拒绝和曲线首刷回归

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajectoryPanelWidget.cpp`
- Test: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/gui_test/TrajectoryPanelGuiTest.cpp`（新增）
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/CMakeLists.txt`

- [x] 写两个失败用例：拒绝命令不调用提交出口；面板创建后曲线源被调用且关节下拉已填充。
- [x] 在 Windows GUI 规则下单独运行 GUI 测试确认失败原因来自当前实现。
- [x] 增加 `outcome.accepted` 门控和构造期曲线刷新。
- [x] 运行该 GUI 测试及轨迹契约测试，确认通过。

### Task 2: proper rotation 与 PTP 单位

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Ptp.hpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Ptp.cpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/CartesianLine.cpp`
- Test: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/test/CartesianLineContinuityTest.cpp`
- Test: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/test/PtpSequenceTest.cpp`

- [x] 增加反射矩阵失败用例并确认当前代码允许它通过。
- [x] 增加移动轴越限单位失败用例并确认当前诊断为 `rad`。
- [x] 在旋转判定加入正行列式检查；在 PTP 请求中加入逐轴单位来源并生成正确比较字段。
- [x] 运行对应单元测试和契约测试。

### Task 3: Sequence 接近/撤离端到端编排

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/include/sdurws/ird/trajectory/Sequence.hpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/src/Sequence.cpp`
- Test: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/test/PtpSequenceTest.cpp`

- [x] 写包含 approach、work、retract 的失败回归，证明当前计划含空几何且无失败素材。
- [x] 增加最小 CartesianLine 编排服务值对象，按站点把直线终点构型推进为后续 PTP 起点。
- [x] 服务缺失时返回可定位失败；服务成功时每个几何段均非空且段号连续。
- [x] 运行序列、CartesianLine、时间参数化和复检测试。

### Task 4: 正式宿主 trajectory 装配

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/DomainAssembly.hpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/DomainAssembly.cpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp`
- Test: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/test/DomainAssemblyTest.cpp`

- [x] 写装配失败用例：白名单含 trajectory 但当前 bundle 没有 trajectory 状态/面板。
- [x] 增加 trajectory assembly 成员、注册、状态和面板取用函数。
- [x] 注入命令、就绪状态和 View3D 帧适配器，保证失败隔离。
- [x] 运行宿主装配契约测试和集成构建。

### Task 5: 交互分组、诊断定位和 GUI 目标

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajectoryPanelWidget.cpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajectoryPanelWidget.hpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajPanelTypes.hpp`
- Create/Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/gui_test/TrajectoryPanelGuiTest.cpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/CMakeLists.txt`

- [x] 为无输入、计算中、只读和失败状态写 GUI 断言。
- [x] 将工作流页改成分组布局，显示明确的状态和拒绝原因。
- [x] 增加失败段定位服务缝与“定位到三维”按钮，空缝时明确降级。
- [x] 在 VS x64/`QT_QPA_PLATFORM=windows` 下逐个运行 GUI 测试。

### Task 6: 三维联动与最终验证

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/trajectory/plugin/TrajPanelTypes.hpp`
- Test: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/gui_test/TrajectoryHostView3DGuiTest.cpp`
- Docs: `RobWork/doc/industrial-robot-design/traceability/builds/trajectory-audit-20261022-119dce6b/`

- [x] 写播放帧驱动真实会话姿态的 GUI/宿主空态探针；TCP/路径图层和失败段选择保留未闭合证据。
- [ ] 接入正式归档 Playback/曲线数据源、路径对象映射并验证真实三维运动（未完成，见 CLOSURE-REPORT）。
- [x] 运行完整集成构建、轨迹单元/契约/GUI 测试和正式宿主空态验收。
- [x] 更新闭合报告，逐项记录通过、失败、未执行和证据路径。
