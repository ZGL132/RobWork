# UI-T58 构建与测试留痕（TCP 显示名文本行编辑批次）

分支 `ui-t58`（基点 redesign-main@66117de7）；任务契约 tasks/foundation/UI-T58.json；
DTB §2.11 WP-10-T58 行；units/modeling.md v0.36；units/ui.md v1.70 §13 UI-T58 行。

## 交付物

- 域原语（Parts.hpp/.cpp）：第五原语 applyTcpDisplayNameEdit（越界 fail-fast→
  键存在 KeyNotFound→displayName 直写＋恰一条变更记录；仅呈现字段无内容
  校验——空串接受＝呈现回落按键；零新增枚举值）；F-516④ applyTcpRemoveEdit
  注释次序随代码修正（引用保护先于最后一条保护）。
- 面板承载：工具页 TCP 段显示名行（QLineEdit ird_modeling_tcp_displayname＋
  应用钮 ird_modeling_tcp_displayname_apply——placeholder 空值回落；基线随
  refreshTcpPane/onTcpComboChanged 回填；零差异提交＝零修订零动作〔PA-2〕；
  setWritable L-7 即时同步）。
- 测试：PartsTest TcpDisplayNameEdit_PresentationOnlyField_UI_T58＋UI_T57 域
  用例增 ValueNotFinite 直断言（F-516⑦）＋gui TcpDisplayNameEdit_Chain_UI_T58
  ＋TcpPane_ReadOnlyDisables_UI_T57 断言面补全（F-516③）。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 集成构建（受影响目标） | 零错误 | gate-all.log（集成段） |
| 独立冒烟（gate-all 配置 standalone 树） | 构建零错误；modeling_test 271/271＋modeling_gui_test 30/30（冒烟模式口径——集成模式多 58/2 例为 sdurw_kinematics 等框架链接专属门控用例；gui 冒烟需 QT_QPA_PLATFORM_PLUGIN_PATH——F-007/F-475 族运行时前置） | gate-all.log＋冒烟树（%TEMP%/ird-gate-smoke——gate-all 因 ird_gates 步非零而保留现场） |
| sdurws_ird_modeling_test（集成） | 329/329（新增 1） | modeling-test.log／.xml |
| sdurws_ird_modeling_gui_test（集成） | 32/32（新增 1） | modeling-gui.log／.xml |
| modeling_contract／ui_test／ui_contract／ui_gui／requirements_gui（集成） | 17／246／32／78／44 全绿 | gate-all.log（集成段逐目标）＋会话执行记录 |
| ird_gates | 引擎汇总 101 命中＝基线 101 条存量集口径；命中集零项涉及本批改动文件（Parts.cpp/.hpp、ModelingPanelWidget.*）——归一化多重集比对见验收记录 | ird-gates-branch.log／ird-gates-branch-hits-normalized.txt |
| validate-docs／validate-task（UI-T58.json） | PASS／PASS | 会话执行记录 |

### gate-all 汇总口径（60/61 的诚实解释）

- gate-all 61 步中唯一 FAIL＝第 2 步 ird_gates（存量 101 命中使门禁目标按
  设计以非零退出——WP-01 起既有状况）；其余 60 步（集成 27 目标构建＋测试
  ＋冒烟配置构建）全过。
- ird_gates 的注册验证口径＝"零新增"（多重集比对），非"退出码 0"——本批
  侧证据：101＝基线计数＋命中集零项涉改动文件；双侧逐项比对归验收记录。

## 诚实边界

- 零差异提交＝零修订：应用钮点击先与工作集现值比对，无差异不产生变更
  记录（PA-2——无信息量历史不产生）。
- ui.md 版本行 v1.69 起的单行堆叠式（悬挂列，F-514/F-516① 同类隐患）为
  既有状况——本批不臆改历史行，仅以规范两列行新增 v1.70；堆叠行拆分归
  治理批次。
- gui 套件口径＝原生窗口平台（gate-all 同口径）。
- 只读会话下基座/场景页面板的门控归各自刷新入口（构造期预建面板从未被
  选中故不走 L-7 降级路径）——本批只读断言面范围＝工具页两面板＋TCP 段
  控件（F-516③ 口径）。
