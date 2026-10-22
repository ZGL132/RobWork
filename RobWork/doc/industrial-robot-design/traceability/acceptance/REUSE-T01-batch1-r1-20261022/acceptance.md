# REUSE-T01 首批独立验收：FAIL

冻结提交：`8da272d79e8a0b89e3b1a60e3bcb3942dd59d1ae`；比较基线：`212226cfd2e2ca2fd12619dc57c458888f42534b`。

范围仅为6单元24条pending功能—能力关系（23个不同functionId，FK同功能两条关系符合契约）、3项新增候选能力与10项公共API探测，以及本批docs/task。不是全20单元审计完成、生产复用通过或T02准入。

## 缺陷

- **T01-B1-01 / P2** `RobWork/doc/industrial-robot-design/reuse/functions/runtime.json:89`：修正 rotationFromCustomEaa 的命名空间定位。qualifiedSymbol 错记为匿名命名空间。BaseWorldTransform.cpp:165 已关闭匿名空间，207行函数及 include/sdurws/ird/runtime/BaseWorldTransform.hpp:137 的声明均位于 sdurws::ird::runtime。应登记 sdurws::ird::runtime::rotationFromCustomEaa。区段摘要正确不消除此身份错误。

- **T01-B1-02 / P2** `RobWork/doc/industrial-robot-design/reuse/extensions/required-proposals.md:9`：修正半周求解器探测的不可达归因。夹具中 RPY(Pi,0,0) 为绕Z半周，RevoluteJoint 同样绕Z且默认无限界。独立探针使用同夹具 setQ(Pi) 得 targetReachableResidualRad=0。应将“不可达半周姿态”改为“可达半周目标处的误报收敛”；误报本身由原用例证实。

- **T01-B1-03 / P2** `RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T01/UpstreamSuitabilityTest.cpp:177`：按产品roll语义构造Euler掩码反例。EulerMaskCannotBeSubstitutedByEaaRows 使用 RPY(0.6,0.4,0.7)→RPY(1.2,0.4,0.7)，在上游实际构造约定中是释放Z/yaw，固定Y/pitch和X/roll。断言却检查EAA的Y/Z残差，包含本来释放的Z分量，因此不能证成注释所述产品自由roll（X轴）与EAA Y/Z行掩码的差异。应按 Z/Y/X 参数顺序构造固定 yaw/pitch、改变roll 的对照（例如RPY(0.7,0.4,0.6)→RPY(0.7,0.4,1.2)），固定索引0/1，再检查EAA Y/Z残差。

## 已执行验证

- 独立临时clone，完整检出冻结提交，最终tracked status为空；未修改产品、治理、上游或vcpkg，未提交、合入、推送。
- validate-task/docs/reuse exit=0。复用仅治理PASS，functions=24、closedFiles=0；compileWorkCell仍输出未授权pending提醒。
- 24条区段摘要、输入摘要与符号短名均匹配；除上述qualifiedSymbol缺陷外，人工按命名空间边界核对其余条目。全部decision/status=pending；无新增grant/closure。
- catalog所有绑定资源路径/符号/规范化摘要与声明上游版本匹配，3个新增能力均candidate。完整机器逐项结果见matrix-check.json和catalog-resource-check.json。
- VS2022 x64环境，按README既有RobWork/Sim导出目标重新configure/build；testkit和core/test/TestMainReport.cpp实际编入；无GUI。
- 原10项全部执行：passed=10、failed=0、skipped=0、notRun=0。真实gtest.xml与原生ird-test-report.json一致。Bullet未实现、Jacobian半周误报及RNE torque差异都是限制断言，不代表生产能力成功。
- 独立可达性附加探针：同夹具setQ(pi)后targetReachableResidualRad=0，exit=0；源码、构建和运行日志在semantic-*。此附加反证不并入原10项计数。

本轮未跑全20单元审计、完整上游集、生产迁移/断开、完整数值性能、140项治理自测、DeepEvidence或远程CI。网络代理阻断继承为外部限制。返工应绑定新SHA重新独立验收，原FAIL不改写。
