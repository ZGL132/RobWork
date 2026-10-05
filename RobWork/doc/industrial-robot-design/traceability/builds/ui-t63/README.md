# UI-T63 构建与测试留痕（从零创建批次——批次 A）

分支 `ui-t63`（基点 redesign-main@64f84a74）；任务契约 tasks/foundation/UI-T63.json；
DTB §2.11 WP-10-T63 行；units/modeling.md v0.40；units/ui.md v1.76 §13 UI-T63 行。

## 交付物

- 命令参数化：new-from-template 分流（chooseItem 选单——generic-6r 现行为
  ｜custom-chain 声明路径；未接线装配缺陷 fail-closed）＋
  ModelingDialogHost::declareCustomChain 缝（QDialog 六行十三列表单——类型
  锁 Revolute＋轴线 xyz＋零位＋限位下/上＋Origin xyz/r/p/y；逐格有限性校
  验拒绝关闭；T-MDL-1 J1 种子预填 to_chars 文本）＋CustomChainJointSpec/
  CustomChainDeclaration 值面＋ModelingFlowDeps::reseedCustomChain。
- 模块组合：reseedCustomChain（恰六轴校验→createDraft(custom-chain 1 轴种
  子)→addJointAt 补齐→applyJointFieldEdit 逐轴 Axis/ZeroOffset/Bounds/
  Origin UserProvided——拒绝 fresh 丢弃会话不变；提交＝根身份保留＋基线/
  预览失效＋声明编辑入 changes 待应用账面）＋deps 接线。
- 测试：FlowsTest 两用例＋HostWiringGuiTest 组合用例。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 集成构建（受影响目标） | 零错误 | 会话执行记录 |
| 独立冒烟（gate-all standalone 树） | 构建零错误；modeling_test 276/276（274+2）＋modeling_gui_test 35/35 全绿 | gate-all.log＋冒烟树实测 |
| sdurws_ird_modeling_test（集成） | 335/335（新增 2） | modeling-test.log／.xml |
| sdurws_ird_modeling_gui_test（集成） | 40/40（新增 1） | modeling-gui.log／.xml |
| modeling_contract／ui_test／ui_contract／ui_gui／requirements_gui | 17／246／32／78／44 全绿 | 会话执行记录（未触碰套件以验收复现为准——DTB §5.1 登记口径） |
| ird_gates | 引擎汇总 101 命中＝基线存量集口径；归一化多重集与 UI-T62 基线集 **diff=0**；命中集零项涉改动文件 | ird-gates-branch.log／ird-gates-branch-hits-normalized.txt（比对基线＝builds/ui-t62/ 同名文件）＋gate-all.log |
| validate-docs／validate-task（UI-T63.json） | PASS（20 units/241 task files）／PASS（显式核验输出行） | 会话执行记录 |

## 诚实边界

- 红线不触：4/5 轴与含 prismatic 链的创建确认仍受 creationEntryGuard
  （AT-20 需求分期口径）——声明面恰六轴校验＋类型锁 Revolute，本命令不
  新增放行；放开归批次 B（所有者裁决）。
- 类型枚举行编辑（既有关节类型转换）仍为 UI-T53 登记诚实边界——本批类型
  在声明时给定（锁 Revolute）。
- 七轴模板 P-03 数值未冻结（enabled=false 登记不启用——O-27）。
- 声明编辑保留在 changes 账面（~29 条待应用）＝与 generic-6r 重种子
  （changes 清零）刻意不同的语义：声明是真编辑非模板种子。
