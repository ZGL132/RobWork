# 轨迹规划模块审核结果

审核基线：`119dce6bb`。结果按“证据维度”拆分；一项只有在算法、窗口交互和真实三维均有证据时才可封闭。

## 总结

| 维度 | 结果 |
| --- | --- |
| 计算/数值 | 182/182 通过，无跳过；覆盖 PTP、笛卡尔、避障、平滑复检、时间参数化、播放、曲线投影、局部重规划、导出及 S1～S3 |
| 公共契约 | 69/69 通过，无跳过；覆盖接口、版本词表、插件描述符和依赖红线 |
| 正式宿主装配 | 不通过；白名单有 trajectory，但 `DomainAssembly.cpp` 没有创建/注册 trajectory assembly |
| 真实窗口 | 未完成；没有 trajectory `gui_test`，正式宿主也没有轨迹面板可进入 |
| 真实三维 | 未通过；harness 不绑定 `sessionPoseSink`，没有真实机器人姿态、TCP/路径线、障碍物或失败段证据 |
| 需求交付结论 | **不通过，不能认定 TRJ-01～TRJ-08 全功能交付** |

## 问题清单

| ID | 严重度 | 问题 | 证据 | 影响 |
| --- | --- | --- | --- | --- |
| F-TRJ-001 | 阻断 | 正式宿主没有装配 trajectory。白名单包含 `trajectory`，但没有 include、`createTrajectoryPluginAssembly()`、`registerWithHostRegistrar()`、状态登记或面板挂位 | `ui/plugin/DomainAssembly.cpp:41,103-306`；`UiPlugin.cpp` 只消费前三域和 extra domains | 产品中轨迹模块不可见，TRJ-07 和所有窗口验收无法开始 |
| F-TRJ-002 | 阻断 | 没有真实 View3D 联动。harness 明确将会话姿态缝置空；播放只更新文字读数 | `trajectory/app/HarnessMain.cpp:203-205,228-236`；`TrajectoryPanelWidget.cpp:354-359` | 无法证明播放姿态、TCP/轨迹线、障碍物、失败对象和局部重规划显示正确 |
| F-TRJ-003 | 高 | 没有 trajectory GUI 自动测试目标。CMake 仅有 `_app` 手动 harness，明确没有 `_gui_test` | `trajectory/CMakeLists.txt:69-70,697-716` | 窗口控件、禁用态、首屏刷新、真实注册链路没有可重复回归 |
| F-TRJ-004 | 中 | 曲线页首次创建只调用 `refreshFromSession()`，没有调用 `refreshCurvesPage()`；首次打开时曲线和关节下拉可能为空，必须先改变下拉触发刷新 | `TrajectoryPanelWidget.cpp:63`、`165-243`、`366-415` | TRJ-07 曲线查看首屏不符合预期 |
| F-TRJ-005 | 高 | 命令处理结果被拒绝后仍无条件调用 `commandSubmit`，拒绝命令可能进入宿主命令管线 | `TrajectoryPanelWidget.cpp:268-296`，提交缺少 `outcome.accepted` 门控 | 破坏失败交互和只读/无效状态安全边界 |
| F-TRJ-006 | 中 | PTP 越限诊断实际单位无法按关节类型确定，`axisUnitToken()` 永远返回 `rad`，移动轴可能错误显示 `rad` | `trajectory/src/Ptp.cpp:41-53,270-295`；`PtpRequest` 没有关节类型 | 违反 TRJ-06“实际值/期望值/单位”正确诊断要求，人工无法可靠判断越限 |
| F-TRJ-007 | 高 | CartesianLine 只检查正交性，不检查 `det(R)>0`；反射矩阵可通过并进入四元数/SLERP | `trajectory/src/CartesianLine.cpp:68-89` | 非合法姿态可能被当成旋转规划，数值结果和失败诊断错误 |
| F-TRJ-008 | 高 | `expandSequence()` 对 approach/retract 只生成结构条目，相关 `ptpGeometry` 保持空且游标失效；生产代码没有把序列编排到 `planCartesianLine()` 的完整 IK/后续 PTP 链 | `trajectory/src/Sequence.cpp:334-418`；`Sequence.hpp` 明确“待 T05 填充” | 含接近/撤离的有序作业序列不能形成可执行完整轨迹，TRJ-01/TRJ-02 端到端未闭合 |

## 人工验收阻断项

本轮不能给出“请手工操作验证通过”的步骤，因为正式宿主没有轨迹页面。即使直接运行 `sdurws_ird_trajectory_app`，也只能验证开发 harness 的控件与演示查表；它不能证明真实项目输入、命令注册、runtime 快照、真实碰撞后端或 View3D。必须先封闭 F-TRJ-001～003，再按矩阵逐项执行窗口和三维验收。

## 建议修复顺序

1. 在正式宿主完成 trajectory assembly、面板挂位、就绪投影和命令注册，并增加宿主装配回归。
2. 将 runtime 会话姿态、TCP/轨迹线、障碍物和失败段定位接到 View3D；提供真实场景证据。
3. 增加 Windows `gui_test`，按项目规则在 VS x64 + `QT_QPA_PLATFORM=windows` 下逐个运行。
4. 修复曲线首刷、拒绝命令提交和旋转矩阵行列式校验，分别增加回归测试。
5. 设计并实现 Sequence→CartesianLine→IK→PTP 的生产编排，补真实端到端测试。
6. 为 PTP 请求补充可靠的关节类型/单位来源，修正移动轴诊断并补单位负例。
