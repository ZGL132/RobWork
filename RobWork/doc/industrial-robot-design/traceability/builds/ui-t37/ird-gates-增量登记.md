# UI-T37 R1 门禁命中集增量登记（ird_gates）

| 字段 | 值 |
| --- | --- |
| 批次 | UI-T37 R1（需求面板呈现层现代化——主题与卡片基建轮） |
| 基线 | redesign-main@ed215912 谱系登记态 96 处命中（R1×13＋R3×53＋R4×3＋R5×1；机器消费面＝builds/ui-t35/ird-gates-head.log 同族基线） |
| 本次 | 98 处命中（恰增 2 条，零意外新增） |
| 增量条目 | ①`ui/include/sdurws/ird/ui/UiTheme.hpp`——R-3（ui 产品面疑似包含 Qt 类头，Q+大写约定：`<QString>/<QVBoxLayout>/<QWidget>`）②`ui/src/UiTheme.cpp`——R-3（`<QHBoxLayout>/<QLabel>` 等） |
| 依据 | NFR-MNT-01 ui 例外类既定形态（UI-T20/T21/T22/T24 各自恰增 R3＋登记件同族先例）——UiTheme 为 UI-T37 契约 acceptance 1 的主题基建公共构件（调色板/QSS/卡片容器），公共头即 Qt 呈现契约面，非计算逻辑渗入；零新增目标（UiTheme.cpp 编入既有 sdurws_ird_ui 源集），白名单数据面零改动 |
| 机器消费面 | 本目录 ird-gates-head.log（98 处命中全文）＋build-all.log（全量构建日志） |
