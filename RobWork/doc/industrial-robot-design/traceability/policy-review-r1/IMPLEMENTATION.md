# POL-T13 — policy 首轮评审整改

用户在专项评审后要求“按照建议进行修改”。本轮在所有者指定的 C 盘工作树 `codex/core-review-fixes` 实施，起点 `59af1f9b449ee23fb78dfe547b3a7339df9d325d`；不操作远程、不派发子代理、不触碰框架或 vcpkg。该记录为实施与送验依据，不是独立验收结论。

期间另一会话提交 PRJ-T19 验收登记 `b8602fb00`、`fd198f54c`，未改变本包产品输入。保留两提交，本包范围核验基点使用 `fd198f54c0b66bc61dd64b29a04e2b1675f42dbc`，不将他会话登记纳入本包允许面。既有 permissions 仅刷新实际受影响来源/构建摘要，grant 的目标、类别、操作、来源及生命周期权限均不扩大。

## 取舍与职责

- 保留：上游 `ProximityStrategyRW`、`CollisionStrategy::inCollision`、`DistanceStrategy::distance`、`ProximityStrategy::addModel(Frame, Geometry, bool)`；policy 的作用域、策略和诊断契约；runtime 的模型、名称表和快照所有权；既有 kinematics 场景投影与适配器。
- 删除：runtime 的 `RuntimeCollisionSession`、规则副本、专属测试与直接 proximity/PQP 链接；其唯一生产消费者原为 UI 运动学通道。宿主不再投影一份 Object-only 规则。
- 适配：单一 evaluator 只持描述符，各会话新建上游后端和锁；UI 只以共享快照的只读 WorkCell 别名及名称映射实现既有场景源端口，复用 `kinematics::makeCollisionSession`。policy 不成为模型 owner，宿主不构造模型。
- 产品业务：输入词表/有限性、生命周期、几何覆盖、必检/排除、比较型限位结果和正式诊断注册。上游提供几何计算，产品保留工程判定与证据约束；不引入新算法、新核心或上游扩展。

定向检索：`rg -n 'addModel|hasModel' RobWork/RobWork/src/rw/proximity/ProximityStrategy.cpp`、`rg -n 'FrameMap|getID' RobWork/RobWork/src/rw/kinematics/FrameMap.hpp`、`rg -n 'RuntimeCollisionSession|RuntimeCollisionRules' RobWork/RobWorkStudio/src/rwslibs/industrialrobot`、`rg -n 'makeCollisionSession|PolicyCollisionSessionAdapter|assembleCollisionScene' RobWork/RobWorkStudio/src/rwslibs/industrialrobot/kinematics`。上游逐几何重载已返回真实接收状态，不需要修改框架；Object 重载创建模型壳并不保证所有几何被接收。

## 实施范围

| 评审项 | 结果与正式回归 |
| --- | --- |
| F01 共享后端串场景 | 会话独立后端；两独立 WorkCell 的相同 Frame ID、小盒分离/大盒相交解析对照 |
| F02 宿主规则旁路 | 同步/覆盖/单点/批量共用 policy 装配；删除重复设施；真实发布快照验证域、相邻默认、Object/Role 必检与距离缺能力 |
| F03 非有限 q/path 参数 | 全样本先验扫描，Failed 且不 finalized；含 NaN/Inf 回归 |
| F04 运行中失效 | 样本边界及终态化前复核 alive；查询中失效替身回归 |
| F05 无效几何冒充覆盖 | 逐几何检查接收，空/部分拒收不计完整覆盖；真实空 mesh 回归 |
| F06 非有限限位端点 | IntervalInvalid 硬结构发现与诊断；不转为 TravelLimitExceeded；处置权仍归 modeling/project |
| F07 行程/半宽溢出下溢 | 宽度与派生值有限性检查；用 `(margin / width) * 2` 避免半宽下溢；极宽、次正规端点/中点回归 |
| F08 非法碰撞域 | 解析与直接字段工厂复用同一词表检查，拒绝表外枚举 |
| F09 非法查询 kind | QueryInvalid fail-fast |
| F10 首个间距发现丢摘要 | 短路前累计已测最小值；脚本距离仅验证协议，不冒充真实距离后端 |
| F11 coverage.excluded 空缺 | 与 appliedFilters 同源、同序赋值 |
| F12 诊断注册差集 | 正式注册缺失六码；工程范围输出沿既有正式拼写；逐生产码经正式 registry/factory 创建 |

接入影响补强：kinematics 适配器只有无碰撞发现且几何/实检覆盖完整时才返回 Evaluated；覆盖不足保持 EvidenceMissing。已发现的碰撞仍可供淘汰构型。该补强是本轮切换真实入口的必要消费者约束。

## 验证口径与未决范围

红例：正式 policy 首批 13 个用例为 1 passed、0 skipped、12 failed；正式诊断注册契约 0 passed、0 skipped、1 failed，原始日志/XML/JSON 随本记录保存。错误拼写测试首次编译错误已修正并保留构建原始日志，不算红例成功。

正式收口结果见同目录验证记录与原始日志；只构建受影响目标和直接消费者，并运行独立冒烟、数值边界与门禁。验收需所有者在送验 SHA 另开会话执行，实施者不自行签署。

Group 成员 schema、真实距离后端、Workpiece/Payload 完整角色供给、bool 严格字节编码等仍待原裁决，不在本轮发明契约。runtime 的资源/呈现代理供给不属于本轮新实现；宿主回归使用无碰撞几何的发布快照验证缺证，不能替代完整场景的 GUI 人工点验。现有矩阵中的 pending/proposal 保留，摘要/行号同步不代表关闭旧债务。
