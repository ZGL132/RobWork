# 轨迹规划模块全面审核报告

## 结论

本轮审核结论为 **不通过**。轨迹计算库的自动测试结果很好，但产品交付链不完整：正式宿主没有装配 trajectory，真实窗口没有可重复测试目标，三维联动没有接线，并确认了 8 项集成/代码问题。因此当前不能把 TRJ-01～TRJ-08 宣布为全功能通过。

## 审核范围

覆盖 `REQUIREMENTS.md` 的 TRJ-01～TRJ-08，并单列已解冻的 TRJ-08-S1 jerk、S2 工艺速度、S3 连续工艺段。检查范围包括：

- PTP、序列拓扑、接近/撤离、笛卡尔插值、IK 分支连续性和奇异警告。
- RobWork 规划器、候选淘汰、避障重规划、预算、取消和碰撞证据。
- 路径简化、五次样条平滑、C2 连续性、P-06 复检、限位/碰撞复检。
- 时间参数化、速度/加速度/jerk、工艺速度、连续工艺边界。
- 播放、曲线、局部重规划、标准导出、失败段诊断。
- 插件装配、命令状态、正式窗口和 View3D 姿态/路径/高亮联动。

## 证据审查

已读取并核对：

- `traceability/trajectory-test.xml`：182 项通过，0 失败，0 跳过。
- `traceability/trajectory-contract-test.xml`：69 项通过，0 失败，0 跳过。
- trajectory CMake、plugin、assembly、harness、Sequence、Ptp、CartesianLine 生产代码。
- 正式宿主 `ui/plugin/DomainAssembly.cpp`、`UiPlugin.cpp`。

自动测试覆盖了大量算法和契约边界，但测试替身或独立 assembly 不能替代正式宿主、runtime 快照、真实窗口和真实三维证据。`trajectory/app/HarnessMain.cpp` 自己声明 View3D 桥为空；`trajectory/CMakeLists.txt` 自己声明没有 `_gui_test`，因此这些部分必须按未通过处理。

## 主要发现

### F-TRJ-001：正式宿主没有装配 trajectory（阻断）

`DomainAssembly.cpp:41` 只有白名单登记；`assembleDomainPlugins()` 从 `:103` 到 `:306` 实际装配 modeling、requirements、kinematics 及 extra domains，没有 trajectory 的 assembly include、创建、注册、状态登记、面板工厂或服务接线。独立 `TrajectoryPluginAssembly` 契约通过不能证明正式产品可见。

### F-TRJ-002：没有真实三维联动（阻断）

`HarnessMain.cpp:203-205` 明确将 `sessionPoseSink` 置空。面板确实在 `TrajectoryPanelWidget.cpp:354-359` 调用该缝，但没有宿主实现时只更新帧读数。当前没有真实机器人姿态、TCP 坐标架、轨迹线、障碍物、透明/线框或失败段高亮证据。

### F-TRJ-003：没有真实 trajectory GUI 测试（高）

trajectory 目录没有 `gui_test/`。CMake 只构建 `_app` harness，并把 GUI 运行定义为人工点验；没有自动检查窗口注册、按钮门控、曲线首屏、命令拒绝和 View3D 联动的回归。

### F-TRJ-004：曲线首屏没有刷新（中）

构造函数只调用 `refreshFromSession()`；该函数没有刷新曲线。曲线只有关节或通道改变时才重投影，首次打开曲线页可能为空。

### F-TRJ-005：拒绝命令仍提交（高）

`onCommandButtonClicked()` 记录 `outcome.accepted` 后仍无条件执行 `commandSubmit(token)`。无效命令、负载不匹配、只读拒绝等场景可能进入宿主管线。

### F-TRJ-006：PTP 移动轴单位错误（中）

`Ptp.cpp` 的 `axisUnitToken()` 永远返回 `rad`，但注释承认本域没有关节类型。移动关节越限时比较字段和原因文案可能错误标成 `rad`，不能满足可核查诊断。

### F-TRJ-007：反射矩阵未拒绝（高）

`CartesianLine.cpp:isOrthogonal()` 检查列范数和两两点积，却未检查行列式为正。det=-1 的反射矩阵会通过并进入四元数/SLERP，必须加入 proper rotation 检查和负例。

### F-TRJ-008：序列接近/撤离端到端未闭合（高）

Sequence 展开对 approach/retract 生成结构条目，但 `ptpGeometry` 为空、`cursorValid=false`，并没有在生产编排中调用 CartesianLine 的采样/IK 或将终点构型交给后续 PTP。它只能证明结构展开，不是可执行完整轨迹。

## 交付判断

| 需求 | 当前判断 |
| --- | --- |
| TRJ-01 | 计算半区通过；含 approach/retract 的端到端序列未闭合；窗口/三维未通过 |
| TRJ-02 | 插值和分支算法通过；proper rotation 校验缺失；窗口/三维未通过 |
| TRJ-03 | 算法/契约通过；正式宿主、真实窗口和三维避障显示未通过 |
| TRJ-04 | 算法复检通过；真实 policy/runtime/三维失败定位未通过 |
| TRJ-05 | 时间参数化算法通过；曲线首屏和真实动画未通过 |
| TRJ-06 | 失败素材算法通过；PTP 单位与拒绝提交有缺陷，窗口未通过 |
| TRJ-07 | 独立面板/查表/导出算法有证据；正式宿主、真实 View3D 和 GUI 回归未通过 |
| TRJ-08 | S1～S3 计算与契约测试通过；没有用户可操作的正式轨迹页面和三维表达证据 |

详细逐项记录见同目录的 `MATRIX.md` 和 `MATRIX-RESULTS.md`。本轮没有修改产品代码，也没有把未执行的 GUI/三维测试标为通过。
