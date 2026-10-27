# DESIGN-T01 设计审核修正与实施验证

起始提交：612143da54365c277a236d12edd665732f2388ee；redesign-main主工作树串行实施。所有者授权修正文档并检查对应代码；保留进入时已有trace-matrix.json及其他未跟踪文件。本文为实施记录，独立验收NOT RUN，由所有者另开会话或人工安排。

## 修正范围

1. drivetrain负载惯量按q̇=Cθ̇的动能等价取CᵀJloadC；转子反射到关节侧仍为C⁻ᵀJrotorC⁻¹。窗口负载缺失不输出伪完整惯量比。
2. 实际轴转矩按方向效率折算传动矩并单加转子矩，使Pmotor=τmotorω；保留无损tauIdeal，静止不除速度。零负载加速保留转子能量。
3. 反转正功率归Q3电动，反转负功率归Q4再生，样本标签与汇总同口径。
4. evidence保留完整inputBaselineId、SliceCodec与缓存身份；computeComparisonInputId经core::ContentDigester规范生成共同需求/工况/采样/环境摘要。跨分支/模型可比，缺失或不一致保守拒绝。域组装方提供权威内容身份，不从对象ID猜共同输入。
5. selection显式接收完整实际电机转矩时序，逐候选额定阈值取最长连续超额定时长；正负穿零分开，周期首尾合并。峰值窗只展示，缺时序数据不足；欠报峰值、非有限或非递增时序fail-fast。时序经既有值端口和codec透传，旧码流由升版明确拒绝。
6. 安全间距正值时后端距离能力不可缺省：policy及trajectory生产逻辑已正确，修正文档与调用注释，执行现有反例验证。
7. workflow优化/报告按B期静态共同前置门控，附加能力缺项仍由域readiness投影负责，不能从导航顺序追加依赖。

三个黄金数据集新增1.1.0，保留1.0.0；参考生成器按独立解析公式/业务规则生成新期望，完整性清单同步。selection新黄金采用首尾零载梯形时序，解析∫τ²dt/T与其RMS声明一致，不用峰值窗补造时序。

## 上游复用与边界

保留rw::math::LinearAlgebra::svd/inverse及既有适配，不新增或绕过上游运动学/动力学核心。惯量与效率是既有接口的物理口径修正。比较身份、目录阈值判定、业务门控和协议编解码归产品；标准容器/排序与core摘要复用原设施。上游检索/候选及逐功能差异见nonoverlap.json。逐文件凭证只证明绑定，不能冒充语义审计或独验。

DYN-02/R1外力输入、P-DT-4负载惯量权威供给等既有未决事项保持；本包没有设计新外力通道、关闭存量债务或扩大runtime模型对象权限。

## 验证记录

测试均在VS2022 x64开发环境逐exe执行，QT_QPA_PLATFORM=windows；产出原始日志、gtest XML与per-exe报告。集成build缓存RWS_BUILD_INDUSTRIALROBOT=ON。独立冒烟复用build/govt24-smoke独立配置，只构建受影响目标。命令及最终计数见本目录verification.json。

红证据为新增独立反例在修改前实际失败，green中间记录保留真实失败/跳过；最终验证以final/consumers-final/smoke-final记录为准，不以脚本外层exit0代替测试结果。GUI手工点验及所有者独立验收未执行，既有主动SKIP分列。

集成及相关消费者：1447 passed / 2 skipped / 0 failed；独立构建：749 passed / 1 skipped / 0 failed；治理新分支：17 passed / 0 skipped / 0 failed。跳过项均为既有明确延后到人工harness的GUI展示用例，不将其计为通过。真实Widgets契约测试通过；人工GUI点验尚未执行。

共同摘要/时序的权威组装在当前仓库仍通过注入端口供给；本包补齐字段、规范生成/编码与消费者规则，未虚构默认供给或新增跨域直链。未提供新事实的路径保持保守拒绝/数据不足。完整集成与独立构建日志、红/绿gtest原件和per-exe报告均保留。

黄金JSON资产规则按reuse/policy.md登记：只允许满足清单身份、显式登记、大小及SHA-256的精确数据路径；17项正反例验证，脚本/未知文件仍走原源码约束。reuse-first.log保留旧规则在新版本资产上的真实拒绝；不改写历史门禁结果。
文档/任务/复用门禁及ird_gates均PASS（最终见docs-final4.log、ird-gates.log，exit 0）。复用结果仅为治理检查通过，存量DEBT/UNRESOLVED仍保留；未据此宣称全库算法合规、语义审计或独立验收通过。

提交自查：源码及文档的staged diff --check通过；原始构建日志保留工具输出的行尾空格，日志未作内容清洗。

实施提交：5f7f39147c503cb53c14782b1fa70ea6f96de363。显式Base=612143da54365c277a236d12edd665732f2388ee、Head=5f7f39147c503cb53c14782b1fa70ea6f96de363 的范围检查PASS（151路径），新增提交commit-lint PASS；见task-scope.log及commit-lint.log。本段及对应日志由后续记录提交保存，不改写实施提交或自签独立验收。
