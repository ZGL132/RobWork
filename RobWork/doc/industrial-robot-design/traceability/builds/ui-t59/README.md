# UI-T59 构建与测试留痕（F-498 预览半区批次）

分支 `ui-t59`（基点 redesign-main@de1d3018）；任务契约 tasks/foundation/UI-T59.json；
DTB §2.11 WP-10-T59 行；units/modeling.md v0.37；units/ui.md v1.71 §13 UI-T59 行。

## 交付物

- 域导出面（Package.hpp/.cpp）：exportPreviewXml 四类（SerialDeviceXml/
  SceneXml/CollisionXml/DwcXml＋token＋PreviewExportOutcome——快照只读消费
  零修订零 I/O；Scene 面 T_world_frame 经 worldTframe 只读推导；Collision
  面能力位＋缺席注记；Dwc 面能力缺席拒绝；确定性同快照同字节）＋
  buildDevicesSection/buildBodiesSection 共用段抽取（NFR-MNT-04）。
- 模块接线（ModelingUiModule）：bindWorkCellPreview＋★appliedPreview 推送
  缺口修复（五位点——UI-T41 A3 残留：预览页在产品中恒停留构造初态）。
- ★F-518 断链修复（ModelingPluginAssembly）：补登 bindWorkCellExport/
  bindWorkCellPreview 装配门面转发器——UI-T56 宿主直调模块面但漏登装配
  面，sdurws_ird_ui_plugin 自 UI-T56 起编译断链（该目标不在受影响测试目标
  集内潜伏未察；本批实施时构建 ui_plugin 发现并修复）。
- 面板承载（ModelingPanelWidget）：预览页多类型（combo 五类＋来源头行＋
  复制全部钮＋refreshPreview 单一渲染出口分轨——XML 类唯一来源＝域导出面，
  UI 零拼装强制点）。
- 宿主接源（UiPlugin）：bindWorkCellPreview（快照现取零第二编译路径；
  来源头行＝修订规范文本＋序号＋模型身份规范文本＋快照创建时刻＋来源对象）。
- 测试：PackageWorkCellTest PreviewExportsFourKinds_UI_T59＋gui
  PreviewPane_MultiTypeChain_UI_T59＋HostWiringGuiTest
  AppliedPreviewPushChain_Fix_UI_T59（推送缺口修复的核心断言面）。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 集成构建（含本批修复的 sdurws_ird_ui_plugin） | 零错误 | 会话执行记录 |
| 独立冒烟（gate-all standalone 树） \| 构建零错误；modeling_test 271/271＋modeling_gui 31/31〔修复 token 头内联落位后〕 | gate-all 会话执行记录 |
| sdurws_ird_modeling_test（集成） | 330/330（新增 1） | 会话执行记录＋gtest XML |
| sdurws_ird_modeling_gui_test（集成） | 34/34（新增 2） | 会话执行记录 |
| modeling_contract／ui_test／ui_contract／ui_gui／requirements_gui／requirements_test | 17／246／32／78／44／200+1skip 全绿 | 会话执行记录 |
| ird_gates | 引擎汇总 101 命中＝基线存量集口径；归一化多重集与 UI-T58 基线集 **diff=0**（逐行全等）；命中集零项涉改动文件 | ird-gates-branch.log／ird-gates-branch-hits-normalized.txt（比对基线＝builds/ui-t58/ 同名文件） |
| validate-docs／validate-task（UI-T59.json） | PASS（20 units/236 task files）／PASS | 会话执行记录 |

## 诚实边界

- 预览类型下拉建议六类中五类落位——**规范包清单类型未承接**（需已应用
  修订闭包的 manifest 清单面，超出本批域导出面边界——F-498 余项注随合入
  段治理提交登记）。
- F-518 断链自 UI-T56 潜伏两轮验收未察的根因＝sdurws_ird_ui_plugin 不在
  受影响测试目标集内（ui_test 等测试目标不链接插件 DLL）——UI-T56 的
  "集成模式构建零错误"声明对该目标而言失实；后续宿主接线新增模块方法
  时须同步补门面转发并构建 ui_plugin 验证（契约 knownPitfalls 已登记）。
- gui 套件口径＝原生窗口平台（gate-all 同口径）。
- gate-all 汇总 60/61 的唯一 FAIL＝ird_gates 步存量命中按设计非零退出
  （UI-T58 留痕 README 同款口径解释）。
