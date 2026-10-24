# WP-10／GOV-T26：C4 成熟原语候选 R3

起点 `0a091394b9466c3fa7521d34162b1c8b64a843c4`，仅修订提案、隔离验证；**没有修改产品、依赖许可或上游，也没有实施或独验通过**。原 A 失败和 R2 原件均不回写。

## 需求、候选与边界

仍判 `|v-r| <= rel*|r|+a`。正常路径保持生产浮点舍入，仅在有限输入、合法有限非负 tolerance、差值与限值同时溢出时修正排序信息。非法聚合 tolerance 保持当前行为，不借本包收窄接口。

上游检索沿用 [B31 实审](../../../../reuse/core-units-compare-audit.md) 及 [原提案](../../../../reuse/extensions/GAP-CORE-C4-01.md) 中 Math/Q/Metric/LinearAlgebra、测试/示例/文档的证据：尚无同语义 RobWork API。新增候选为**既有 vcpkg Boost 1.92** 的 `boost/multiprecision/cpp_bin_float.hpp`，不是 RobWork 能力，不另引依赖渠道。

R3 只适配成熟库原语：用固定128位 `cpp_bin_float` 形成 binary64 两数的精确乘积，再用固定4224位类型进行原差值、绝对值、加法与比较；均为 `Allocator=void`、`et_off`。拟议生产计算约十行，不写 TwoSum、增广精度循环、哨兵或另一套算术核心，不新增公共类型/包装层。

保留业务公式、标量入口和 vector/Q 委托、core 所有权、公开 ABI/noexcept、C7 数字；删除的是拟议 R2 所需自写折叠算法，**当前生产尚未包含它，不能记为已删除产品代码**。数据流仍为业务值 → core::closeWithin → 仅溢出分支的库原语 → 原 bool。没有模型或状态所有权变化。

## 位宽依据与待审条件

- 有限 binary64 是 `2^-1074` 的整数倍，绝对值 `<2^1024`；两数乘积至多106个有效二进制位，128位类型及其指数范围可精确容纳，包含最小乘积 `2^-2148`。
- 差值绝对值 `<2^1025`、量子 `2^-1074`；右侧乘加 `<2^2049`、量子 `2^-2148`，因此至多4197位，4224位有余量。输入转换及上述中间结果可精确表示，不需除二，不丢次正规 absolute。
- 不在4224位类型执行乘法：已安装 `cpp_int/multiply.hpp::setup_karatsuba` 的固定宽度分支仍可能使用临时堆存储。128位乘法低于其40-limb阈值，再无损扩大，可避免该路径；不更改 Boost 宏、阈值或源码。
- 这些是本轮推导，**未经独立验收**。公开 noexcept 不能仅由零次分配样本证明；获批实施时须审查这组有界操作的异常/分配路径，不能用 catch-all 返回 false、终止进程或放宽阈值掩盖差异。

## 原始验证

仅用 MSVC x64 `/std:c++17 /O2 /EHsc` 直接编译隔离探针和真实生产 Compare.cpp；R2 则复用原 C4Probe.cpp 和原候选文件。[构建命令](build-probes.cmd)、[构建日志](build.log)。未编译整个 core/runtime，不修改 CMake。

```powershell
# 仓库根；先在 build/c4-r3-probe 执行 build-probes.cmd
D:/software/Miniconda3/python.exe RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T14/revision-r3/verify.py build/c4-r3-probe/c4-r3-probe.exe build/c4-r3-probe/c4-r2-probe.exe
build/c4-r3-probe/c4-r3-probe.exe --bench
```

[verify.py](verify.py) 通过 AST 仅取原 oracle.py 的语料生成前缀，停止于 bits 定义前，不执行其历史输出段；真实 CPython/Fraction 复用13168组，补全指数域、阈值邻点、原失配和特殊输入。没有再手写 MT。此举直接核验真实 Python 语料，**不证明旧 C++ MT 复刻逐位等价**。

| 检查 | 本轮实测 |
| --- | --- |
| 总输入／有限合法输入 | 22831／22600 |
| 同时溢出 | 13291 |
| 当前生产相对精确 oracle 的溢出误判 | 3494 |
| R3 库公式／溢出分支／不变路径失配 | 各0 |
| R2 溢出／不变路径失配 | 各0；样本通过不消除下述静态风险 |
| 正常路径相对精确实数的原舍入差异 | 36，原样保留，不计为本轮修正 |
| R3 计算期间 C++ new/new[] 计数 | 0；仅为已执行样本证据，不是全域无分配证明 |

原件：[结果与源/库摘要](result.json)、[运行日志](verify.log)。测试为普通协议探针，未使用 gtest，不能冒称产品测试或已验证真实 Q 重载。独验 **NOT RUN**。

## 性能与运行限制

[串行微基准](performance-serial.log)：每轮正常路径100万次、溢出2万次、3轮。当前生产正常 `0.00156–0.00190 µs/次`，候选正常委托 `0.00469–0.00676 µs/次`；当前生产错误溢出 `0.00185–0.00292 µs/次`，候选溢出 `9.29–10.54 µs/次`。计数校验和及分配计数均保留。只测函数调用，不是产品端到端预算；正常候选包含重复检查和生产委托，不能当成未来单点实施的精确开销。

[首次性能原件](performance.log) 与正确性探针同时运行，存在竞争，仅保留过程，不用其作为最终性能依据。没有接受本包性能的所有者授权；T15/T20 的接受不外推。后续实施须测最终生产正常/溢出成本及真实消费者影响，未经接受不得结项。

## R2 复核与下一动作

原候选 `exactSignInner` 256轮后返回 `-2`，外层 `exactSign(leaf,5) <= 0` 实际得到 true，与提案“哨兵返回 false”相反。这是静态分支不一致，**没有发现合法输入触发哨兵的反例**。终止性及次正规论证仍未独验，不能将样本通过当作严格证明。

推荐用 R3 取代 R2 作为待批准的实施方向，先确认私有 Boost 依赖范围与有界运算/异常契约；批准前不动生产。core 当前仅有 FNV 私有 Boost 授权，不自动覆盖 Compare.cpp；第三方 catalog 表达缺口也不沿用 T20 的局部裁决。后续若获批，仅改原 Compare.cpp/CompareTest.cpp 及必要 core 规范、功能凭证；复用既有 core/Q、runtime 相关消费者、必要冒烟和独立 oracle，只验证实际影响范围。无法满足异常/性能契约时保留缺口，不恢复自写折叠核心。

回滚本轮仅撤销提案、任务和隔离实验的增量；不涉及产品回退。F-966 保持 OPEN。

治理过程：首次文档检查因GOV-T26缺少必需reuse引用失败，见[失败原件](validate-docs-initial-fail.log)；已按既有格式补空产品变更凭证（本轮没有产品改动），不改门禁。最终检查见[原件](validate-docs.log)。
