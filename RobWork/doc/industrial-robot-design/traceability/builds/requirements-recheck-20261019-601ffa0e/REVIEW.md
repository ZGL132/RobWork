# 需求模块修复状态复核：601ffa0e

日期：2026-10-19。冻结HEAD：`601ffa0ed8b4f7887576dfb9a560acffea86b757`，分支`wp14-t12`。

**结论：仍未修复完成。用户“需求模块修复完成后再修复建模模块”的条件未满足，本轮没有开始修改建模产品代码。**

本轮实际重建5个集成目标，并重新编译此前独立探针链接当前库；重跑需求模型、契约、GUI、运动学、ui套件及独立反例。所有Qt GUI依VS2022 x64、windows平台、绝对exe、单进程顺序执行。未使用子代理，未修改产品源码，未提交或推送。

## 阻断问题

### P1-1：允许工具滚转的姿态残差方向与加法迭代不一致

[Ik.cpp:367](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Ik.cpp:367) 构造 E=R_targetᵀR_actual，其残余轴角方向是目标到实际；后续直接投影到正向角速度Jacobian，并在 [Ik.cpp:1021](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Ik.cpp:1021) 执行 q+=dq。此残差符号与纠正更新方向相反。参考系投影也需要一并核对，不能只以已满足目标的初值为验收。

真实复现（公开IkSolver＋真实FK，没有求解替身）：

- 单Z轴、TCP在轴心、目标单位姿态、允许滚转[-0.2,0.2]rad，唯一初值0.5rad、迭代200次，已知q=0合法，却返回0解。原评审窄区间反例仍失败。
- 单X轴、TCP在轴心、目标Rx(0.2)、允许全周工具滚转，唯一初值0、迭代200次，已知q=0.2合法，却返回0解。
- 对应 `NarrowRollOutsideSeedMustConverge_REQ09` 与 `TiltedDirectionNeedsActualCorrection_REQ09`，见 [residual_probe.log](residual_probe.log)、[XML](residual_probe.xml)、[源码](residual_probe.cpp)。

现有产品新增 `ToolRollRangeIteratesIntoRange_WP14T12` 使用TCP偏置(1,0,0)，位置误差也会推动q回零，因而该用例通过不能证明姿态区间自身具备正确纠错能力。重新验收必须包含TCP在轴心、仅姿态驱动、区间外单初值，并对实际方向与残差单独断言。

### P1-2：相反180°工具轴被误判成功，已贯通需求批量评估

[Ik.cpp:392](C:/Users/zgl18/.codex/worktrees/486b/RobWork/RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics/src/Ik.cpp:392) 用 R−Rᵀ 取轴；180°旋转的反对称部分也近零，却在分支中被视为“近恒等”而置零残差。方向任务不能把这两种情况等同。

真实复现：

- 单Z轴、行程[-0.01,0.01]rad、TCP在轴心，实际工具+Z恒为+Z；目标Rx(pi)要求工具轴朝−Z。任何关节值或允许的工具Z滚转均无法满足。
- TaskPoseConstraint允许滚转时返回1解；相同模型/目标的旧nullopt FullPose对照正确返回0解。见 `OppositeToolAxisMustBeRejected_REQ09` 与 `OppositeToolAxisFullPoseControl_REQ09`。
- **实际需求批量评估通道** `KinEvaluationExecutor` 中，PointAtTarget设向−Z、AlignGeometryNormal设目标Frame局部+Z为−Z，两条均被标成 `completed 解1个/1工况`，预期均为failed。见 [ui_probe.log](ui_probe.log)、[XML](ui_probe.xml)、[源码](ui_probe.cpp) 的 `ReviewOppositeDirectionMustFail`。
- 这是实际计算状态错误，不是环境、剪贴板或仅缺视觉截图。

重新验收须加入0/近0、pi/近pi、正反向工具轴、倾斜参考系，以及PointAtTarget和AlignGeometryNormal的生产通道对照；对每个返回解用独立FK核对工具轴，而非只断言“completed”。

## 原7类问题逐类对账

|原编号|本轮实际证据|判定范围|
|---|---|---|
|1 滚转区间与真实工具滚转|合法区间内初值及倾斜已满足初值通过；区间外初值仍零解|未关闭，见P1-1；新增180°误成功见P1-2|
|2 Euler残差与Jacobian|非零yaw纯pitch及1.4/1.55/1.7/2.0/2.8rad姿态原反例返回解|原复现用例通过，不外推所有数值边界|
|3 正90°万向锁|原Z转轴+Ry(pi/2)目标和仅yaw已满足反例均通过|原复现用例通过，不代表全奇异点收敛已经验齐|
|4 指向/法向滚转自由|原世界+Z及倾斜合法方向正例通过；180°反例在生产通道错误completed|未关闭，见P1-2|
|5 区域尺寸与计划同步|单字段和批量表2→4m均将X计数5→9|原复现路径通过|
|6 无修改确认补建计划|可见面板直接确认后1计划|原复现路径通过|
|7 Random分母|count=10的digest分母10|原复现路径通过|

## 本轮执行结果

|套件/探针|总数|通过|失败|跳过|
|---|---:|---:|---:|---:|
|requirements|222|221|0|1|
|requirements_contract|16|16|0|0|
|requirements_gui|106|106|0|0|
|kinematics|279|278|0|1|
|ui|319|319|0|0|
|原区域GUI独立反例 gui_probe|5|5|0|0|
|方向生产通道 ui_probe（含新增边界）|4|3|1|0|
|求解器边界 residual_probe|4|1|3|0|

原有5套合计942：940通过、2个既有登记跳过；13项独立验收反例：9通过、4失败。失败测试数不同于独立根因数量。旧 `ik-probe.log` 是打印式诊断，没有失败退出断言，关键失败已由本轮gtest固化；不得将其进程退出0理解为全部用例通过。

本轮requirements_gui **106项全过**，此前剪贴板故障未在本轮复现，不能继续沿用历史6失败作为当前结论。模型/运动学套件各1条GUI登记占位跳过如实保留，不抹成全通过。

## 全功能验收仍有证据缺口

功能账本 `batch25-functional-ledger.md` 仍保留未执行行，包括真实覆盖着色/图例、标准视图/相机导航/显示分组、高亮与组掩码，以及同一需求集的TRJ/DYN/选型联合消费等。部分PASS行明确仅部分验证。本轮没有重新完成120项逐行交互与真实视觉终验，因此即使上述数值问题修好，也不能仅凭套件全绿宣称全功能验收通过。

## 可复跑与边界

本目录保留测试XML、per-exe JSON、日志、临时探针源码/链接响应文件及run.ps1。脚本与响应文件使用本机绝对路径，复跑需当前checkout和VS/Qt/vcpkg依赖。可在原build目录执行：

```powershell
pwsh -NoProfile -File build/requirements-recheck-20261019/run.ps1 -Mode build
pwsh -NoProfile -File build/requirements-recheck-20261019/run.ps1 -Mode compile
pwsh -NoProfile -File build/requirements-recheck-20261019/run.ps1 -Name residual_probe
pwsh -NoProfile -File build/requirements-recheck-20261019/run.ps1 -Name ui_probe -Filter '*Review*'
```

本轮未执行新的独立冒烟配置、全仓gate-all或正式宿主完整三维终验；没有把这些标为通过。探针首次链接缺kinematics/runtime库失败，换用包含这些真实依赖的既有UI链接响应文件后构建成功；该探针装配失误不计为产品缺陷，正式失败结论来自成功构建后的gtest执行。

