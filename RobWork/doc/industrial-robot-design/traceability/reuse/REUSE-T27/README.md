# REUSE-T27 / WP-12：惯量特征值直接复用

基线 `704667b598967d9849dd02f113dd28ec3ad93a8e`，`redesign-main` 本地串行实施。当前实施自查完成、可交独验；独验 **NOT RUN**，任务 ready，功能 migrated 非 closed，不关闭其他包债务。

## 需求、删除与保留

MDL-05／I-MDL-5：同一连杆参考系、同一参考点、单位 kg·m² 的六分量对称惯量，取升序主惯量；SPD 严格 λmin>0、三角严格 λmax≤λmid+λmin，无新 epsilon。删除 `InertiaMath.hpp::symmetricEigenvalues3x3` 的零阵/均值/平方和/归一行列式/三角解析根核心。原私有入口只做六分量→上游矩阵→三特征值投影，不增加 facade、缓存或模型层。

直接来源：`RobWork/RobWork/src/rw/math/LinearAlgebra.hpp/.cpp::LinearAlgebra::eigenDecompositionSymmetric`，既有 `sdurw_math`。`GeometryUtil.hpp::calculatePrincipalInertia` 内部也委托同一入口，额外求主轴/修反射并增加几何依赖，对本需求无收益，因此选择直接 math API；不修改上游/Eigen/vcpkg，不新增渠道或 CMake 目标。原上游 `gtest/math/LinearAlgebraTest.cpp` 已由 modeling_test 直接编译，本包实际运行 `LinAlgeTest.LinearAlgebraTest`，不复制测试源码。

数据流：RobotDesign／Parts／Import／CommandHandlers／PropertyEstimation／ModelingBodyEditor → 原 checkInertiaAssertions／synthesisSelfCheck → 原私有特征值入口 → 上游分解 → 原判断/素材返回。modeling 保持物性策略和输入值职责；runtime 仍唯一模型 owner。全部临时矩阵/分解对象仅本次调用持有，无共享可变状态、无私有 State 或 worker 指针传播。

保留原消费者、有限性/对称性检查、NotProvided 跳过、SPD/三角谓词、错误 token 和输出协议；不把生产数学搬到 core，不另建诊断体系。私有函数移除 noexcept：上游动态矩阵的分配失败向原调用方传播，避免 terminate；未改公共头/公共函数契约。分配失败注入与线程压力 **NOT RUN**，不声称通过。原文件“解析式保证跨平台位级”等过强注释已修正，当前工具链重复确定性由测试证明。

## 验证原件与复现

命令/输入/配置/依赖摘要见 [verification.json](verification.json)，生产正向证据见 [production.json](production.json)。测试用 VS2022 x64、绝对 exe，逐进程运行；QCoreApplication 模型测试，无 GUI 窗口系统。只构建 modeling_test／modeling_contract_test 及其实际依赖（含私有头消费者 plugin），不全库重配/重测。

| 验证 | 真实结果 | 原件 |
| --- | --- | --- |
| 旧生产＋新增独立用例 | FAIL：0 passed / 2 failed / 0 skipped；1e-300非各向同性误算平均值，大尺度溢出NaN/Inf | baseline.xml / baseline-report.json / baseline.log / baseline-build.log |
| 首次替换 | PASS：62 passed / 0 failed / 0 skipped | normal.xml / normal-report.json / normal.log / integration-build.log |
| 真实生产返回值扰动 | 预期FAIL：0 passed / 2 failed / 0 skipped；调用上游后丢弃结果返回三个0，原连杆物性测试和新增消费者测试失败 | disconnected.xml / disconnected.log / disconnected-report.json / InertiaMath.mutated.hpp |
| 恢复后第一次增量尝试 | FAIL：43 passed / 22 failed / 0 skipped；复制恢复保留旧时间戳，MSBuild跳过头消费者，仍为扰动对象；未宣称此构建证明正确恢复 | restore-stale.xml / restore-stale.log / restore-stale-report.json / restore-stale-build.log |
| 写回正确字节触发头依赖重编译后的最终集成 | PASS：受影响两目标构建exit0，65 passed / 0 failed / 0 skipped；增加手动物性编辑纯求值与canonical物性映射消费者 | final-build.log / final.xml / final-report.json / final.log |
| 独立正确性 | PASS：3正交基×4已知谱×8尺度=96张量；不调求解器生成期望，归一尺度后原1e-12容差，零/负根/重根/排序及重复确定性 | final.xml 中 KnownSpectrumAcrossScales_REUSE_T27 |
| 上游与严格策略边界 | PASS：真实上游结果对照；正定非对角极小/极大输入、零阵、SPD零根、三角等号/超界 | final.xml 中 UpstreamAndAssertionConsumers_REUSE_T27 |
| 对端契约 | PASS：8 passed / 0 failed / 0 skipped；描述读取/canonical编译桥/原peer流程，不是T13模型真值独验 | contract.xml / contract-report.json / contract.log |
| 既有独立冒烟树 | PASS：同一正确字节版本、原math导出供给及实际modeling消费者编译；未重配新树 | smoke-build.log |
| 架构 | PASS：11类零命中；未扩大permissions | architecture-gates.log |
| 独立验收 | NOT RUN：实施者不自签，由所有者另开会话或人工 | 尚无独立记录 |
| 分配失败注入、线程压力、整机性能/全部GUI及全库测试 | NOT RUN：本包未改公共API/共享状态/UI流程，不扩大无关验证 | 无 |

基线与扰动均是真实编译运行；恢复异常原件保留，不覆盖成PASS。最终源码与保存的正确副本逐字相同；最终构建日志包含受影响 .cpp 的实际重编译。契约原件复跑仍8项通过。原独立oracle/字面期望及物性公式表对照保留，不用求解器对照替代独立验证。

## 性能与接受

[performance.log](performance.log)：精确摘取基线/最终函数，MSVC /O2 /MD /DNDEBUG、100000次×4场景×3轮，交替先后；复现 [run-performance.cmd](run-performance.cmd)，旧核仅隔离 perf-old.cpp 比较，不进生产目标。

一般非对角旧0.0345–0.0404µs/次、新0.4656–0.4690µs，约慢11.6–13.5倍；近各向同性微扰旧0.0231–0.0357µs、新0.4233–0.5461µs。极小尺度旧误算、大尺度旧非有限，新路径正确，不能用旧错误路径速度作正确基线。没有宣称总体加速或零分配。所有者明确回复 **“接受本包具体开销并交独验”**，仅接受本次具体测量，非其他包预算、非独验PASS。

回滚本包完整提交（生产/测试/矩阵/catalog/凭证），不覆盖他人、不改历史、不留旧核生产fallback，不动框架或远程。历史单元卡修订日志是当时快照，本包现状以当前源码/凭证为准，不批量重写历史。

直接复用迁移：本轮 **1包已实施待独验**；必要配套修复：**0包新增**（极值正确性改善由直接复用同时获得）；独立缺陷：**0包新增**。T26仍待独验；T13未验仍不放行普通IK/RNE。本包不批准其他runtime/selection惯量候选，它们原矩阵保持pending。
