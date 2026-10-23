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

## 修订提案 R2（2026-10-23，待所有者批准）

- **缺口与反例（不变）**：合法输入 `value=DBL_MAX, reference=-DBL_MAX, relative=1.5, absolute=0` 精确 C4 应 false，现三个公开重载因 `diff`、`limit` 同溢为 `+Inf` 误判 true（F-966 OPEN）。另存在对称的漏报族（`value=-DBL_MAX, reference=+DBL_MAX, relative≤2` 精确 true 仍返回 true/或视 absolute 误判），修订分支同时覆盖。
- **原方案 A 失败原因**：`std::scalbn` 同除后两侧**各自**舍入（减法、乘加各损一比特），在精确裕度 ≤1 ulp 的边界上舍入相撞——oracle 13,168 组中 6 组溢出边界误绿。教训：该分支必须以**无舍入**的精确和表示差值，不能比较两个各自舍入的量。
- **需求语义边界（B3 核对结论）**：原批准确定——正常路径保持既有浮点表达式舍入行为（T14 记录 18 组正常路径舍入差异属既定口径，本轮不修）；仅"diff 与 limit 同时为 +Inf"分支按输入 binary64 的**精确实数**语义判定。非有限 v/r → false、零参考退化、`Tolerance::make` 契约、C7、公开签名/ABI/noexcept 均不变。不采用范数、对称近似、放大 epsilon、任意精度生产依赖（原"不采用"清单全部继续有效）。
- **上游与复用检索（B4）**：rw/rwsim 无同语义入口——B31 检索继续有效，本轮补充 `rg "std::fma|closeEnough|fuzzyEqual|robustCompare"` 于 `RobWork/src/rw`、`RobWorkSim/src` 零命中；`LinearAlgebra.hpp:190/211` 的 epsilon 默认参数是对称正交检查，与 C4 非对称逐元素公式不等价（名称相似≠公式等价）。承担工作的标准原语：`std::isfinite/isinf/scalbn/fma/ilogb/ldexp`（C++11 起，MSVC 正确舍入）＋Knuth TwoSum（错误免费变换，非新算术框架）。Boost.Multiprecision 仅作实验 oracle（cpp_int 精确对照），不进产品（core 白名单＝标准库+sdurw_math；FNV 先例不构成 C4 依赖授权）。
- **推荐候选 R2 公式**：仅溢出分支内，判 `A ≤ B`（A=|v|+|r|，B=rel·|r|+a）。两侧除 2（精确）：`A/2=(x+y)`（TwoSum 叶子）＋`rel·y=p+ep`（fma 2Product 残差，精确）＋`a/2`（正规数精确）→ 五叶子精确和 `D=A/2−rel·y−a/2`，`irdExactSign` 合并最大两项（TwoSum 保持"叶子精确和＝D"不变式）至 |主导项| 超过其余项保守上界（上界只放大不缩小），判 `D ≤ 0`。捷径：`a=+Inf→true`；`p=+Inf→true`（引理2：`rel·ar>2·DBL_MAX≥A`）。伪代码与生产补丁形态见 [../traceability/reuse/REUSE-T14/revision-candidate/candidate.patch](../../traceability/reuse/REUSE-T14/revision-candidate/candidate.patch)。
- **正常路径**：不改变。补丁结构＝非分支提前返回原表达式；实验以"候选==原实现逐位一致"为硬断言，29,072 组零违规（含非有限域）。
- **精度/复杂度/依赖/异常风险**：精度＝分支内精确实数语义，唯一记载偏差为次正规绝对项 halving ≤2^-1075（触达需次正规绝对项＋~2^-1000 有理对齐同时构造，记录在案失败边界 2^918–2^970 裕度均不受影响）；复杂度＝分支内折叠循环实测 ≤5 轮（上限 256 轮含量子耗尽论证 4 倍余量，哨兵不可达则 fail-fast 取 false 以可见误拒暴露）；依赖＝仅 <cmath>/<limits> 标准头，零新链接；异常＝全 noexcept 保持，fma 正确舍入由标准保证、不依赖 /arch。风险：引理1–4（异号性/减半精确/2Product 残差/量子耗尽终止）的正式证明列为独验义务；无 fma 硬件时 std::fma 走库实现仅降速不改语义。
- **候选实验原件**：[../traceability/reuse/REUSE-T14/revision-candidate/](../../traceability/reuse/REUSE-T14/revision-candidate/README.md)——29,072 组（与 oracle.py 同源 3,168 网格＋CPython MT 逐位复刻 10,000＋对抗 15,840＋全部 24 组历史失配原件＋非有限/±0 专项），cpp_int 精确对照：分支内 10,708 组零失配、原误绿纠正 2,524、不变式零违规、两次复跑逐位一致（PASS，exit 0）。实验含两次已记录的失败迭代（候选 a 项未减半 1,147 失配；探针 ulp 生成 Inf 假失配），最终形态修复并通过；失败样本未删除，过程见其 README。
- **生产实施预期（获批后）**：仅 `core/src/Compare.cpp`（补丁即候选 R2 全部产品改动）＋`core/test/CompareTest.cpp`（沿用被拒候选补丁中的 FiniteOverflowBoundary/SubnormalAndSignedZero/Q-overflow 断言，全部保留原阈值）＋`units/core.md` §4.5 实现说明一句＋core 逐功能矩阵/凭证受影响行。消费者：全部 closeWithin/allCloseWithin（vector/Q 委托同一标量入口）调用方，公共签名/ABI 不变；受影响 core/runtime 契约回归＋必要冒烟，阈值不放宽。
- **回滚**：整包单提交回退（产品补丁＋测试＋矩阵同回退），不保留双实现 fallback。
- **本次申请批准的确切范围**：仅批准按 candidate.patch 实施上述溢出分支修正（含其引理义务与既定验证范围），不批准新数值核心、通用补偿算术、Boost 进产品、上游修改、正常路径语义变更；F-966 在独立验收通过前保持 OPEN。

## 待决风险（2026-10-23 复核会话澄清；不改变上述提案范围，不继续扩写候选）

1. **"是否构成新数值核心"是语义问题**：TwoSum 折叠＋irdExactSign 是单函数有界判定例程（约 40 行，无通用算术 API、不可复用于其他运算），不属于禁止条款所指"通用浮点算术框架"；但其验证义务按数值核心标准执行（引理审查＋oracle 对照＋边界语料）。
2. **"分支精确"表述须修正**：次正规 absolute 减半舍入（≤2^-1075 绝对偏差）是分支内的真实非精确点，准确表述为"分支精确，唯一记载例外为次正规绝对项减半"。
3. **256 轮哨兵返回 false 是保守误拒风险，不是已证明的 fail-fast**：fail-fast 是设计意图（可见误拒优于静默误绿）；其不可达性目前由引理4论证骨架＋实验零触发（29,072 组）支撑，"已证明不可达"未达成。独验须裁定哨兵语义（误拒 vs 拒绝服务）。
4. **引理状态分列**：引理1（异号性与减半精确）、引理2（a=+Inf/p=Inf 捷径）论证完整但未经独立审查；引理3（2Product 残差精确）为标准定理引用、条件已核对；**引理4（量子耗尽终止）仅为论证骨架，未成严格证明**。四条均列入独验义务，无一已独立证明。
5. **中间失败只有文字过程记录**：两次失败迭代（候选 a 项未减半 1,147 失配；探针 ulp 生成 Inf 假失配）发生在最终形态前的快速迭代中，中间版本探针输出未逐版保留，仅存终态 result.json 与 README 文字记载——不得声称"原始失败日志全部保留"。
6. **独立性/样本/适用范围**：oracle 为 cpp_int 独立精确参考（实验域）；29,072 组为有限样本；分支全量语义由引理承担而非样本承担——三者已分列，但 **CPython MT 复刻的正确性未与真实 Python 交叉验证**（本机无 Python；确定性网格 3,168 组与对抗枚举 15,840 组不受影响），登记为待验证假设。原方案 A 批准不自动覆盖 R2；F-966 保持 OPEN。
