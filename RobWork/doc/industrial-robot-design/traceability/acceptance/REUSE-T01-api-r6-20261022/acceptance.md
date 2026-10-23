# REUSE-T01 API新5项独立验收：PASS

冻结 SHA `877f6afee38446ab43c5e6a6fd9b09593925aedb`；基线 `c7e1469b40ca7c1dbba55a3b6bd8a443fc6086c5`。独立临时clone、detached HEAD；最终git status为空，未改父树、产品、上游或vcpkg，未提交/推送/合入。

VS2022 x64从零configure/build既有probe，以既有RobWork/RobWorkSim导出targets及testkit报告入口实跑19项：passed=19、failed=0、skipped=0、notRun=0。原始configure/build/tests日志、gtest.xml及ird-test-report.json保留。task/reuse/docs与diff-check均exit0。已有14项测试正文未变；仅增头文件及5项测试。

独立语义复核：

- RPY普通±0.4、近奇异±(π/2−1e−8)及精确±π/2旋转往返断言有效。独立Rz·Ry·Rx连乘确认，正奇异时yaw=0、roll=−0.9与原yaw=0.7/roll=−0.2表示相同旋转；负奇异等效roll=0.5。源码r12明确是R(0,1)，不是R(1,2)。产品roll=0的规范化选择与上游yaw=0字段不同，不能以字段不等认定旋转不等。本轮不认定上游RPY缺陷。
- 混合尺度对角阵diag(denorm_min,1,1e300)的精确数学谱即三个正对角元素，denorm_min=2^−1074>0。上游谱接口返回min=0的实测有效，说明直接以该谱值判严格SPD受限；不等于上游完全没有可用候选。
- 单位块[[1,off],[off,1]]谱为1±off。用binary64精确有理数独算，off=nextafter(1,0)最小谱=2^−53>0，off=1为0，off=nextafter(1,2)为−2^−52。既有Eigen LLT对三者分别成功/拒绝/拒绝，且对混合尺度对角输入成功。
- 1e−14虽不是精确十进制，但四个块元素是同一个binary64值，前两列完全相等，(1,−1,0)是精确零向量；谱为0、2c、1，严格SPD应拒绝。LLT实测NumericalIssue正确。以上只覆盖列明输入，不宣称完整合法性/不对称/平衡/极值/消费者回归已通过，也不授权保留自持LDLT。

r4保留18实跑、17passed/1failed的假设源码和失败日志/XML/native报告；唯一失败是错误预期1.8rad，而实测约4.199e−17rad。r5保留18passed，r6保留19passed，XML与native报告的total/passed/failed/skipped/notRun一致，全部status=run。r6源码快照与冻结源码一致；历史r5无独立源码快照，不将行号或build.log冒充完整构建来源证明。本轮没有改写任何历史证据，也没有重跑旧错误假设。

5条功能记录的11条新增auditEvidence规范化摘要全部匹配；environment-artifacts列示数学DLL和Eigen LLT.h的原始SHA256及字节数全部与本机文件相符，源码/CMake规范化摘要匹配。记录明确仅是已安装二进制/头文件身份，不证明上游DLL由冻结源可复现构建；该限制已在README及环境文件注明。auditEvidence由本轮独立核对，不声称治理校验器已实现此字段语义审计。

累计121条关系仍decision=status=pending，closedFiles=0。本PASS仅覆盖API新5项及相关证据，不覆盖第四批其他引用修正、全T01功能审计、生产复用或T02准入。未执行完整上游集、全工况数值性能、140治理自测、DeepEvidence和远程CI；无GUI测试需求。

可复现命令见run-probe.ps1，独立解析/摘要核查见inspect.py与independent-review.json，生成步骤见finalize.py；详细结果见acceptance.json。
