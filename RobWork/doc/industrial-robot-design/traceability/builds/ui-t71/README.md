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
