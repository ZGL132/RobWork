# UI-T61 构建与测试留痕（规范包清单预览类型批次——F-498 余项收尾）

分支 `ui-t61`（基点 redesign-main@6618ce02）；任务契约 tasks/foundation/UI-T61.json；
DTB §2.11 WP-10-T61 行；units/modeling.md v0.39；units/ui.md v1.73 §13 UI-T61 行。

## 交付物

- 域渲染面（Package.hpp/.cpp）：packageChecklistText（roundtripChecklist
  单一实现的确定性文本渲染——NFR-MNT-04 渲染侧单点；标题＋条目计数行＋
  逐行 group | path | valueText 管道分隔；纯函数同输入同字节）。
- 面板：预览页 combo 第六项"规范包清单"（token package-checklist）。
- 模块本地应答（ModelingUiModule attachPanelWiring 分流）：数据源＝会话基
  线定格内容（已应用修订的草稿值视图——D-MDL-10），不经宿主编译快照回调；
  headerLine＝来源修订＋定格语义＋对象计数；缺席＝未应用/撤销重做失效回落。
- 测试：PackageTest PackageChecklistText_Deterministic_UI_T61＋gui
  MultiTypeChain count 6 适配＋HostWiringGuiTest
  PackageChecklistPreviewChain_UI_T61（缺席→定格呈现→失效回落三段）。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 集成构建（受影响目标） | 零错误 | 会话执行记录 |
| 独立冒烟（gate-all standalone 树） | 构建零错误；modeling_test 274/274（273+1）＋modeling_gui_test 35/35 全绿 | gate-all 会话执行记录＋冒烟树实测 |
| sdurws_ird_modeling_test（集成） | 333/333（新增 1） | modeling-test.log／.xml |
| sdurws_ird_modeling_gui_test（集成） | 39/39（新增 1） | modeling-gui.log／.xml |
| modeling_contract／ui_test／ui_contract／ui_gui／requirements_gui | 17／246／32／78／44 全绿 | 会话执行记录 |
| ird_gates | 引擎汇总 101 命中＝基线存量集口径；归一化多重集与 UI-T60 基线集 **diff=0**；命中集零项涉改动文件 | ird-gates-branch.log／ird-gates-branch-hits-normalized.txt（比对基线＝builds/ui-t60/ 同名文件） |
| validate-docs／validate-task（UI-T61.json） | PASS（20 units/239 task files）／PASS | 会话执行记录 |

## 诚实边界

- 清单数据源＝会话基线定格内容（已应用修订的草稿值视图）——与 MDL-20 导
  出的闭包同内容源（应用时刻定格）；digest 级 manifest 面归 MDL-20 导出命
  令本体（预览呈现 V-20 逐项清单语义——来源修订/定格语义/对象计数在来源
  头行明确，正文域确定性渲染）。
- 撤销/重做后基线失效＝清单缺席（D-MDL-10 不虚构内容——与预览页全局空态
  语义一致）。
- gui 套件口径＝原生窗口平台（gate-all 同口径）。
