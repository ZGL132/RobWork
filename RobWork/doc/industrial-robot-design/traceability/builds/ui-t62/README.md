# UI-T62 构建与测试留痕（治理清扫批次）

分支 `ui-t62`（基点 redesign-main@b19ff973）；任务契约 tasks/foundation/UI-T62.json；
DTB §2.11 WP-10-T62 行＋§5.1 两行（回归留痕全集口径＋契约模板口径）；
units/ui.md v1.74 §13 UI-T62 行。

## 交付物

- F-521：refreshPreview 缺席文案按 kind 分流（清单轨闭包措辞/XML 轨措辞）
  ＋PackageChecklistPreviewChain 断言适配。
- F-513②：verify-task.ps1 散文条目显式化（[manual] 人工核对口径计数；
  Parse/CommandNotFound 类错误判散文、其余异常重抛；汇总行分列——UI-T61
  契约实测 0 机器/4 人工）。
- F-513①＋F-511/F-512：DTB §5.1 '回归留痕全集口径'行；F-503/F-504：
  '契约模板口径'行。
- 文档健康：ui.md 版本行堆叠拆分（七列堆叠→规范行＋六历史残段行照录，
  版本头失佚不臆补——F-514 防复发落实）＋全文件表格分列审计（§13 十三行
  历史分列不齐→F-522 登记留治理）＋F-517 验证口径行闭合管道修复（UI-T60
  D-1 拆分遗留）。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 集成构建（受影响目标） | 零错误 | 会话执行记录 |
| 独立冒烟（gate-all standalone 树） | 构建零错误；modeling_test 274/274＋modeling_gui_test 35/35 全绿 | gate-all.log＋冒烟树实测 |
| sdurws_ird_modeling_test（集成） | 333/333 | modeling-test.log／.xml |
| sdurws_ird_modeling_gui_test（集成） | 39/39 | modeling-gui.log／.xml |
| modeling_contract／ui_test／ui_contract／ui_gui／requirements_gui | 17／246／32／78／44 全绿 | 会话执行记录（未触碰套件以验收复现为准——本批登记口径） |
| verify-task.ps1 行为实测 | UI-T61 契约→0 机器/4 散文人工核对显式分列 | 会话执行记录 |
| ird_gates | 引擎汇总 101 命中＝基线存量集口径；归一化多重集与 UI-T61 基线集 **diff=0**；命中集零项涉改动文件 | ird-gates-branch.log／ird-gates-branch-hits-normalized.txt（比对基线＝builds/ui-t61/ 同名文件）＋gate-all.log（留痕全集口径首执） |
| validate-docs／validate-task（UI-T62.json） | PASS（20 units/240 task files）／PASS | 会话执行记录（显式核验输出行——UI-T60 教训落实） |

## 诚实边界

- ui.md 版本行残段为历史截段照录（版本头失佚不臆补）——历史堆叠的版本
  头在既有历史中已失佚，本批不臆测补注。
- ui.md §13 十三行历史分列不齐为既有面（非本批引入；分列审计工具发现）
  ——登记 F-522 留治理，逐行修需判读历史行语义。
- F-506/F-507/F-508/F-510 维持 open（owner 口径＝教训条目即处置，无新
  动作——本批仅 F-511/F-512 因口径行落地而翻转）。
