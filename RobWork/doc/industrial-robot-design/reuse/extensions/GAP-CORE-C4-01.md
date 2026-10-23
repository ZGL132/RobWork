# GAP-CORE-C4-01：C4 极值溢出修正提案

状态：所有者已明确批准下述有界范围（2026-10-23）；实施后仍须独立验收。

- **证据**：复用 [core-units-compare-audit](../core-units-compare-audit.md) 与 [B31 verification](../../traceability/reuse/REUSE-T01/batch31/verification.json)、[独立 Fraction oracle](../../traceability/reuse/REUSE-T01/batch31/oracle-result.json)。原探测22项19 passed/0 skipped/3 failed；不是新跑。当前 Compare.cpp 和其输入仍与矩阵绑定一致，REUSE-T13 范围诊断实际核查了这些绑定。合法输入 `value=DBL_MAX, reference=-DBL_MAX, relative=1.5, absolute=0` 应判 `2<=1.5` 为 false，现三个公开重载误判 true。
- **复用适用性**：B31 的源码/测试/示例/真实文档检索已经审读 Math、Q、Metric 等入口；`Q::operator==` 是精确相等，范数/对称近似不等于相对 reference 的逐元素 C4。`Math::isNaN` 不能拒 Inf。无证据表明有满足该产品非对称公式及量纲契约的等价入口；不能靠替换 epsilon/放大阈值消除缺陷。
- **申请 A（推荐，有界修正）**：只在 `core/src/Compare.cpp::closeWithin` 保留既有公式和正常有限运算路径，非有限拒绝用标准 `std::isfinite`；当有限输入的中间差/限值溢出时，用公共正尺度对原不等式做等价归一化再比较，避免 Inf<=Inf。复用标准 fabs/max/缩放原语，不增加比较器类、通用数学层或第二套公开 API；vector/Q 继续委托同一标量入口。具体缩放分支须先用独立精确 oracle 检查阈值两侧及舍入边界，不能仅覆盖一个反例后宣称全双精度精确。
- **不采用**：MSVC 的 long double 与 double 同宽；整体替换为范数会扭曲逐元素/参考方向；引入任意精度生产依赖成本过高且原核心只允许标准库与 sdurw_math；拒绝合法大值会收窄契约。
- **边界**：不改变 Tolerance::make、公开聚合字段、非法 tolerance 的既有约定或 C7 数字；不借此收窄公共 API。来源与诊断职责仍归原 owner；不修改上游/vcpkg。
- **文件/消费者**：Compare.cpp、原 CompareTest.cpp、已有 B31 oracle/夹具、units/core.md 对应实现说明、core 逐功能矩阵/凭证。消费者是所有使用 closeWithin/allCloseWithin 的基础与业务单元；公共函数/ABI不变。先沿同文件其他功能的现有审计确认并保留，不顺带改单位转换或业务阈值。
- **验收/回滚**：原15项＋三个红例；补同号/异号、0、DBL_MAX邻域、次正规数及 rel/abs 混合边界的独立 Fraction 对照；真实 Q 重载必须编入，受影响 core/runtime 契约回归及必要冒烟。阈值不放宽；性能只测现有正常路径与溢出分支的必要对比。一次独验后方可关闭 F-966。按独立完整能力提交回退，不保留双实现 fallback。

批准范围仅为上述产品既有 C4 的最小修正；不是批准新通用数值核心、上游扩展或改需求。若边界验证显示缩放方案不能满足原语义，保留 FAIL 并报告，不继续另写算法。

## 所有者授权

- 原提案规范内容 SHA-256：`d3a12d6599f297907ffa008692af1f255742dcac920b68fffe051f18ddf39176`。
- 所有者答复原文：**“批准方案A”**（本会话异步批准项，2026-10-23）。
- 已确认范围：方案 A：保持原 C4 公式、阈值及正常路径，仅修有限输入的中间溢出，使用标准缩放原语并以独立精确 oracle 验证。
- 不含未申请的新核心、上游修改或其他提案；实施者不得将自查冒充独立验收。

## 候选验证结论

方案 A 的缩放候选未通过精确边界验证，已撤回生产变更，F-966 保持 OPEN。原始失败与回归均见 [REUSE-T14](../../traceability/reuse/REUSE-T14/README.md)；本批准不自动转为其他算法授权。
