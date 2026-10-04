# UI-T54 构建与测试留痕（基座安装姿态编辑接线批次）

分支 `ui-t54`（基点 redesign-main@5a2245d9）；任务契约 tasks/foundation/UI-T54.json；
DTB §2.11 WP-10-T54 行；units/ui.md v1.63 §13 UI-T54 行。

## 交付物

- 面板承载：建模面板"编辑"页签改模态堆栈（0＝关节区 UI-T53 既有／1＝基座
  安装页）＋模式入口行（关节编辑／基座安装…——基座安装为锚外对象，显式
  入口进页）；基座页＝预设四值组合框＋位置 XYZ（m，世界系）＋自定义 EAA
  XYZ（rad，仅 Custom 启用，呈现精度 6 位小数）＋应用/还原；
  onBasePlacementApplyClicked 整体替换提交（非 Custom 不携 customEaa 的
  UI 侧同口径分流；接受走 L-2 分流；拒绝经 basePlacementEditErrorCodeToken
  就地呈现）＋refreshBasePlacementPane 权威回填＋L-7 门控双半区。
- F-502 折叠用例合并适配：isVisibleTo(面板)→isHidden（"编辑"页签居首后
  工具页恒非当前页——折叠 setVisible 翻转语义零变化）。
- 测试：gui 三用例（WholeReplacementChain／PresetIdentityRotationRejected／
  ReadOnlyDisablesApply）＋对账排除扩展（基座页 SpinBox 内部 QLineEdit 与
  模式钮——两 TU 同步）。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 集成构建（受影响目标全量） | 零错误 | 会话执行记录 |
| 独立冒烟（配置＋构建＋ctest 3/3） | 全绿 | 会话执行记录 |
| sdurws_ird_modeling_test | 323/323（零新增——域原语既有九例不动） | modeling-test.log／.xml |
| sdurws_ird_modeling_gui_test | 25/25（新增 3） | modeling-gui.log／.xml |
| sdurws_ird_modeling_contract_test | 17/17 | 会话执行记录 |
| sdurws_ird_ui_test／ui_contract_test／ui_gui_test | 246/246／32/32／78/78（原生窗口平台） | 会话执行记录 |
| sdurws_ird_requirements_gui_test | 44/44 | 会话执行记录 |
| ird_gates | 本任务零新增依赖/目标——比对口径以基线 101 条存量集为准（域文件增量仅插件呈现面） | 会话执行记录 |
| validate-docs／validate-task（UI-T54.json） | PASS／PASS | 会话执行记录 |

## 诚实边界

- 基座安装为锚外对象（无 ObjectId 树锚）——显式入口进页为 B.1 模式锚外
  适配；ModelRoot 树选可达方案（根身份回填后）随后续批次登记。
- EAA SpinBox 呈现精度 6 位小数（rad）／位置 4 位（m）——呈现精度非语义
  边界，域接受任意有限 double。
- gui 套件口径＝原生窗口平台（gate-all 同口径）；offscreen 下
  WorkbenchShell 布局/窗口几何类用例为环境性误报（F-507 同族教训）。
