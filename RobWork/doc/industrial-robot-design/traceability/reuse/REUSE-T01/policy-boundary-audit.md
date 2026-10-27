# REUSE-T01 B18：policy 权威与上游会话边界

范围为 JointLimits.cpp、PolicyParsing.cpp、CollisionEvaluator.cpp、RobWorkCollisionEvaluator.cpp 的尚未登记定义，以及 CollisionQuery.cpp::buildEvaluationHalf。逐项区段见 batch18-definitions.json，实际六组检索见 batch18-search.json。沿用固定库存，不把这些定义或局部 PASS 当全项目覆盖。原产品、上游及 vcpkg 未修改。

## 可复用能力与保留语义

- `Models::inBounds` / `QConstraint::makeBounds` 是普通 Q 闭区间过滤候选。policy 仍拥有已发布策略、行程阈值、continuous 工程范围、近限位、有符号裕量、首命中与来源事实；不能用一个布尔过滤替代这些业务输出。有限关节位置先核非有限；区间仅核 `qmin<qmax`，不等同显式核双端有限，极值差/半宽溢出尚未运行，不登记为已复现缺陷。
- 解析两入口共用 `runParsePipeline`。单位经 `core::UnitToken` / `convert`，域窗经 `PolicyThreshold::make`；唯一冻结缺省 4π 经 `JointThresholds`，规范内容摘要经 `PolicyCodec::contentIdentity`，发布经 `EngineeringPolicySet::make`。角色/规范键/规则重复冲突是产品策略语义，标准 sort/unique/map/set 已复用；同词表与 codec 排序谓词仍有维护重复，不能因本批未改算法就宣布已消除。
- 上游 `ProximitySetup` / `ProximitySetupRule` / `ProximityStrategyRW` 被真实调用。policy 借持传入的 WC，使用 findDevice/findFrame/getObjects 只读定位；后端模型注册是会话缓存，不是 WC/DWC 结构发布。评估器共用后端与同一把锁，`buildEvaluationHalf` 的 hasModel/addModel 均持锁。直构多个会话共享后端但不共享锁时，缺省各建锁不足以自动证明安全；本批未实跑并发错误。
- 内置 `ProximityStrategyRW` 只继承 CollisionStrategy。`dynamic_cast<DistanceStrategy*>` 探测结果才是距离能力依据；描述符里的 `binary-collision+distance` 不是能力证明。不能把距离缺供静默变成无碰撞。
- 作用域先验证/展开冲突，再按域门控、必检、显式排除、相邻默认、Scene 默认不检，最后规范排序；Group 在解析期存在性应答通过，不表示会话拥有组成员数据，当前会话明确拒绝不可展开 Group。

## 权威与诊断承重边界

本批未见构建权威 WC/DWC 或另造 ResultEnvelope：会话借持编译产物，限位 `finalized` 是本评估 DTO 的完成标记。会话身份真实复用 core 摘要器；规范串长度仍手工写 u32BE，可核已登记 endian 候选，但不能改变既有身份组成。旧注释“SHA-256 不可能全零”不成立，不作为本审计保证。

policy 通过 `policyDiagCode` / `registryCode` 选择码，经 core `DiagnosticRecord::make` 组数据，但各调用点仍注入中文 context/cause/action，`domainWindowText` / `levelText` 也有本地文案。ARCH §3.6.3 明确 diagnostics 为稳定码/正式文案权威。因此这些行保留 pending，后续须追到 diagnostics 的正式模板、显示消费者和已有转换接口：合法结构化参数可保留，替代正式文案的部分收口原 diagnostics，不增第二诊断服务或让 adapter 拥有 envelope。此处是静态边界风险，不伪称已执行显示/诊断全链测试。

## F-961：完整名不是上游规则的字面参数

runtime 的合法字符集 `[A-Za-z0-9_.-]` 允许设备 `REGEXarm` 及完整帧名 `REGEXarm.link_1`。`ProximitySetupRule.cpp::convertToRegEx` 对长度大于 5 且前 5 字节为 `REGEX` 的串去掉该前缀，余串直接作为正则；普通名字则走 `StringUtil::patternToRegEx`，点号会转义。原 `buildPolicyProximitySetup` 直接传完整名，注释却声明精确匹配。

隔离审计目标通过原 `policy/CMakeLists.txt` 编译全部真实生产源，别名只把既有 `RW::sdurw_*` 导出目标接到原 CMake 的集成判断；没有重列生产源或复制实现。测试 include 原 `CollisionSessionTest.cpp` 复用其身份、发布、设备/名称替身，只在新增用例内换成合法 REGEXarm 设备和两个同前缀帧。真实生产 `createSession` 返回的相邻排除规则 `match(原名A,原名B)` 为 false，应为 true。测试只检查公开 `proximitySetup()` 产物，不运行运动学/几何查询。

VS2022 x64 全新配置/编译 exit0；过滤 `ReusePolicyBoundary.*` 实跑 **1 passed、1 failed、0 skipped、0 notRun**，进程 exit1。XML、原 testkit 原生报告、stdout/exit 及构建日志见 probe-r18；原会话测试虽被注册但未运行，不能混入本批通过计数。初探测后将设备和两帧前缀统一，并重建重跑，归档为最终一致场景证据。

影响边界：公开 setup 规则不能匹配所声明原名；当前 `evaluate` 按 scope 逐对调用后端，未消费这个 setup 做过滤，因此本反例**不证明实际碰撞漏检**。后续 T05 在原 adapter 中使用已有规则 API 作最小字面适配，并在原会话测试补 REGEX 前缀/普通点号/反向对回归；保持 runtime 名称真值，不重命名对象、不改上游、不建新规则引擎。适配方式须保留上游模式语义且通过名称权限审查。

## 本批退出边界

只登记实际审读定义和公开 API 差异，均 pending；无 grant、无 closed 文件、无新核心/上游扩展批准。全 policy 回归、Models::inBounds 实跑、真实碰撞/距离数值、生命周期/并发/性能、诊断正式显示链均 NOT RUN。B16 返工独立 PASS 只关闭 B16-P2-01；B17 局部独立 PASS 不改变全覆盖 NOT PASS。T01 全量阶段仍未退出。
