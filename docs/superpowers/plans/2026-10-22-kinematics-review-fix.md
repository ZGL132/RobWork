# 运动学审核修复与交互优化 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 修复异步运动学求解的会话一致性、异常恢复、覆盖结果校验和 future 生命周期问题，并增强面板状态反馈。

**Architecture:** 让异步任务只消费提交时冻结的不可变运行时视图和值请求；回调以 `snapshotId/epoch/configDigest` 作为提交身份，在 UI 线程统一校验后再写入会话。面板继续只负责投影和事件转接，通过状态摘要表达忙碌、成功、失败和过期，不新增业务判定。

**Tech Stack:** C++17、Qt Widgets、现有 ui 执行器缝、GTest、Visual Studio 2022 x64。

---

### Task 1: 固化异步求解身份与快照接口

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.hpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.hpp`
- Test: 现有 kinematics GUI/model 测试目录中与 `KinematicsPanelWidget`、`KinEvaluationExecutor` 相关测试

- [ ] 写测试：提交求解后替换会话模型，完成回调必须被判定为过期且不得改变当前结果。
- [ ] 执行该测试，确认当前实现因捕获可变视图或未校验提交身份而失败。
- [ ] 增加不可变快照类型别名/导出入口，提交时复制 `snapshotId/epoch/configDigest` 与求解所需值；后台 job 只捕获 `shared_ptr<const ...>` 或等价 const 值视图。
- [ ] 让面板完成回调调用统一的身份校验函数，过期回执走状态更新而不写会话。
- [ ] 重新执行测试并确认通过。

### Task 2: 异常回执与 future 生命周期

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/UiPlugin.cpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/plugin/KinEvaluationChannel.cpp`
- Test: UiPlugin/KinEvaluation GUI input tests

- [ ] 写测试：job 抛出 `std::invalid_argument` 或 `std::logic_error` 后，完成回调收到失败回执，求解按钮恢复可用。
- [ ] 执行测试确认当前实现卡在 `m_solveInFlight`。
- [ ] 在执行器边界捕获 `std::exception` 和未知异常，生成稳定失败回执并保证 UI 回调只调用一次；用一次性任务句柄替代捕获承载 future 的 `shared_ptr`。
- [ ] 保留执行器缺席时的同步降级路径，并补中文注释说明线程、所有权和错误语义。
- [ ] 重新执行异常和生命周期测试。

### Task 3: 覆盖结果配置摘要校验

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPluginAssembly.cpp`
- Modify: 相关面板状态刷新代码
- Test: coverage assembly/presentation tests

- [ ] 写测试：相同 snapshot/epoch 但不同 `configDigest` 的覆盖结果必须被拒绝并保持旧结果为空/过期。
- [ ] 执行测试确认当前实现错误接受结果。
- [ ] 将 `configDigest` 纳入覆盖结果锚核对；配置变更、项目切换和 rebind 时清空旧覆盖行并设置需重新求解状态。
- [ ] 重新执行覆盖测试。

### Task 4: 面板交互状态优化

**Files:**
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.cpp`
- Modify: `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/plugin/KinematicsPanelWidget.hpp`
- Test: Windows Qt GUI input/session tests

- [ ] 写 GUI 测试：求解中按钮禁用并显示忙碌文案；成功、失败和过期分别显示可区分状态；单位切换/TCP 回填后提示需重新求解。
- [ ] 执行测试确认当前界面缺少这些状态或文案。
- [ ] 调整位姿页布局为目标输入、上下文、求解操作、状态摘要分组，保留现有控件和域投影；增加字段就地错误样式/提示，避免改变计算语义。
- [ ] 在 Windows x64 开发者环境按规则一次启动一个绝对路径 GUI 测试可执行文件，设置 `QT_QPA_PLATFORM=windows`。
- [ ] 重新执行 GUI 测试并留存 XML、报告和截图/日志。

### Task 5: 全量验证与交付

**Files:**
- Modify: only files listed above if verification finds a defect
- Evidence: `traceability/builds/` and task report locations used by this branch

- [ ] 运行独立冒烟配置和集成 Release 构建，确认 `RWS_BUILD_INDUSTRIALROBOT=ON` 且带 vcpkg toolchain。
- [ ] 运行单元、契约和 GUI 测试，区分 passed/skipped/failed，不把环境跳过计入通过数。
- [ ] 运行 `ird_gates`、`git diff --check` 和任务验证脚本。
- [ ] 编写中文四段提交说明，提交并推送 `origin wp15-t20`。
