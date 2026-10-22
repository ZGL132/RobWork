# B23：范围、批次与偶发失败观测审计

本批完整审读verify-task-scope.ps1、validate-b1-batch.ps1、run-flake-probe.ps1，按真实函数及入口语义段登记。PowerShell原生AST定义另存batch23/governance/definitions.json，全文绑定与精确源区段见功能矩阵。关系仍pending，空targets、governance-script执行入口；不虚构CMake目标或授予模型权限，不代表全治理、全T01退出。

上游实际检索见batch23/search.json。已核RobWork/RobWorkSim的原gtest宏、CTest add_test、gtest XML报告目标和原InitGoogleTest/RUN_ALL_TESTS入口；这些基础可提供真实C++执行及原生XML，未提供项目契约路径白名单、12阶段业务编排规则或PowerShell重复负载观测语义。保留原Git、PowerShell/.NET及现有三个脚本，不另建通用调度/测试平台。有限范围无命中不表示全库无能力；TEST-HARNESS仍candidate。

范围脚本复用Git解析冻结提交和NUL路径diff，forbidden优先于allowed；planned/blocked只警告，不应当成实施准入。真实正例、非法SHA反例、allowed内命中forbidden反例分别exit0/1/1且具名命中。B1原清单结构正例exit0，临时副本首阶段顺序改9真实exit1；结构PASS允许openGates存在，绝不等同外部前置已满足。5项探测都符合预期，但不声称穷尽通配、rename/删除、Git异常、全部批次边界。

已复现原run-flake-probe误绿：VS x64环境、单绝对原B22审计exe、真实filter无匹配，gtest产生tests=0 XML且进程exit0，脚本也exit0并报1/1全绿。原健康单测正常通过。解析器通过PowerShell原生AST原样提取，7项在PS7/PS5.1各4passed/3failed/0skipped，零用例、error、全skipped均漏过；before原字节与日志保留。

最小修补仅原Get-FailedTestNames与原-SelfTest：无testcase拒绝，failure/error点名，全跳过/禁用拒绝，缺XML/坏XML原语义保留；健康执行不变。未另建解析器或核心算法。相同7项两运行时各7passed/0failed/skipped，原SelfTest两运行时各7passed。真实进程重跑零用例脚本exit1且<xml-no-tests>具名，健康1例exit0。红绿目录隔离，stdout/XML/summary原样保留。仅单次、无CPU负载，未验证持续压力/竞态/GUI，不能声称稳定性全通过。

此次改动是治理观测修正，无产品/框架/vcpkg变化；B22 F964及其他产品缺陷仍open。治理独验、全T01审计、T02适配及最终关闭分别验收，不相互替代。遵从所有者要求，本地提交且不做远程同步。
