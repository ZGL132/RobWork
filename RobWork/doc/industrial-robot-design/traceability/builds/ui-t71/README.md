# UI-T71 构建与测试留痕（F-539 定位出口生产接线批次）

分支 ui-t71（基点 redesign-main@76e9f554）；任务契约 tasks/foundation/UI-T71.json；
DTB §2.11 WP-10-T71 行；units/ui.md §13 UI-T71 行。

## 验证记录

| 项 | 结果 |
| --- | --- |
| 受影响目标集成构建（两 plugin＋两 gui_test＋studio 静态链接） | 零错误 |
| modeling_gui_test | 42/42（新增 LocateSinkProductionWiring_ReadinessRowClickFocusesTree_UI_T71） |
| requirements_gui_test | 45/45（新增 LocateSinkProductionWiring_ValidationRowClickFocusesTree_UI_T71） |
| modeling_test／modeling_contract／ui_test／ui_contract | 339／17／254／32 全绿 |
| 做红自证（requirements 侧 sink 体改空→用例红→还原复绿） | 红/绿双向实证 |
| 真机 requirements-tour | TOUR_PASS（exit=0）——7 帧截图全异哈希（E-4 加固保持） |
| 真机 modeling-tour（回归） | MTOUR_PASS（exit=0；F-536 gap 断言保持预期形态） |
| ird_gates | 零命中 exit 0 |

## 诚实边界

- tour 7.5 拍未升级落点读回：快乐路径 Warning 行无 subject（Readiness 规格）——落点钉扎由两 gui 用例承载（伪造带锚行同形注入，被测链全生产代码）。
- 复跑事故如实登记：首批回归巡检误用 ui-t68 驱动覆盖已合入批次留痕——git checkout 还原后本批改用 ui-t71 驱动重取（ui-t68 证据零改动入库）。

## 返工更正（acc/ui-t71/1 B-1——attempt 1 fail 单一阻断）

- 原交付叙事「modeling 就绪表从未 setColumnCount——锚列 setText 静默丢弃＝定位链第二断点」为**错误技术断言**（验收 M-B 变异＋运行时探针证伪）：QTreeWidgetItem 逐项值存于条目自身、不受树 columnCount 上限——锚文本从未丢失，modeling 侧唯一断点＝sink 缺席（本批接线即修复）。
- setColumnCount(2)＋hideColumn(1) 实为 UX-02 声明式呈现形态对齐（非功能修复）——孪生用例补 ASSERT_EQ(columnCount,2) 钉扎（B-1 返工②）。
- 本 README 首版未含该失实表述（失实面在实施提交正文/契约/DTB/ui.md/代码注释——各处已随返工更正，提交正文不可改写以返工提交更正注承载）。
