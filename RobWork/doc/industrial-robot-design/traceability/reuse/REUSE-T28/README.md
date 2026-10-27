# WP-12 / REUSE-T28：惯量旋转与长方体惯量复用

起点 `4794f64476945d3b8cf6974bb3e778b6ba419f95`，redesign-main 本地串行。所属 migration §13 **WP-12**，DTB 交付字段 **WP-E**。本轮 **2项直接迁移、1项必要配套修复、0项独立缺陷**，实施自查完成、独立验收 **NOT RUN**，任务 ready、功能 migrated 非 closed。没有框架/vcpkg/公共接口/CMake/permissions 变更，没有远程操作。

## 删除、保留与数据流

- 删除 PropertyEstimation.cpp::accumulateRotatedInertia 的手写 R·diag(I)·Rᵀ 核及调用包装；在原 PropertyEstimator::estimateLink 直接消费 `Rotation3D * InertiaMatrix * inverse(Rotation3D)` 的完整九项。
- 删除长方体三条主惯量公式，原分支直接消费 `InertiaMatrix<double>::makeCuboidInertia(m,a,b,L)` 三对角；原质量/密度/尺寸检查保留。来源 `RobWork/RobWork/src/rw/math/InertiaMatrix.hpp` 及 `Rotation3D.hpp`，原 `sdurw_math` 目标。不建 facade、中间公共类型或缓存。
- 保留圆柱闭式、平行轴、材料表、结果及 provenance、失败 token、确认流。实读 GeometryUtil.hpp/.cpp::estimateInertia(COG)：输入几何/TriMesh/可选 Frame/State，重估几何质量积分，不接受既有任意六分量张量和迁移位移；Body.hpp::setMass 只是存质量/惯量/质心并发事件，不执行平行轴变换；InertiaMatrix 公共工厂没有圆柱/空心圆柱或任意张量平移。DynamicUtil.hpp 无对应公开转换入口。定向检索原件 upstream-search.log。本轮不新增这些保留公式或宣称库永远无对应能力；已存在产品公式保留，后续若发现等义公开入口再迁移。
- `estimateErrorCodeToken` / `centroidEditResolutionToken` 是既有产品枚举与内部稳定 token 映射；`defaultMaterialDensity` 是单元卡固定5材料默认表，不能替成上游几何估算或新诊断轨；`formulaVersion` 返回原产品公式版本；`applyCentroidEdit` 保留显式迁移/覆盖/拒绝、先计算后写入及来源标记。这些是产品规则，非上游通用核心。SegmentInertia 仅原函数栈上的每段物性暂存，不保存模型/State。
- 数据流：原段元/密度解析 → 上游局部惯量/旋转 → 原段序累加与平行轴 → 唯一 synthesisSelfCheck → 原结果/来源 → 原编辑和建模消费者。modeling 只持物性值与业务规则，runtime 模型 owner、diagnostics 文案权威、evidence 结果契约均不变。临时数学值在调用内持有，无共享可变状态/新增任务会话。

必要配套：REQUIREMENTS 附录D第6项与公共注释规定相对1e-12，原实现绝对比较会在同一大尺度张量误拒旧/新核。所有者已确认 [symmetry-gap.md](symmetry-gap.md) 方案A；只修原 InertiaMath.hpp::synthesisSelfCheck 的有限扫描/相对检查，九项最大绝对元素作尺度，先归一后相减，零阵交原严格SPD，不以1作下限、不放宽1e-12。SPD、三角判据、token和签名不变。T27 分解函数体不变，原T27证据不改写；本轮 carry-forward-eigen.json 绑定当前头/消费者及真实新扰动。

## 完整文件审计与验证

功能矩阵逐项记录生产文件所有函数/局部暂存、头3函数、测试及lambda；声明区只含include/注释/namespace等无体内容。独立字面oracle保留在原测试，不移入生产、不用上游互证替代。

| 检查 | 结果与原件 |
| --- | --- |
| 旧实现＋新增旋转用例 | FAIL：1 passed /1 failed /0 skipped；baseline.xml/log/report，真实大尺度误拒 |
| 上游替换但仍绝对自检 | FAIL：22 passed /1 failed /0 skipped；candidate-absolute.xml/log/report，同一误拒 |
| 最终受影响集成 | PASS：68 passed /0 failed /0 skipped；final.xml/log/report、final-build.log；含T27已知谱、上游与原物性/编辑/导入/命令/canonical消费者 |
| 对端契约 | PASS：8 passed /0 failed /0 skipped；contract.xml/log/report；不构成T13模型真值独验 |
| 独立oracle | PASS：3有理旋转×3尺度字面张量、混合偏置段质量/质心/惯量、重复确定性；相对阈值正负内外、闭边界、1e-300/1e300/零阵/非有限/有限反号极值；见final.xml中的MdlInertiaTransformReuse |
| 旋转上游返回值丢弃 | 预期FAIL：0 passed /2 failed /0 skipped；rotated-disconnected.*、rotated-mutated.cpp |
| 长方体工厂返回值丢弃 | 预期FAIL：1 passed /1 failed /0 skipped；cuboid-disconnected.*、cuboid-mutated.cpp；圆柱用例不受长方体扰动，保留其PASS |
| 当前头上游特征值返回值丢弃 | 预期FAIL：0 passed /2 failed /0 skipped；eigen-disconnected.*、eigen-mutated.hpp；随后正确字节写回、受影响重编译并执行上述最终68/8 |
| 既有独立冒烟树 | PASS：smoke-build.log，只构建modeling，不重配全库 |
| 架构门禁 | PASS：architecture-gates.log，11类零命中 |
| 独立验收 | NOT RUN：由所有者另开会话或人工，不由实施者自签 |
| 整机/GUI性能、线程压力、分配失败注入、不受影响模块 | NOT RUN：未改变这些生产链/共享状态，不扩大无关验证 |

两条旋转/工厂扰动执行于相对自检修正前，PropertyEstimation.cpp 与当前完全相同，选定正常规模输入在新旧相对策略等义；丢结果后零阵仍被严格SPD拒收，因此适用，不伪称修正后执行。当前头的分解扰动另行真实执行。首次启动该扰动的cmd斜杠导致启动失败、未执行测试；修正为Windows路径后实际运行2项断言失败，不将启动失败当断开成功。命令与源码/依赖/配置/输入绑定见 verification.json，各生产正向报告引用同一次最终运行。不重复T27历史基线实验，不洗其历史失败记录。

## 性能与所有者决定

MSVC /O2 /MD /DNDEBUG、100万次×3轮、独立TU防消去、交替old/new；performance.log scene0/1/2=旋转、scene3=工厂、scene4=含真实分解的完整自检；旧核心仅隔离证据，不进产品目标。kernel-performance.log 保留最初已获接受原件。

1. 所有者明确 **“接受本包已测旋转内核开销，最终回归后交独验”**：一般/稠密旧0.00940–0.01006µs、新0.01938–0.02057µs，约增0.01µs；长方体工厂旧0.00257–0.00280、新0.00154–0.00172µs。
2. 所有者明确 **“确认方案 A，完成相对自检和定向回归”**：确认九项最大绝对元素的归一尺度，未提高固定1e-12。
3. 所有者明确 **“接受本次相对自检开销，交独验”**：scene4旧0.47618–0.49412µs、新0.50180–0.53671µs，同轮慢5.4%–8.6%、增0.026–0.043µs。

接受仅针对本包具体测量，不是其他包预算或独验PASS。完整整机性能NOT RUN。

## 剩余与回滚

WP-12仍余 BaseWorldTransform/DescriptionValidator 按实际owner核对、DH转换及其他物性候选，按真实依赖继续；本包不关闭整个WP。T27/T28独验仍待安排，T13模型真值无独立PASS前仍不放行普通IK/RNE。T27任务字段改为正确DTB归属WP-E并注明串行WP-12，既有ready状态不改；不新建台账。

回滚完整T28提交（生产/测试/矩阵/catalog/凭证），恢复其起点；不留旧核fallback，不覆盖他人，不动上游/模型owner或远程。
