# trajectory

本目录为 trajectory 单元公共头目录（命名空间 sdurws/ird/trajectory）。

**WP-16-T03 已落位**：`sdurws_ird_trajectory` 为真实 STATIC 库（上级 INTERFACE
占位升级，CMake 目标名不变），当前公共头＝`DiagCodes.hpp`（TRJ-\* 12 码稳定
诊断码登记表——units/trajectory.md §14.4 拟注册清单全表物化＋装配期注册函数）。

依赖面与布局见 units/trajectory.md §3.2/§4.2；PTP/直线/连续性/时间化等业务
公共头（TrjTypes/Errors/PlanConfig/Sequence/Ptp/CartesianLine/Continuity/
Planner/Smooth/Recheck/TimeParam/Quality/Evidence/Evaluators/KinematicsPort/
Commands/Export）随 WP-16-T04~T10 按布局表任务列增列。本文件不参与编译。
