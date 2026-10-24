# REUSE-T21：C4 R3 有界生产适配

起点 `2136a89d5cd65e7183c734956df68291862e0fc0`，`redesign-main` 串行实施。所有者已明确批准 **“批准 R3 有界实施，最终性能另验”**；批准对象、规范摘要及限制见[原提案批准节](../../../reuse/extensions/GAP-CORE-C4-01.md#r3所有者批准2026-10-24)。本包是实施自查，**独立验收 NOT RUN，最终性能已获本范围接受，F-966保持OPEN**。

## 需求、来源与适配边界

- **保留**：C4非对称公式、普通路径的历史浮点舍入、非有限输入拒绝及非法聚合tolerance历史行为；标量/vector/真实Q公共签名、ABI、noexcept、长度错误和C7默认值不变。core仍为公式唯一owner，无共享状态、模型、缓存或新公共类型。
- **适配**：只在有限合法非负tolerance且diff/limit同时溢出时，在原`Compare.cpp::closeWithin`使用既有vcpkg Boost 1.92固定128位乘积、4224位加减/绝对值/比较。数据流为消费者值→既有core标量入口→该分支库算术→bool；vector/Q继续委托，不做业务侧补偿。
- **删除/不引入**：本包未删除既有生产算法或类；旧错误比较仅在此分支被替换，正常返回仍用于其余输入。未引入R2折叠/哨兵、双核心fallback、helper/包装层或通用精确数值平台；R2从未进入生产，不能计为本包删除的生产代码。
- **上游适用性**：复用[B31检索及实审](../../../reuse/core-units-compare-audit.md)、[R3库来源/位宽推导](../REUSE-T14/revision-r3/README.md)。`rw::math::Q::operator==`为精确等值，范数/对称近似不满足逐元素、相对reference的C4；Math有限检测/C7任意epsilon也不能替代产品契约。RobWork/RobWorkSim无已验证同语义入口；不因同名函数强接，不新写核心。
- **逐功能结论**：标量为C4权威及获批库算术接缝；vector/Q分别保留顺序逐元素、空集true、短路和长度违约异常；C7仅机械转写需求，非上游数值算法。原8项测试保留其独立业务期望，新5项覆盖本次溢出及不变边界；不将产品测试oracle改成同库互证。
- **构建**：已有core私有Boost头供给满足需要；CMake仅2行依赖范围注释变化，无目标/链接/公开依赖变化。没有修改上游、vcpkg、permissions、catalog、门禁或公共头。

### 异常与资源边界

成熟来源为安装树`boost/multiprecision/cpp_bin_float.hpp`的`cpp_bin_float`、`do_eval_add`、`eval_subtract`、`eval_multiply`及比较；摘要绑定于[生产oracle原件](production-oracle.json)。128位乘积最多106有效位；4224位仅用于扩展、加减、abs和比较，4197位上界推导沿用R3，待独立审查。

该头的`rep_type/double_rep_type`在`Allocator=void`时为定宽、`unchecked`整数；本包无字符串解析/输出/除法/转窄整数路径。有界binary64输入的指数远离库指数极限。128位乘法低于`cpp_int/multiply.hpp::karatsuba_cutoff=40` limbs；不执行4224位乘法，避开其`setup_karatsuba`临时动态存储。不改库阈值、不catch-all掩盖异常。已执行全部语料及计时的new/new[]计数0；**样本计数并非全域无异常证明**，固定存储及有界操作的静态论证仍须独验。

## 原始验证与范围

使用既有VS2022 x64、Release、vcpkg、gtest及原生`ird-test-report.json`。只构建core及真实C4消费者目标；复用现有集成/冒烟配置，无全库重配。命令、过滤器、绝对exe及环境见[消费者执行脚本](run-affected-tests.ps1)与[构建日志](integration-build.log)。

| 检查 | passed | skipped | failed | 原件 |
| --- | ---: | ---: | ---: | --- |
| 仅加回归测试、旧生产红例 | 0 | 0 | 4 | [red.xml](red.xml)、[red-report.json](red-report.json)、[构建](red-build.log) |
| core最终恢复版本（含真实Q） | 74 | 0 | 0 | [core-restored.xml](core-restored.xml)、[报告](core-restored-report.json)、[构建](restored-build.log) |
| 独立冒烟core（不含Q，非跳过） | 71 | 0 | 0 | [smoke.xml](smoke.xml)、[报告](smoke-report.json)、[构建](smoke-build.log) |
| testkit相关消费者 | 29 | 0 | 0 | [testkit.xml](testkit.xml)、[报告](testkit-report.json) |
| trajectory相关消费者/契约 | 31 / 20 | 0 / 0 | 0 / 0 | [测试](trajectory.xml)、[契约](trajectory-contract.xml)及同名-report.json |
| selection相关消费者/契约 | 112 / 29 | 0 / 0 | 0 / 0 | [测试](selection.xml)、[契约](selection-contract.xml)及同名-report.json |
| optimization相关消费者/契约 | 9 / 9 | 0 / 0 | 0 / 0 | [测试](optimization.xml)、[契约](optimization-contract.xml)及同名-report.json |
| 真实库结果消费扰动（预期FAIL） | 0 | 0 | 3 | [consumption-red.xml](consumption-red.xml)、[报告](consumption-red-report.json)、[恢复摘要](consumption.json) |

- **独立数值对照 PASS**：[verify-production.py](verify-production.py)、[production-oracle.json](production-oracle.json)、[日志](production-oracle.log)。真实生产入口对真实CPython Fraction共22,831组，其中13,291组同时溢出；溢出失配0、正常/非法不变路径差异0，修正3,494个旧误判。保留普通路径原舍入差异36，不声称全域改为精确实数语义。输入、生产源、公开头、库文件和两个exe的原始摘要均在JSON中。
- **正向消费 PASS**：[verify-consumption.py](verify-consumption.py)临时将真实`wideLimit`替换为0，标量/vector/Q边界各真实FAIL；finally恢复原字节，核原始SHA相同，再build并执行core 74项PASS。没有保留测试开关或替代算法。消费者先前构建使用同一正确源码，未因临时变异重复全跑。
- **架构 PASS**：[architecture-gates.log](architecture-gates.log)，既有11类规则零命中。生产消费者为testkit/Check、trajectory/TimeParam与Continuity、selection/Screening与CombinationCheck、optimization/Pareto；runtime生产无直接C4调用，未重跑无关模型/GUI。不将提案的“相关runtime”泛化为强制全量验证。
- 线程：纯函数、局部定宽对象，无跨线程状态；不新增线程资源或动态缓存。未跑全库、上游完整套件、产品GUI、跨平台及打包，均为**NOT RUN**，不在本次影响范围。

复现数值命令（仓库根）：

```powershell
# 先在build/c4-production-probe执行本目录build-probe.cmd；保留旧R3对照exe，不覆盖它
D:/software/Miniconda3/python.exe RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T21/verify-production.py build/c4-production-probe/c4-production-probe.exe build/c4-r3-probe/c4-r3-probe.exe
```

## 性能与第三方表达债务

[同轮交替对照](performance-pair.log)使用[同一驱动](performance-pair.cpp)、MSVC `/O2`、不同翻译单元无LTO；每轮正常500万次、溢出2万次，3轮串行。旧实现从起点Git对象导出，仅重命名符号供实验，不入产品。构建/源与exe摘要见[命令](build-performance-pair.cmd)、[复现](measure-pair.py)、[记录](performance-pair.json)。

- 正常：旧`0.00178–0.00227µs/次`，新`0.00352–0.00477µs/次`；同轮比约1.70–2.11倍，绝对增加约`0.00145–0.00250µs/次`。
- 同时溢出：新`9.78–10.21µs/次`；旧`0.00214–0.00235µs/次`但结果错误，仅是成本参照，不是等正确性比较。
- [另一真实生产计时](production-performance.log)三轮溢出`9.84–12.18µs/次`，正常`0.00348–0.00604µs/次`，分配0。计时不是产品端到端预算；消费者正确性回归不能替代性能接受。**所有者已针对上述同轮结果明确答复“接受本次开销，交独验”**（2026-10-24）；仅接受本包具体成本，不代替独验，T15/T20裁决不外推。

当前catalog只支持RobWork/RobWorkSim Git来源，无法如实表达Boost。本次批准明确允许保留第三方表达债务；沿用过渡`capabilityId=NONE / business-specific / migrated`及manifest `business`，绑定本原件和真实库来源。该标签只表示实施及证据登记，**不表示无第三方、RobWork适配、整体合规、独验或closed**。不伪造能力、不改schema或放宽门禁；最终准入仍受性能/独验约束。

回滚边界是REUSE-T21的3个产品文件及本轮治理增量；保留原提案/失败/独验记录，不修改历史，不恢复R2或EAA，不留双核心。回滚后F-966仍需显式开放，依赖本修复的消费者不得冒称极值问题已关闭。

治理过程：初次凭证使用Windows反斜杠被路径门禁拒绝，修正后发现文件头/注释/条件编译声明段尚未登记，分别保留[首次失败](validate-docs-initial-fail.log)、[覆盖失败](validate-docs-coverage-fail.log)。补齐本文件声明区段的真实审计，不放宽门禁；[最终文档/契约/复用检查原件](validate-docs.log)。全库证据索引深查未运行，不冒称全项目迁移完成。
