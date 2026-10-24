# GAP-TRJ-LINEAR-01：PTP 线性插值的有限极值差异

状态：待决；仅登记WP-11的具体适配缺口，不批准新核心、框架修改或生产替换。起点`19f394a593f67d19e6a6c6a0bc754c165581ad06`。

## 需求、检索与反例

产品`trajectory/src/Ptp.cpp::interpolateJointLinear`按`units/trajectory.md §7.3/7.4`采用逐轴凸组合，保留端点、零段、维度/参数前置及限位凸组合语义。当前无可依据的极值输入收窄授权。

沿用`reuse/functions/trajectory.json`中的`trajectory.interpolateJointLinear`、REUSE-T01的`ToolSuitabilityTest.cpp::LinearInterpolatorExtrapolatesAndCanOverflowFiniteEndpoints`及`probe-r7`原件，不因HEAD改变重跑未变的标量证据。本轮定向阅读：

- `RobWork/RobWork/src/rw/trajectory/LinearInterpolator.hpp::LinearInterpolator<T>`构造时计算`(end-start)/duration`，`x(t)`计算`_a*t+_b`，库目标`sdurw_trajectory`。
- 同目录`CMakeLists.txt`及`RobWork/RobWork/gtest/trajectory/LinearInterpolatorTest.cpp`是构建/测试复用入口；原检索命令及版本仍由上述功能行/catalog `TRAJ-LINEAR`绑定。
- `trajectory/include/sdurws/ird/trajectory/Ptp.hpp`和原实现说明产品定义域；真实Q模板路径的极值探测本轮 **NOT RUN**，不能把旧标量探测称为Q或生产迁移通过。

具体反例：`start=DBL_MAX,end=-DBL_MAX,duration=1,t=0.5`。现凸组合为0；上游先计算`end-start`溢出，结果非有限。仅增加s=0/1端点快路不能解决中点。该差异已被旧实验真实记录，不把它写成上游整体不可用。

## 最小候选与边界

1. **直接替换：当前否决。** 数学同名不代表浮点行为满足现有契约；不静默缩小输入、不保留未登记双核心fallback。
2. **优先继续检查现有上游稳定求值入口：待验证。** 仅沿插值器/Polynomial现成API查实际算式，验证Q、普通工程输入、极值、端点和零段；不能把另一个差分式入口当作问题已解决。
3. **如仍无适用入口，提交最小上游扩展设计：当前不实施。** 候选位置仍是原`LinearInterpolator`抽象的稳定凸组合求值入口，避免产品自建插值类。需明确端点存储/API及ABI影响、数值容差、构建/测试与许可证，再由所有者单独批准SA-02 patch；本提案不授权框架改动。
4. **产品新核心或未经验证的缩放算法：不批准。** 二分缩放可能丢次正规信息，不能沿用C4已经否决的推理或把其性能接受外推。

接口/owner均保持：trajectory负责路径几何，runtime保持模型真值，policy负责碰撞；不增加模型/状态/缓存。可独立核查Cartesian旋转slerp，但同文件其他功能准入仍须真实审计，不用拆文件绕过覆盖门禁。

## 后续退出与回滚

批准前仅做定向适用性验证，不修改Ptp.cpp或相关矩阵迁移状态。候选须有真实Q、独立插值期望、端点/零段/极值、直接消费者和性能证据；达到适用性且获得所需批准后才编制生产子包。现有Smooth/TimeParam成果不重写，T13未独验仍不放行IK/RNE。

本次为纯文档，无C++构建/性能/产品测试，均 **NOT RUN（生产未变）**；引用历史实验只限其原版本/类型/输入。回滚仅本提案、任务契约和空产品凭证，不影响已有生产或历史原件。未提交独立算法验收，不关闭WP-11。
