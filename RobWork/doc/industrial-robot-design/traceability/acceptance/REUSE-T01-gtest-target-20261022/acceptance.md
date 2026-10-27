# REUSE-T01 gtest目标门禁补正独立验收：PASS

冻结SHA `7af7e86a18e34384677c2599a4f8812ad262908f`；基点 `ee9cc79bd68ec27b8b2f95901dddd78d6c57d5d2`。独立随机Temp clone、detached HEAD，使用冻结版本的validator/helper/driver；最终git status为空。未改父树、产品、上游、vcpkg或库存，未提交/推送/合并。

PowerShell 7.6.5与Windows PowerShell 5.1分别完整真实运行142项：各passed=142、failed=0、skipped=0、notRun=0、complete=true；两个进程exit0。每项日志与JSON输出逐项一致，JUnit计数及全部142个唯一用例匹配。真实REUSE-T01 task/docs/reuse均exit0，仅是治理检查。两套独立报告中的validator/helper/driver原始SHA256均等于冻结源码；归档r4的两套报告摘要也全部匹配。

允许范围核对：校验器只从原src子目录路径增加`RobWork/RobWork/gtest/CMakeLists.txt`和`RobWork/RobWorkSim/gtest/CMakeLists.txt`两精确根。原有文件/符号/摘要/目标检查保留，SafePath未改；reuse-contract.ps1未变。驱动sparse checkout补齐两gtest根。独立补充8项实跑：两真实根（sdurw-gtest-main/sdurwsim-gtest-main）通过；两嵌套fake目录、gtest2、RobWorkStudio/gtest、CMakeLists.txt.fake与真实根伪造目标均exit1并报REUSE-UPSTREAM-TARGET。

两套完整驱动中的新正例、任意测试目录拒绝例、两个新文件最终关闭负例均按预期真实执行。关闭负例输出明确为“new/current file lacks independent closure”，没有被无关pending抢先截断。IsolateFinalClosureMatrices只在这两个Case调用，变动仅在隔离副本；PutJson记录原字节、Case finally恢复。01-clean仍看到200关系/closedFiles=0及DEBT=16/UNRESOLVED=1399；22-final-debt-rejected保留真实矩阵并拒绝。另对真实REUSE-T01加RequireMigrationComplete独立重跑，仍以未决函数拒绝，债务未被放行。

独立重放基线版本validator对冻结真实catalog，得到原REUSE-UPSTREAM-TARGET TEST-HARNESS错误；当前门禁通过，修正对应实际触发点。归档r1/r2/r3/r4均保留：r1各executed30/passed7/failed23/notRun112，r2各142/139/3/0，r3各142/141/1/0，r4各142/142/0/0；JSON/JUnit真实失败及driver未完成状态一致，没有把complete=true当作零失败。

全量diff-check=2，只来自历史归档日志尾空白/EOF空行；原文保留，未洗历史。活动validator/driver/audit-contract/gtest-target README单列diff-check=0。源码/上游/vcpkg/库存差异为空，因此无须无关C++或GUI构建。累计200关系=197 pending/3 proposal，closedFiles=0。

PASS只限本门禁补正；不代表第七批其他登记通过、全T01退出、生产迁移或T02准入。未跑无关产品/上游测试、DeepEvidence、远程CI；未提交推送合并。原始日志/报告在ps7、ps51及本目录；归档文件清单见evidence-files.json，勿递归复制repo或scope-fixture。
