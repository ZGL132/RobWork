# optimization

本目录为 optimization 单元公共头目录（命名空间 sdurws/ird/optimization）。

**WP-20-T02 已落位**：`sdurws_ird_optimization` 为真实 STATIC 库（上级
INTERFACE 占位升级，CMake 目标名不变），当前公共头＝`DiagCodes.hpp`
（OPT-\* 15 码稳定诊断码登记表——units/optimization.md §6.6 登记表全表
物化＋装配期注册函数；阶段锁码落位 `OPT-STAGE-LOCKED`，P-OPT-1 对应
关系见卡内登记）。

依赖面与布局见 units/optimization.md §3.1/§3.2；变量/补丁/约束/目标/
运行/评估器端口/预检/Pareto/导出/应用等业务公共头（Types/Variable/
CandidatePatch/Constraint/Objective/Run/EvaluatorPorts/Preflight/Pareto/
Export/Applier——§3.1 布局表）随 WP-20-T03~T09 按布局表任务列增列。
本文件不参与编译。
