# REUSE-T01 B23 局部独立验收

结论：**B23_LOCAL_PASS**。冻结对象 `062522fdeb9215e6a604a11d95cfca99842523db`，比较基线 `daf0dcd175fb76c956154fe0213f2d791dad8575`。本次只验收三个治理脚本的18条pending功能关系、run-flake-probe原解析器与原SelfTest最小修补，以及相关证据归档。未实施产品修改，未提交、未远程同步，全部新证据只写入本Temp验收目录。

- 18条关系的126项绑定全部通过：源区段规范化摘要、符号字面量、完整输入和auditEvidence摘要正确；均保持pending、空targets、governance-script入口。PowerShell原生AST独立重解析得到80项定义/入口语句，与归档完全一致。按真实语义逐项复核见semantic-review.json，未把分段登记当成新的PowerShell函数。
- verify-task-scope复用Git冻结提交、merge-base与NUL路径diff，原.NET通配规则及forbidden优先级符合登记；planned/blocked警告仍只提供范围参考。validate-b1-batch校验12阶段、契约分支/依赖、外部门引用，结构PASS允许openGates，不能当成前置已满足。run-flake-probe沿原XML解析器增加零case、error、全skipped/notrun拒绝；进程启动、重复/负载和汇总机制保持原实现。
- 四条上游检索独立重放，退出码及规范化stdout均一致。另审读原gtest/CTest宏和InitGoogleTest/RUN_ALL_TESTS入口，原生执行及XML能力存在；本地项目路径契约、12阶段编排和PowerShell观测语义未被这些符号提供。有限检索不证明整个上游不存在能力，TEST-HARNESS仍candidate，不授模型构造/写权限。
- B21归档manifest的91个条目与原Temp证据的长度、raw SHA256完全一致，manifest本身亦逐字节相同，共92文件。before原件与父Git逐字节**不同**：原件151处CRLF，父blob为LF；替换CRLF为LF后正文完全相同。两份raw原件均保留，未改写。before raw为35878035…，父blob为28da2cf7…；最终LF after raw为a9bf73af…，与verification一致；此前CRLF after测试raw 78ad8b1b…仅对应历史测试原件，未冒充最终LF测试。

独立执行结果：

| 验证 | passed | failed | skipped | 结果 |
| --- | ---: | ---: | ---: | --- |
| 最终LF原SelfTest，PS7 / PS5.1各自 | 7 | 0 | 0 | exit0 |
| 原解析器7例，PS7 / PS5.1各自 | 4 | 3 | 0 | exit1，预期红：zero/error/all-skipped |
| 最终解析器相同7例，PS7 / PS5.1各自 | 7 | 0 | 0 | exit0 |
| 独立附加XML边界，PS7 / PS5.1各自 | 5 | 0 | 0 | disabled、mixed skip/healthy、disabled/error、多suite失败、全跳过 |
| scope/B1正负5探测 | 5 | 0 | 0 | 预期退出码0/1/1/0/1及具名输出 |
| 冻结范围184路径 | 1 | 0 | 0 | 全部允许面内 |
| verify-task原机器命令 | 3 | 0 | 0 | validate-task、validate-reuse、validate-docs均真实执行；manual=0 |
| 新增1提交commit-lint | 1 | 0 | 0 | PASS |

真实进程红绿在VS x64环境逐个单独启动同一个只读绝对路径exe，QT_QPA_PLATFORM=windows，未使用offscreen。每次Repeat=1、LoadJobs=0。零用例gtest实际执行0、进程exit0：修前脚本exit0假绿，修后脚本exit1并具名`<xml-no-tests>`；健康路径实际执行1、passed1、failed0、skipped0，前后脚本均exit0。四次均保留stdout/stderr/XML/summary/driver与退出码，健康路径保留原有per-exe报告。零用例不计为通过的产品测试。详见process-results.json和process-*目录。

验收过程中首次Temp检出因长路径失败；仅在独立副本启用core.longpaths后重新检出成功。before raw等同性初检为false，进一步核实仅换行差异，未把该false粉饰成字节相同。默认git diff --check因JSON原CRLF报告trailing whitespace（exit2）；显式Windows cr-at-eol口径重跑相同四个实际编写文件exit0，两个原始日志均保留。最终Temp副本无已跟踪修改。完整命令及退出码见commands.json，未执行项如下。

本结论不授予全REUSE-T01、全治理、REUSE-T02、复用迁移、最终关闭、模型所有权/构造/写权限。持续负载、竞态、GUI、全产品构建和全产品测试为NOT RUN；B22/F964等既有open缺陷不在本局部验收中关闭。治理机器PASS仍只证明机器可查事实，validate-reuse所列DEBT/UNRESOLVED仍保留。
