# UI-T57 构建与测试留痕（TCP 列表结构化编辑批次）

分支 `ui-t57`（基点 redesign-main@53c5fcc8）；任务契约 tasks/foundation/UI-T57.json；
DTB §2.11 WP-10-T57 行；units/modeling.md v0.35；units/ui.md v1.69 §13 UI-T57 行。

## 交付物

- 域原语（Parts.hpp/.cpp）：TcpEditErrorCode 六值＋token＋TcpEditError＋四
  原语（applyTcpAddEdit／applyTcpRemoveEdit〔引用保护先于最后一条保护〕／
  applyTcpOffsetEdit／applyDefaultTcpSwitchEdit——全拒绝路径工作集字节不变）。
- 面板承载：工具页 TCP 段（combo 选 TCP＋新增/删除/设为默认三钮＋TCP
  offset 六行 ParamTablePanel——字段集常量不重建〔UI-T55 教训沿用〕；
  QSignalBlocker 防重入；自动键 tcp-N 首空位）；L-7 门控双半区。
- 测试：PartsTest TcpListEdits_AddRemoveOffsetDefault_UI_T57＋gui 三用例
  （全链／守卫／L-7）＋对账排除。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 集成构建（受影响目标全量） | 零错误 | 会话执行记录 |
| 独立冒烟（配置＋构建＋ctest 3/3） | 全绿 | 会话执行记录 |
| sdurws_ird_modeling_test | 328/328（新增 1） | modeling-test.log／.xml |
| sdurws_ird_modeling_gui_test | 31/31（新增 3） | modeling-gui.log／.xml |
| modeling_contract／ui_test／ui_contract／ui_gui／requirements_gui | 17／246／32／78／44 全绿 | 会话执行记录 |
| ird_gates | 零新增依赖/目标——基线 101 条存量集口径，比对见验收记录 | 会话执行记录 |
| validate-docs／validate-task（UI-T57.json） | PASS／PASS | 会话执行记录 |

## 诚实边界

- displayName 编辑不在本批（新增时呈现缺省"TCP <key>"——文本行编辑归
  后续批次）。
- 删除守卫次序＝引用保护先于最后一条保护（实测定序——指引更明确）。
- gui 套件口径＝原生窗口平台（gate-all 同口径）。
