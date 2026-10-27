# B16 独立语义登记验收

结论：**FAIL**。冻结 `435dbdb716b9eb86d0932a478eb086642206ac76`，直接父 `a703b88225b1e094947b46723706b5be8af5159f`。只覆盖B16新增43关系，不授全T01 PASS或T02准入；29脚本重跑由另一验收者负责。

**B16-P2-01（P2，OPEN）：5条helper未绑定函数外决定性测试上下文。**

- StatisticsTest.cpp:97的kGoldenRelTol不在expectNearRel登记区段103–108；184–189的质量/半径/惯量/重力常量不在prismAxialForce、revTorque、makePrismRevModel登记区段。inputs只有CMake与TwoLinkFixture.hpp。
- XacroExpandTest.cpp:186–200的kBasicXacro raw字符串不在makeBasicSource的202–205区段，inputs只有CMake与XacroExpand.hpp。
- 5项仅内存替换分别改变容差、解析期望、模型质量或Xacro字节/digest/名称，但对应全部sources/inputs/auditEvidence摘要不变。依据audit-contract.md:77，真实行为变化必须使相关证据失效。详见freshness-probe.json；没有写仓库文件，没有声称C++或真实门禁变异运行。

返工只需补绑定真正影响这些关系的上下文（可将相应cpp全文件加入inputs或绑定精确依赖区段），更新受影响摘要后独立复验。保持精确函数区段，不修改原测试、产品或容差。

其余已核：43个新增ID/定义精确对应，旧矩阵行无变动；optimization 4、modeling 9、dynamics 9、foundation-governance 21。43个函数/顶层组区段完整，匿名namespace/FakeContext/ESModule作用域准确，makeXacroSource默认{}没有误作函数体。341摘要全匹配、6检索命中集合一致、43目标与真实CMake/消费者关系一致。测试独立oracle用途及适用域、Node执行与generated-artifact-consumer区别、F-959未夸大，详见semantic-checks.json。已匹配摘要不消除上述新鲜度缺口。

| 验证 | 结果 |
| --- | --- |
| validate-task | PASS，exit0 |
| validate-docs | PASS，exit0；NOT AUDITED evidence |
| validate-reuse，冻结SHA | PASS，exit0；仅治理、closedFiles=0 |
| scoped git diff --check | PASS，exit0；排除保真原.log/.frozen.txt |

门禁passed=4、skipped=0、failed=0；语义阻断1项。初次只排除.log的whitespace检查exit2来自原frozen摘录的尾空格，日志保存在scoped-diff-check-initial.log；未清洗归档原件。

三组历史归档123文件：71原始字节相同、52仅CRLF/LF、0内容偏差。B14-r2为23（20原样+3换行）、B15-r2为33（31+2）、全覆盖为67（20+47）。B14/B15原单项返工PASS和全覆盖NOT PASS及原冻结SHA均保持原样；本批未扩大其结论。

零产品/上游/vcpkg变动，独立checkout干净，未修改实施现场、提交、推送或合入。NOT RUN：C++/GUI/数值性能、DeepEvidence、29生成器重跑、其余批次、远程交付核验。

完整结果与命令见acceptance.json；逐项证据见facts.json、hash-checks.json、definition-checks.json、semantic-checks.json、target-checks.json、search-replays.json、archive-checks.json、freshness-probe.json以及task/docs/reuse日志与exit文件。
