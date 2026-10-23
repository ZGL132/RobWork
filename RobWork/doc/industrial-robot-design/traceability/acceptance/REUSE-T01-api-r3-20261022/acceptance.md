# REUSE-T01 API探测增量独立验收：PASS

冻结 SHA `4e142e485343e8bade52a8481eefbb447b017989`；基线 `dfc317f234d9253b2e07bdea89a9bb3028e6db9d`。独立临时 clone、detached HEAD，最终工作区干净；未改父工作树、产品、上游或vcpkg，未实施、提交、推送或合入。

VS2022 x64环境从零configure/build既有probe，通过现有RobWork/RobWorkSim导出targets和testkit报告入口真实运行全部14项。passed=14、failed=0、skipped=0、notRun=0；原始tests.log、gtest.xml、ird-test-report.json留存。task/docs/reuse均exit0；后者只证明治理结构，不证明算法合规。

新增四项独立语义复核成立：

- ParallelIK：单Z转动关节TCP为(cos q,sin q,0)，对(2,0,0)距离平方=5−4cos q≥1；q=0的切向Jacobian=(0,1,0)，残差=(1,0,0)，伪逆增量为0，两个迭代均不能改善。实际日志确有预算耗尽，源代码随后仍push候选；返回候选残差1 m，不能当收敛。默认未启用关节限位检查；本例q=0本身在给定界内。
- 标准DH：Rz(theta)Tz(d)Tx(a)Rx(alpha)直接四因子展开与DH公式一致，参数0.4/0.3/−0.2/0.7的Craig顺序Rx(alpha)Tx(a)Rz(theta)Tz(d)与之不同。RPY实参确为yaw/pitch/roll，因此探测四因子轴选取正确。
- 反向对齐：EAA(z,−z)分支选z×x=+y，得到Ry(π)=diag(−1,1,−1)；产品Sampling.cpp反向分支固定Rx(π)=diag(1,−1,−1)。相对旋转为diag(−1,−1,1)=工具局部Rz(π)，两者方向相同而滚转相差π。
- 多项式：从p(u)=Σ(i+1)u^i直接逐单项求导，独立精确有理数算出左端1/2/6/24、右端36/168/756/3024。系数先除h^i后对物理时间求导：h=0.5右端36/336/3024/24192；h=2右端36/84/189/378。测试未将断言两侧同除h来伪造时间缩放，覆盖非零端导数。

归档第二批FAIL/PASS共82文件原文一致：78字节完全一致，包括acceptance.md/json及原始日志；4个Python脚本仅Git CRLF/LF检出转换。全差异git diff --check=2，唯一告警为归档batch2-r2/inspect.py:21原件已有EOF空行；此为保留历史原文的非阻塞格式项，未改写原件。排除两份归档后的活动文件diff检查exit0。完整比较见archive-review.json。

累计95关系全部decision=status=pending，closedFiles=0。结论仅API探测增量，不代表T01全20单元全功能审计完成、生产复用通过或T02准入。未跑完整上游集、140治理自测、DeepEvidence、生产/性能验收与远程CI。

独立解析复算脚本inspect.py、构建运行脚本run-probe.ps1与本报告生成脚本finalize.py均在本目录；详细命令、绑定与原始结果见acceptance.json和对应日志。
