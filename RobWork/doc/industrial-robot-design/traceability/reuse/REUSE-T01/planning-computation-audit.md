# REUSE-T01：规划、优化、包络与功率统计审计

本批定向补COV-01～05，不宣称覆盖审查其他缺口或全项目完成。逐功能事实唯一见functions矩阵，本说明解释候选排除、适配边界和探测限制。完整定义区段已逐项核对返回类型与实际作用域，非根据调用点登记。产品与上游未改。

## 现有规划链

Planner.cpp实用runtime::WorkCellConstView::findDevice取const SerialDevice，零WorkCell构造/写入；PlannerSearchRequest仅借指针，调用期锚点仍由装配者保证，尚未追踪全UI宿主入口。PolicySessionQConstraint每次调用policy会话，只有Completed+Applicable+finalized可采信；缺口保守拒绝，SafetyMarginViolation不同于Collision。RRTPlanner消费makeBounds/makeMerged、QEdgeConstraint、QSampler和L2 metric，query返回的路径经过端点守卫和PathSequence复核，再进入候选选择与段构造；路径不是丢弃上游结果后的自算输出。该TU仅集成源集编译，冒烟不含，不能凭冒烟声称RRT交付成功。

sampleStraightPath已委托本域唯一interpolateJointLinear；剩余长度平方和可复用L2，均匀参数/端点/百万段上限是现有采样协议，不以QEdgeConstraint替掉对policy的整路查询。maxAdjacentStep可逐相邻对用Linf；pathJointLength以及plan内部两段相同L1累加可用PathAnalyzer::pathLength+Manhattan。规划器L2与候选选择L1不是同口径，源码注释泛称同源需后续修正。上游pathLength先段内归约，现有逐轴累加，浮点舍入可能改变近并列选择，迁移需数值/身份/稳定序回归；不能仅凭10=10探测批准位级替换。minNormalizedMargin按产品评价区间归一，不由欧氏距离替代。

Random.cpp持进程全局mt19937，QSampler::BoundsSampler逐次调用Math::ranQ；真实API探测确认不同sampler重播种互扰，64位高位在unsigned投影后碰撞。现注释把并发互斥交装配面，但本轮未证明跨任务/其他随机消费者隔离，保留pending，不把每实例SingleThread当进程全局锁。StopCriteria已有stopByFun+stopEither+stopAfter，可薄接取消和预算，不需要自写RRT循环；当前产品query仅传remainingS、约束中转发取消，T05再核取消实效与预算，不把API探测当生产断开实验。

makeAvoidFailure只组装TRJ payload；其cause/recommendedAction现带中文文本，需在T05按diagnostics稳定文案权威核对，不把本域载荷当evidence envelope或任务判决。搜索未果保持DataInsufficient素材；禁改为不可行。

## 优化链

Pareto的slotValue/notWorse/strictlyBetter/dominates/buildFront逐一定义审读。比较消费现有core::closeWithin（参考元b），min/max方向来自本域词表，去重保首次、rank及目标序/CandidateId排序由产品身份契约限定。上游sandbox Optimizer明确Multi-Input Single-Output，Types的ResultType为double；pathoptimization只改一条几何路径，不是多目标候选支配/排名器。本次检索没有找到可直接替换整个Pareto前沿的上游实现，但不据此批准保留或新写通用排序核；T05先确认业务规则与core比较共享边界、测试黄金独立性，再给最终决策。容差支配非严格偏序的可能循环须另核，不能沿注释“恒有非空front”当证明。

Generator.cpp的seeded-lhs已经委托EvaluatorPorts::generateSeededLhsCandidates；旧PRNG/LHS审计不因该薄消费而关闭。baseline-perturb是授权/锁定过滤、bindingId排序、baseline恒首、OFAT正后负、合法邻点/预算与补丁构造；QSampler均匀随机或单目标Optimizer不等价于这些枚举/身份规则。不可为去重引入通用优化器平台。EvaluatorPorts.cpp真实消费生成批（771）、Quick前沿（828）与Verified前沿（958）；本轮绑定这些输入供追溯，没有宣称整个编排器已审完。

## 包络与能量

Envelope.cpp探针组装PositionOnly IK，消费原solver结果（取消/设施失败优先），probeSerial只在实际IK调用推进；compute做方向采样、解析外界、等距内扫/外段二分与近似标注。AABB/几何包围盒无IK存在性，不能替代可达集合估计。优先复用后续上游IK适配端口；64位种子派生、方向冻结序、未找到≠不可达、NotRun/计数守恒/近似非Must判定保留。尚未发现测试之外的生产调用，保留接线待核，不造新workspace solver。

PowerEnergy直接消费mechanicalPower而非重复tau*qdot，逐关节时间序、无效行缺口零贡献、正负分项梯形、首峰/等值窗、无时间NaN为产品统计规则。RigidBody::calcEnergy算刚体动势能，BodyIntegrator推进状态，都不等价。新定位TrapMethod真实提供固定h积分；M=2可逐区间调用，两个区间1s/2s探测通过，但算法表达式(h/2)*(f0+f1)和产品0.5*(f0+f1)*h的溢出/舍入不同。目标sdurw_softbody当前默认关闭，审计模板可由源码include得到不代表产品可直接依赖；T05比较最小供给/适配与收益，不启用整个软体模块、不复制模板、不声称没有可复用积分。峰值/缺口/身份部分保持业务编排，不套calcEnergy。

## 验证与后续

本批新增4项公开API：路径L1/Linf/L2、StopCriteria回调、全局采样互扰、两点梯形，实跑4 passed，failed/skipped/notRun=0；旧26项本轮未重跑。首轮误把QPath当initializer_list容器导致构建失败，改用上游push_back后构建/运行成功，初失败原件保留。它们不是生产迁移正向/断开/独立oracle验收。新增关系保持pending；后续按T02～T05、独立验收及必要批准落实，不清库存、不扩大对象权限，不以本批宣称T01退出。
