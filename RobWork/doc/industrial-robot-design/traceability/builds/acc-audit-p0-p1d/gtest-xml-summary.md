# 验收者实测 gtest XML 汇总（acc/audit-p0-p1d/1，2026-10-09）

产生方式：对送验 SHA 5f8c3904d99cab297107c3b74f7e507cf6b4e444 的复现树，
逐套件构建 <target>_report 目标（ird_add_gtest 宏——运行测试 exe 并落
--gtest_output=xml 至构建树 gtest_reports/<target>.xml），汇总如下。
原始 XML 留存于验收临时树（构建树 gtest_reports/），此处只登记计数。

| 套件 | total | skipped(登记跳过) | 实过 | 失败 | 链上声明 | 一致 |
| --- | --- | --- | --- | --- | --- | --- |
| core_test | 63 | 0 | 63 | 0 | （未单独声明） | 是 |
| diagnostics_test | 115 | 0 | 115 | 0 | 115/115 | 是 |
| dynamics_test | 60 | 0 | 60 | 0 | 60/60 | 是 |
| execution_contract_test | 38 | 0 | 38 | 0 | 38/38（真进程） | 是 |
| execution_test | 150 | 0 | 150 | 0 | 150/150 | 是 |
| io_test | 122 | 0 | 122 | 0 | 122/122 | 是 |
| kinematics_test | 183 | 1 | 182 | 0 | 182/182 | 是 |
| modeling_test | 344 | 0 | 344 | 0 | 344/344 | 是 |
| project_test | 255 | 1 | 254 | 0 | 254/254（1 项登记跳过） | 是 |
| requirements_test | 204 | 1 | 203 | 0 | 203/203 | 是 |
| testkit_test | 101 | 0 | 101 | 0 | 101/101 | 是 |
| ui_test | 255 | 0 | 255 | 0 | 255/255（含 F-613 新增） | 是 |

说明：requirements/kinematics/project 三套件 total 比链上声明多 1，
均为既有登记跳过用例（GuiRegistration.V22RegisteredNotExecuted_ACC1 /
KinGuiPresentationRegisteredNotExecuted_ACC1 /
Tx11CopyConsistency.StageBPrerequisiteNotReady_SkipRegistered），
实过数与声明严格一致。