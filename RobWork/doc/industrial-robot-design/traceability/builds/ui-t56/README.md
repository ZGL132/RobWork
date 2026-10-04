# UI-T56 构建与测试留痕（WC/DWC XML 外供导出命令批次）

分支 `ui-t56`（基点 redesign-main@7743af91）；任务契约 tasks/foundation/UI-T56.json；
DTB §2.11 WP-10-T56 行；units/modeling.md v0.34 §9.7.3 第十一号行；units/ui.md v1.67 §13 UI-T56 行。

## 交付物

- 命令目录：第十一号 `modeling.export-workcell-xml`（Project/只读允许——
  MDL-20 ③会话级文件操作零修订；F-502 分层自动入折叠区）＋
  isAssembledModelingCommand 词表同源演化。
- 流程与绑定：ModelingFlowDeps 增 exportWorkCellXml 回调＋执行分支
  （saveFilePath→覆盖确认→回调透传）；ModelingUiModule bindWorkCellExport
  ＋deps 注入（模块零 runtime 类型依赖）；UiPlugin 接源
  （lastPublishedSnapshot 现取——零第二编译路径；快照缺席诚实拒绝；
  双面输出 wc＋dwc）。
- UiText：title/tooltip 两键（kDomainCommandTitleTable 容量 40→42）。
- 命令钮 objectName 挂 id 后缀（ird_modeling_cmd_<id>——F-502 折叠重排后
  子树序≠目录序的测试对账锚）＋两 gui TU 对账改目录序拾取。
- 测试：ModelingCommandFlowsTest 三用例（全链／缺席与未接线／取消与覆盖
  确认）＋FlowHarness export 替身＋计数与只读集合更新（10→11）。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 集成构建（受影响九目标） | 零错误 | 会话执行记录 |
| 独立冒烟（配置＋构建＋ctest 3/3） | 全绿 | 会话执行记录 |
| sdurws_ird_modeling_test | 327/327（新增 3） | modeling-test.log／.xml |
| sdurws_ird_modeling_gui_test | 28/28 | modeling-gui.log／.xml |
| modeling_contract／ui_test／ui_contract／ui_gui／requirements_gui | 17／246／32／78／44 全绿 | 会话执行记录 |
| ird_gates | 零新增依赖/目标——基线 101 条存量集口径，比对见验收记录 | 会话执行记录 |
| validate-docs／validate-task（UI-T56.json） | PASS／PASS | 会话执行记录 |

## 诚实边界

- F-498 预览页多类型下拉半区不在本批——需域侧内存导出面（toString 变体）
  与 SerialDevice/Scene/Collision XML 域导出函数，维持 open 登记导出半区兑现。
- UI-T55 卡注"TCP 列表＝UI-T56"取号顺延 UI-T57（本号被本批占用——备案）。
- gui 套件口径＝原生窗口平台（gate-all 同口径）。
