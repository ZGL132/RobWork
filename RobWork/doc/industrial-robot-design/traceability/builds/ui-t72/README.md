# UI-T72 构建与测试留痕（F-541 modeling 通道取证加固批次）

分支 ui-t72（基点 redesign-main@988336be）；任务契约 tasks/foundation/UI-T72.json；
DTB §2.11 WP-10-T72 行；units/ui.md v1.84 §13 UI-T72 行。

## 交付物

- maybeRunModelingTour 拍 1 E-4 同款三件套：建模 Dock show/raise＋step1
  modeling-panel-visible 断言＋宿主窗体最大化（G-C/S-B 双实证静窗对偶面闭合）。
- F-542/S-2 粒度恢复示范：logs/ 逐套件原始 stdout 六件随批入库。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 受影响目标集成构建（sdurws_ird_studio——UiPlugin.cpp 唯一编译目标对） | 零错误 | 会话执行记录 |
| 真机 modeling-tour | MTOUR_PASS（exit=0；panel-visible 在场） | smoke-tour-modeling/（console＋driver-verdict＋6 PNG＋irdbundle） |
| 截图唯一性（F-541 核心面） | 六帧 6/6 全同→5 异值；唯一相同对 mtour-4==mtour-5＝F-536 已知缺口的功能性视觉签名（apply 被拒→无状态变化——非静窗退化） | smoke-tour-modeling/*.png（md5 五值） |
| 真机 requirements-tour（回归） | TOUR_PASS（exit=0；E-4 加固保持） | smoke-tour/（console＋driver-verdict＋7 PNG） |
| 六套件回归（logs/ 逐套件原始 stdout——F-542 粒度） | modeling_gui 42＋requirements_gui 45＋modeling_test 339＋modeling_contract 17＋ui_test 254＋ui_contract 32 全绿 | logs/*.log 六件 |
| ird_gates | 零命中 exit 0 | ird-gates.log |
| validate-docs／validate-task（UI-T72.json） | PASS（20 units/310 task files）／PASS | 会话执行记录 |

## 诚实边界

- mtour-4==mtour-5 帧同哈希为 F-536 已知装配缺口的功能性零视觉变化（apply 被拒 committed=false），非静窗退化——面板可见性由 panel-visible 断言独立钉扎；F-536 修复后该帧自然分化。
- 六套件为 tour 变更的回归面（UiPlugin.cpp 仅 plugin/studio 两目标编译——测试不触该 TU），逐套件 log 即 F-542 粒度本体；gtest XML 未随批（逐套件 stdout 为留痕本体口径）。
- 巡检驱动复跑覆写风险：全程只用本批 ui-t72 驱动（ui-t68/ui-t71 事故先例在案）。
