# reporting 测试套件与可控替身说明（RPT-T11）

本目录是 `sdurws_ird_reporting_test`（单元内测试）的源码根。本文件登记测试
套件的组织方式、**四具名替身**与**替身边界声明**（units/reporting.md §10
"替身边界声明"、§10.1 RP-STATE-4 行的文档留痕位——任务契约
tasks/foundation/RPT-T11.json acceptance 3）。

## 1. 测试目标与套件组织

| 目标 | 内容 | 依据 |
| --- | --- | --- |
| `sdurws_ird_reporting_test` | 本目录全部用例——各任务单元内验收用例（RPT-T02~T10）＋ RP-\* 契约套件（ReportingContractSuiteTest.cpp——§10.1 用例矩阵逐条的统一用例体） | §3.4、§11 RPT-T11 行 |
| `sdurws_ird_reporting_contract_test` | `../contract_test/`——跨单元契约面（构建图边界/上游值类型消费/协作编排） | §3.4 |

测试入口为自有 main（`TestMainReport.cpp`）：安装 testkit
`installTestRecordListener`，`IRD_TEST_INFO` 登记的需求/AT 追溯聚合为
`ird-test-report.json`，与 gtest XML 并存（testkit §7.2/§7.3；EX-T09/
PRJ-T15 同款先例）。链接面遵循 T-1 允许形态
`{被测产品目标, sdurws_ird_testkit, gtest}`（testkit §2.4）——**产品目标
`sdurws_ird_reporting` 零 testkit 边**（T-1 红线；具名自证在
`contract_test/LinkageContractTest.cpp::NoTestkitEdge`）。

## 2. 四具名替身（§10 可控替身清单）

| 具名替身 | 正本 | 契约面 | 可注入 |
| --- | --- | --- | --- |
| `ScriptedResultSource` | `ScriptedResultSource.hpp` | `IReportResultSource`（§9.1 四方法） | envelope/当前性/资格/复现块脚本；复现块缺项 |
| `ScriptedSectionProvider` | `ScriptedSectionProvider.hpp` | `IReportSectionProvider`（§9.2 三方法） | 章节内容/缺项/不适用（§5.3 四态任一） |
| `FakeArtifactSink` | `FakeArtifactSink.hpp` | `IReportArtifactSink`＋`IReportPublishedIndex`（§9.6） | 冲突/磁盘满/清理失败/只读/占用 |
| `FakeArchiveWriter` | `FakeArchiveWriter.hpp` | `IArchiveWriter`（§9.6 三方法） | open/addEntry/finish 三类故障 |

RPT-T11 已把 RPT-T03~T10 各任务散落的局部夹具（BuilderTest.cpp 局部
`ScriptedResultSource`/`ScriptedSectionProvider`、SectionProviderTest.cpp
局部 `ScriptedSectionProvider`、BundleTest.cpp 局部 `FakeResultSource`、
BuilderCrossUnitContractTest.cpp 局部 `MinimalResultSource`）收敛/替换为
上述四具名正本（任务契约 acceptance 2——各任务"随 RPT-T11 收口"承诺在本
任务兑现）。不在四具名清单内的辅助夹具（`FakeQueryPort` 等项目查询端口
投影、io 注入缝的最小 canonical writer/reader、取消令牌）保持各套件自持，
不在收敛范围（§10 清单原文仅列四具名）。

## 3. 替身边界声明（RP-STATE-4；EV-REG-3 同源）

**本目录全部替身输出仅验证 reporting 侧契约（绑定校验/状态呈现/一致性/
幂等），不构成任何运动学/轨迹/动力学/选型结果的业务正确性证明。**

具体约束（与四具名替身头注释同源，机器可核对的自证在
`ReportingContractSuiteTest.cpp::RP_STATE_4_*` 用例）：

1. **替身 envelope 合法组合-only**：一切 envelope 一律经 evidence
   `ResultEnvelope::make` 构造——非法组合在 evidence 构造边界即被拒绝，
   reporting 不自造非法样本（§10 替身边界声明原文）。唯一例外＝Preview
   包络（表 1/表 3 行 4——evidence 构造边界本就拒绝，无法经 make() 产出），
   仅 RP-MDL-2 的"Preview 结果到达注入面"第二道闸用例以聚合初始化受控
   构造并在用例内显式注明（RPT-T05 先例形态）。
2. **不伪造报告内容**：工件字节来自被测链路的真实渲染产物（FakeArtifactSink
   不生成任何报告字节）；脚本数值为确定性固定值，禁随机（同脚本同
   contentIdentity——RP-MDL-1 的前提）。
3. **不构成业务正确性证明**：替身章节内容/结果判定的业务语义（如
   "ik-converged" 条目、"1.5 mm" 数值）是结构契约的载体，不宣称任何真实
   机构的可达性/碰撞/动力学结论；真实域章节内容归 RPT-T14/T15 真实注册
   联调（§12.3：真实章节内容不得以替身数据冒充验收——AT-32 必须真实链路）。
4. **上游契约基线（P-RPT-9）**：替身消费的 evidence/core 契约以各卡 v0.1
   Draft 为基线；冻结出 diff 后按影响面同步替身脚本面与用例（本节即同步
   义务的登记位）。
5. **EvidenceItemStatus 五值词表（O-13）**：Missing/Invalid/Unverified/
   NotApplicable/Satisfied 各有其位不合并——呈现断言在 RP-STATE-2 与
   RP-CONS 组锁定；词表实现承载状态随上游裁决同步（DTB §5.4）。
6. **测试报告边界（消费纪律）**：TestRecord/ird-test-report.json 与
   ReviewReport 数据契约无共享（testkit §2.3 对照表 reporting 行冻结口径）
   ——测试报告不经 reporting 生成；ReviewReport 的渲染器/导出器不用于
   测试结果序列化。

## 4. 用例命名与追溯（DTB §5.5）

- 单元内用例：`<Suite>.<主题>_<锚点>_<需求/AT 追溯>`（各任务既有约定）；
- RP-\* 契约套件：`ReportingContractSuite.RP_<组>_<行号>_<主题>`，测试体
  首行 `IRD_TEST_INFO("<需求 IDs>", {<AT 集合>}, std::nullopt)` 登记
  §10.1 矩阵行"需求/AT 依据"列（ird-test-report.json 承载）。
