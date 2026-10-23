本能力将 kinematics FK/Jacobian 计算核迁移为消费 RobWork 上游真实计算，删除自建实现。
精确契约：[REUSE-T19](../tasks/REUSE-T19.json)（编号映射：REUSE-T19（编号映射：本包实施期登记为 REUSE-T18，因主线同号〔F-958 testkit 监听器收口〕冲突，经协调流程改登记为 REUSE-T19；原验收受检 SHA 20522f776/434500c43 与用例标识保留不变））。实施基于已独验的 REUSE-T17 锚能力；复用基础包 REUSE-T13 独验未过＝本包实施待独验，不自行关闭、不合入。
生产正向证据四面（normal/oracle/upstreamComparison/disconnected）：[REUSE-T19/production](../traceability/reuse/REUSE-T19/production/kinematics.FkEvaluator.evaluate.json)。

| 功能/位置 | 复用来源 | 适配边界 |
| --- | --- | --- |
| `Fk.cpp::FkEvaluator::evaluate` 第4-7步 | `rw::models::Device::setQ/baseTframe/baseJframe`（经 T17 锚→编译产物 WorkCell＋任务私有 State） | 锚获取/设备查找/DOF 防御；q_rw＝q_auth−zeroOffset（src/JointMapping.hpp，与编译链烘焙互逆）；命名 TCP＝法兰位姿·偏置一次复合（KIN-14 消费侧语义，A-15 实际键优先）；命名 TCP 雅可比参考点移轴 v_tcp＝v_flange＋ω×r；行义（0-2 线/3-5 角）与列序（设备活动关节＝可动链序）经上游源码核对 |
| `Fk.cpp` 奇异值 | `rw::math::LinearAlgebra::svd`（Eigen JacobiSVD 内核，降序） | sigma→容器拷贝；Eigen 直调 SVD 与"rw::math 无 SVD"旧注释删除（GOV-T21 勘误兑现）；O-40 依赖保留（Ik.cpp 直用面不变） |
| 手写链式 FK/几何雅可比/toEigen·toRw 转换 | 删除（H2 禁平行实现） | — |
| TCP 三态解析/错误面/条件数/可操作度/裕量/IRDPM01 编码 | 保留产品实现（逐字节） | 锚缺失/设备未命中→NoDevice 值面（§9.2 @pre 运行期承载）；4 错误码面不变 |

输入仍是权威 q（§5.1 调用方直入）；基座—世界组合唯一经 runtime::composeWorldBaseTcp（M-11/AT-37，不变）。
测试面：夹具集成模式经 RuntimeSnapshotFactory 真实编译（S1–S5 接口文档自载的"测试替身注入点"＋S6–S10 全产品路径）；FK 用例/黄金 FK/关节模式用例经 IRD_HAVE_RUNTIME_COMPILE 门控不编入冒烟树（CollisionPortTest 同款 gating）；两模式链接按 REUSE-T07/T12 先例（集成真实 sdurw_* 目标/冒烟 RW:: 导出包，绝不源码复制/备用算法）。
语义变化一处：同几何双模型（zeroOffset 0/0.5）的跨模型字节级断言改为附录 D 第 9 项容差断言——偏置映射相消后浮点复合路径不同；消费语义与单模型"同输入同字节"确定性不变（bitwise 用例照旧通过）。
适用性限定为本能力的 6×n 基础雅可比、SerialDevice 链、0-DOF 退化面与命名/默认 TCP 选择；不声称 IK/DLS 已迁移。
