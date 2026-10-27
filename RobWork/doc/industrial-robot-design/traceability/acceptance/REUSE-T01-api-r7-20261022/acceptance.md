# REUSE-T01 新3项工具API独立验收：PASS

冻结 SHA `9fd7256cf61a7768622810ed547617a66013949b`；直接基线 `8a7174945a8f280f674a40ec46a512b5b5f1b414`。独立临时clone、detached HEAD，最终git status为空。仅在Temp产出报告和日志；未改父树、产品、上游、vcpkg，未提交/推送。

VS2022 x64从零configure/build既有目标；CMake沿用RobWork CONFIG组件并新增common及RW::sdurw_common，使用既有GTest/testkit报告入口，没有手拼lib或新依赖。绝对路径执行一个可执行文件，filter严格为`ReuseAudit.BinaryArchive*:ReuseAudit.LinearInterpolatorExtrapolates*`。实际passed=3、failed=0、skipped=0、notRun=0；原19项本轮未执行，不称22全跑。gtest.xml、原生ird-test-report.json、tests.log和build/configure日志均留存。task/reuse/docs exit0。

三项语义及独立oracle有效：

- uint32 0x01020304经BINArchive标量writeValue按地址/sizeof直接写出；本x64小端实测04 03 02 01，独立memcpy及显式字节oracle一致，不等于跨平台canonical或大端协议。源码确无端序转换。
- string写入以格式化`<<`输出十进制长度及字符，`x y`编码为`3x y`。读取格式化uint32得3，resize初始化3字符；格式化char提取x、跳空白得y，第三次遇EOF。因此长度仍3但首两字符xy，与原串不同。测试不依赖未初始化内存，不把此个例泛化成所有字符串/畸形输入行为；不能用一般archive roundtrip承诺冻结u32长度+原始UTF8协议等价。
- LinearInterpolator标量为(start+(end−start)/h*t)，普通1→3、h=1、t=2得5，源代码x(t)没有区间守卫；h=0抛出异常。有限DBL_MAX→−DBL_MAX在构造时end−start先变−∞，中点非有限；权重式0.5*max+0.5*(−max)=0。独立标量oracle和源码一致，只说明列明输入差异，不能证明完整回放器迁移或所有舍入等价。

原UpstreamSuitabilityTest.cpp和probe-r6所有文件相对直接基线及先前冻结877f6af...均Git blob不变。冻结probe-r7与本次独立XML/native均实际3项、零失败/跳过/未运行，XML文件路径绑定ToolSuitabilityTest.cpp。21条相关功能记录的84条auditEvidence规范化摘要均匹配，源码/CMake绑定有效，已安装sdurw_common.dll原始SHA256及字节数与环境记录一致。DLL摘要只标识环境，不证明其由冻结源可复现构建；原环境文件已注明该限制。

UTIL-BINARCHIVE仍candidate，矩阵明确主机序/格式化长度差异及既有依赖继续检索，不将候选排除当新核心批准。回放条目保留越界nullopt、帧原值、五量混合、极值/舍入及只显示边界，仍pending。相关结论无过度声称；宿主GUI及第五批其余功能不在本验收范围。

全量diff-check=2，仅归档REUSE-T01-proposals-20261022/diff.log的历史原文尾空白；保留原件，不宣称全差异检查通过。排除该归档后的活动文件diff-check=0。累计146条关系=143 pending/3 proposal，closedFiles=0；未批准提案实施、生产迁移或全T01退出。

未运行原19项、完整上游测试、一般/畸形输入与跨平台验证、生产/性能验收、GUI、140治理自测、DeepEvidence和远程CI。复现脚本run-probe.ps1、独立复核inspect.py、详细independent-review.json及acceptance.json均在本目录。
