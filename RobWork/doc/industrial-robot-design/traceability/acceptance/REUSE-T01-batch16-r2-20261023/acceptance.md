# B16-P2-01 返工独立复验

结论：**PASS，仅B16-P2-01返工复验，该项关闭。** 不重授整批B16、全T01 PASS或T02准入，不复验B17，不重跑29脚本。

冻结SHA：`3a8375eac74408d06ba8999013635428aa6c90e7`；直接父：`7eb991346be4eacaf15b0fdf5f914f0e4859875c`；原FAIL比较基准：`435dbdb716b9eb86d0932a478eb086642206ac76`。独立现场：C:\Users\zgl18\AppData\Local\Temp\ird-accept-b16-r2-152411a5b07f4a5b856e70805989a6f9\repo。

22条C++ helper均已绑定来源cpp全文inputs（StatisticsTest 9、XacroExpandTest 9、ParetoTest 4），原精确sources/全限定符号保持不变。对B16涉及的四份矩阵比较，只有这22处inputs增加，其余数据与原FAIL基准一致；batch16-rework.json的22项绑定完全对应。全文绑定覆盖同文件常量、raw字符串、相邻helper和类默认成员，无同批未补的C++ helper。

154项摘要全部匹配。5个原内存反例现在均会使新增全文inputs摘要失配：容差、质量、解析期望/模型参数以及Xacro字节改变时，证据可失效。精确函数区段摘要仍保持不变，证明此次补充确实覆盖原漏点。仅内存替换，没有写仓库文件，没有执行C++或真实门禁变异运行。

| 检查 | 结果 |
| --- | --- |
| validate-task | PASS，exit0 |
| validate-docs | PASS，exit0；NOT AUDITED evidence |
| validate-reuse，冻结SHA | PASS，exit0；仅治理、closedFiles=0 |
| scoped git diff --check | PASS，exit0；仅排除保真原.log |

门禁passed=4、skipped=0、failed=0。原FAIL与生成器报告共151文件：144原始字节相同、7仅CRLF/LF、无内容偏差。原语义FAIL保留45文件（42原样+3换行），生成器原报告保留106文件（102+4）；原FAIL/OPEN及生成器限域PASS保持不变，F-959未关闭。

自原FAIL基准起产品/上游/vcpkg无变动，独立checkout干净，未修改实施现场、提交、推送或合入。父提交包含B17其他登记，本复验不覆盖。NOT RUN：C++/GUI/数值性能、DeepEvidence、29生成器重跑、整批B16/B17或其他批次、远程交付核验。

详细命令、脚本SHA256与边界见acceptance.json；证据见facts.json、binding-checks.json、matrix-change-checks.json、hash-checks.json、freshness-probe.json、archive-checks.json及task/docs/reuse日志和exit文件。
