# EV-T13 evidence 首轮评审整改

实施基线：4c5e4301f62977f62f33f344c93f678d50d85efe，codex/core-review-fixes；用户已批准按本轮评审结果修改。实施不代表独验或 REUSE-T47 验收。

## 设计与执行顺序

1. 将独立反例纳入正式回归并记录红灯；先修证据适用性/多实例完整性、数据不足明细、证明必填和包络交叉绑定。
2. 配置摘要复用 core::ContentDigester；builder 与 codec 共用本单元既有不变量，解码仅结构/内容验证，修订成员真实性仍由 project 提供。声明与结果拒绝非法枚举。
3. 切片 codec 升为 v2：交换载体保留 snapshotId 溯源，计算身份投影排除整个 snapshotId；v1 解码和旧字节身份保持可读，新旧缓存身份隔离。完整输入基准保留既定条目集，DESIGN-T01 跨方案 comparisonInputId 保持。
4. 检查点请求显式携带 evaluatorKey，经 execution 查询/恢复接线；兼容仅判定，不调度。缓存拒绝缺失身份和 Preview。
5. 增量构建、正式回归、真实接纳与检查点对端回归、独立冒烟、门禁；最后一次同步文档和逐文件/功能凭证，本地提交待所有者另会话独验。

## 保留、删除、适配与归属

保留：evidence 唯一快照/切片/证据/工程裁定契约；runtime 模型/名称真值、policy 策略身份、project 修订/写入、execution 接纳/缓存/恢复；业务算法与真实证明归业务域。保留跨信任边界复验、独立 oracle、合法旧字节和历史结果。

删除：首条同名证据采信、无条件 NA 豁免、只比较相等而不验有效性的兼容捷径、解码绕过不变量、完整 snapshotId 对 v2 计算身份的耦合。

适配：证据实例按 itemId+subject+规范 caseScope 区分，所有已提供实例必须合格；不凭空生成业务必验 subject 集，域仍须完整产出实例。条件项 NA 仍须原因，证据注册结构不得冲突。证明仅校验契约字段/绑定，不新增定理证明系统。

复用：上轮已检索 RobWork/RobWorkSim 无同语义证据入口；BINArchive 字节布局不适用于冻结协议；摘要复用 core 唯一供给。不引入新库、不替换字节原语（Boost.Endian 为可选维护，另批实施）。

## 兼容与回滚

合法 snapshot v1 字节保持；配置错误申报拒绝或由 builder 计算。切片 v2 与 v1 按版本分域，旧结果只读保留，不改写归档；恢复仍需切片/契约/格式/完整性和显式评估键一致。每批本地提交可独立回滚；不合并、不推送、不关闭 T47 或签独验。

## 已实施范围与验证

E01/E04：Profile 冲突和非法状态拒绝；无条件 NA 不再豁免，合法条件 NA 保留；所有 subject/caseScope 实例逐条核对并列出位置，实例去重使用有序集合，不作二次方全表扫描。E02：搜索未果、输入合法性无明细、零样本、降级、无必验工况均有 DataInsufficient 缺失说明，真实接纳/归档保留 Completed。E03：证明通用字段完整性复核，不代替域数学依据。E05/E06：配置摘要从真实字节计算，切片已消费的快照引用精确绑定；两种快照解码共用内容冻结规则，拒绝非法布尔/集合/重复/排序。E07/E08：任务锚、快照、Profile 三元组交叉验证以及依赖/模式词表检查。E09：v2 计算身份与溯源快照分离。E10：缓存有效身份/模式防御及检查点显式请求键贯穿 execution。

所有测试在本工作树构建和执行，Release/x64，源码/构建目录锚核对正确，RWS_BUILD_INDUSTRIALROBOT=ON。命令脚本、原始输出、XML 和每 exe 原生 JSON 见 raw/；测试均显式传 --gtest_output 与 --ird_report，native exit=0 才记 PASS。各定向运行过滤均非零命中。

| 验证 | passed | skipped | failed | 结果/原始证据 |
| --- | ---: | ---: | ---: | --- |
| 修复前正式反例首轮（32 项） | 2 | 0 | 30 | 预期 RED，raw/red.*；输入原文 red-regression-input.cpp.txt |
| 首批实现（32 项） | 29 | 0 | 3 | 中间结果，raw/green1.*；剩兼容性反例随后修复 |
| 旧基线诊断（208 项） | 157 | 0 | 51 | 中间 FAIL，raw/existing1.*；配置/快照夹具及v2期望随后订正 |
| evidence 最终完整报告目标 | 245 | 0 | 0 | PASS，raw/sdurws_ird_evidence_test.* |
| evidence 最终完整契约报告目标 | 14 | 0 | 0 | PASS，raw/sdurws_ird_evidence_contract_test.*；旧 v1 黄金原文零修改，另以独立字节变换核对 v2 |
| execution 接纳/检查点/缓存 | 48 | 0 | 0 | PASS，raw/execution1.* |
| 新增真实接纳与错键拒绝 | 3 | 0 | 0 | PASS，raw/execution2.*；其中2项与上行重叠，唯一用例总数不相加 |
| execution 对端 CacheJudge 契约 | 3 | 0 | 0 | PASS，raw/execution-contract.* |
| optimization 检查点/暂停/恢复 | 7 | 0 | 0 | PASS，raw/optimization3.*；早期夹具失配见 optimization*.xml，不隐去失败 |
| optimization 同版本恢复性能基准 | 1 | 0 | 0 | PASS，raw/perf.*；25.141s，10,000 Quick+100 Verified，80,800 指标与完整参照逐位相等，maxRelDiff=0，未改标准/容差 |
| 独立 core-smoke 整改回归 | 37 | 0 | 0 | PASS，raw/smoke.* |
| 独立 core-smoke 契约 | 14 | 0 | 0 | PASS，raw/smoke-contract.* |

首次构建中的拼写/夹具/凭证问题曾失败；保留的中间失败报告只作定位证据，不算 PASS。_report 已真实执行完整套件，未再单独重复同套件。独立构建属于构建模式冒烟，**不是独立验收**。

## 最终门禁

集成 Release/x64 构建 evidence 两目标及 `ird_gates` 均 PASS（native exit=0），结构/依赖红线扫描零命中，日志与 VS x64 入口见 `raw/final-build-gates.log`、`raw/final-build-gates.cmd`。`validate-docs.ps1 -TaskFile .../EV-T13.json` 的任务、文档及复用治理检查 PASS，原始输出见 `raw/validate-docs-final2.log`。

复用检查仅为 governance only：DEBT=16、UNRESOLVED=1399，既有 AD-PENDING 不产生任何权限批准，也不代表算法合规或债务关闭。DeepEvidence 明确 NOT RUN。切片公共注释已同步 v1/v2 身份差异，不改变既有测试语义或容差。
## 送验、兼容及前序影响

本包公共值结构追加 codecVersion、manifestIndex 与检查点请求评估键，要求相关消费者重编译；既有聚合前字段顺序保留，不宣称二进制 ABI 不变。快照合法 v1 与切片历史 v1 字节/摘要保持；非法历史配置身份不予洗白。新请求默认 v2，不自动改写/迁移旧缓存或检查点；新旧切片不相等时须重算，兼容不等于恢复成功。回滚应整体回滚本包实现/测试/设计/凭证，保留已写历史结果，不能把新 v2 数据当 v1 解码。

core 的 ContentDigester 仍是唯一摘要供给；前序 core 修复未改。testkit 的原生报告/独立黄金装载沿用 TK-T13 成果，未改其断言、容差或状态。project 保持磁盘与修订 owner、产品代码零修改；新增接纳测试通过真实 ProjectStore 留存 CompletedDI。optimization 仅更新测试/基准夹具的冻结配置、任务锚及请求键，未修改业务算法。

下一单元 runtime 仍须确保模型/名称/编译真值与快照同源，实际消费的 runtime.model-identity/robwork-baseline 值真实传入；本轮并未证明 CR-05 同值生产接线（既有 F-061）。主机必须在快照中登记实际消费的配置子集与策略/样本引用，不能靠调用方任意申报身份。

未验证疑点和既有争议未擅自转为修复：Currentness 调用方违反来源前置的真实接线、comparison 空身份的实际消费、证明对象闭包/非Completed证明留存及 Must 与证明冲突仍按原审查边界处理；F-056/F-061/F-067 不重复建账或关闭。低层字节原语合并未实施，避免无关字节与依赖变化。

EV-T13 保持 ready、实施待所有者另会话独验；REUSE-T47、历史验收与治理批准零改动。独验须核对本包起始 SHA 至实施提交的显式范围、真实公共入口、v1/v2字节、接纳归档与恢复对端以及门禁原始输出，不能只复用本轮自查判定。全库 DeepEvidence、无关单元完整测试及远程同步均 NOT RUN。

在所列版本、范围和已执行验证内，未发现其他问题。
