# B21：治理入口与精简回归审计

本批覆盖commit-lint、validate-task、verify-task、test-governance-lite四脚本，逐函数和入口语义段登记14条pending；不是19脚本全部完成，也不是全T01退出。source区段/全文绑定见batch21/definitions.json及原矩阵；PowerShell原生AST定义/入口定位原件另存script-definitions.json。继续沿用B19独立验收后的execution表达，不虚构CMake目标，不发模型权限或提前迁移关闭。

上游检索实际命令/输出/退出码见batch21/search.json。已读取RobWork与RobWorkSim的gtest CMake宏及RobWork测试main：前者add_test和XML报告目标、后者InitGoogleTest/AddGlobalTestEnvironment/RUN_ALL_TESTS，是现成C++测试基础；不能当PowerShell契约/提交/章节/库存规则实现。没有检索命中不证明全库无能力；本批用现有Git、PowerShell/.NET及原项目validate-task/reuse/docs，不新写通用核心或另建C++脚本测试宿主。TEST-HARNESS候选仍保持candidate，不把脚本测试计为生产上游接线。

已复现精简回归夹具漂移：catalog新增绑定上游gtest后，原稀疏副本未带两套gtest目录，default-ignores-optional-index提前REUSE-MISSING；PS7和PS5.1均expected8/executed7/passed6/failed1，最后1项未执行，不伪报8项失败。最小修补沿用test-reuse-governance已有检出面，只加RobWork/RobWork/gtest与RobWork/RobWorkSim/gtest并解释原因。两运行时修后各8passed/0failed/skipped、complete=true，具名坏索引反例真实拒绝。原脚本前态保留lite-before.ps1；未改变索引规则、预期退出码或断言。

额外将T01契约仅branch改为main，validate-task与verify-task -DryRun均真实exit1且具名invalid branch；健康原契约DryRun exit0。DryRun只核前置/打印命令，不能冒充执行verify三命令。commit-lint原6例验证短提交/自由正文允许、缺前缀/泛化/空标题/未知前缀拒绝；范围、Merge/长度全部边界及脚本执行分类真实性没有靠这6例授予全覆盖。当前verify-task将Parse/CommandNotFound显式标manual，仅机器计数PASS，不能解释为所有散文已人工验收。

产品/框架/vcpkg零变更，未跑无关C++/GUI/性能。本批只修治理夹具并记录已查边界，剩余治理脚本、执行/平台/业务隐藏实现继续审计；全T01、T02和最终关闭仍未通过。

初稿入口语义段误将非字面标签填入source.symbol，被原门禁REUSE-FRESHNESS region/symbol拒绝；原日志保留batch21/initial-registration-rejected.log。改为真实源码锚点，语义分组保存在semanticGroup/qualifiedSymbol；未放宽门禁。
