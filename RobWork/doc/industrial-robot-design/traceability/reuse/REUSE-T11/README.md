# REUSE-T11：modeling 旋转薄适配

业务需求：URDF/Origin 编辑继续接受 `{roll,pitch,yaw}`（rad），采用 `Rz(yaw) Ry(pitch) Rx(roll)`；基座欧拉编辑只存既有 EAA 参数，最终矩阵由 runtime 唯一形成。保留有限性/Custom 身份/原子拒绝、平移 m、预设及 codec 语义，不保存重复模型或矩阵。

复用点：`RobWork/src/rw/math/RPY.cpp::toRotation3D`（构造参数 yaw,pitch,roll），`EAA.cpp::EAA(Rotation3D)` 与 `EAA.hpp::toVector3D`，目标 `sdurw_math`。已有 modeling 数学链接无需修改。数学值类型按 ARCH §3.6.1 使用，不构造 WorkCell/State。

删除：RpyMath.hpp 内九项矩阵展开、半角四元数组合/归一化/atan2核及其头依赖。保留两处原私有函数名，理由是多个现有导入/字段编辑消费者需要同一换序边界；不新增 facade、DTO 或缓存。上游直接满足转换，产品仅换序；Quaternion::ln 已有零角非有限反例，未采用。EAA 沿原库，补丁已撤销；REUSE-T10 原库12例和3156输入/入口网格可复用，仅相关表示转换按已批准1e-6最大矩阵元素误差，普通RPY正向/合法性/物理量阈值不变。

实施顺序：替换两核 → 增量重建真实 modeling 消费者 → 原业务/错误/codec回归与边界oracle → 真实生产结果断开 → 恢复再构建 → 文档/矩阵/凭证 → 一次独验。回滚仅 revert 本能力提交，不恢复EAA补丁，不涉及并行专属范围。

实测结果：集成受影响目标构建通过；modeling 474通过、契约17通过，均0失败/跳过。只修改既有 EulerCompilesAndCodecRoundTrips 中的局部表示往返阈值；原1e-12下的小角反例最大差约1.0e-11，保留原始失败日志。普通RPY正向、预设、矩阵合法性、非法/歧义输入及原子拒绝断言未改。

CONFIG消费探针：1140固定输入（零、微小角、pi邻域、万向锁、超一周、一般网格），独立ZYX/Rodrigues、明确上游API对照及重复输入确定性通过；2项通过、0失败/跳过。最大普通RPY误差0，EAA回构最大9.996e-8。前后同输入Release微基准约0.053→0.032微秒/次（单次测量，非端到端保证），未观察退化；基准中的旧头仅由Git冻结0032c8e78导出至.cache供测试，不进入生产或回退。

真实结果断开：仅测试时将上游RPY计算结果丢弃、返回I，重建实际modeling库及既有测试；Origin编辑与基座欧拉编辑2项断言失败，exit1；恢复源码、刷新时间戳并真实重编后2项通过。首次Copy-Item保留旧时间戳导致MSBuild未重编的两项失败日志保留为环境诊断，不计为恢复通过；最终以restored-real原件为准。Disconnect.ps1已修正恢复时间戳，未修改生产构建规则。

BasePlacementEditTest中的fixture/断言并非生产核心：工厂创建、codec解码、字段映射、原子事务/三预设/Custom身份/单位与来源断言均沿既有GTest/testkit/runtime公共接口；importDigest只是测试字节模式，不实现摘要算法；手抄矩阵/ZYX仅为独立oracle，不改为调用被测核。其功能矩阵只登记测试职责，不批准新的数学实现。临时fixture复制优化不阻塞本次数值替换。

旧EAA补丁保持撤销；上游原12例及原库网格引用REUSE-T10，不重复运行。无CMake/依赖/公开接口变化，CONFIG值类型消费探针作为相关链接冒烟，不重配全库/重跑152治理夹具/历史深查。只声明本能力实施验证；一次独验和全项目债务关闭仍以各自真实记录为准。

本包真实断开报告暴露机器误报：gtest 的 `failures` 统计失败用例数，旧门禁却统计每个失败断言节点，导致一个用例多断言失败时误拒绝。仅将 XPath 改为统计含 failure 的 testcase；错误/跳过/空报告/失败数不符/正常轨失败仍拒绝。真实 helper 的针对性正负例8通过、0失败/跳过，不为这一条规则重跑无关全152例。
