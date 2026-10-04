# UI-T55 构建与测试留痕（工具安装接口/场景世界位姿编辑页批次）

分支 `ui-t55`（基点 redesign-main@dc0f5dca）；任务契约 tasks/foundation/UI-T55.json；
DTB §2.11 WP-10-T55 行；units/ui.md v1.65 §13 UI-T55 行。

## 交付物

- 域原语（Parts.hpp/.cpp）：PartPoseEditValue 六标量（工具面法兰系
  T_flange_tool／场景面世界系固连 M-11）＋PartPoseEditErrorCode/token＋
  applyToolMountEdit/applyScenePoseEdit（越界 fail-fast→共用有限性判定→
  Transform3D 直写＋恰一条变更记录）；RPY 正解 src/RpyMath.hpp 第三消费面。
- 面板承载："编辑"页签堆栈扩至四页（关节 0/基座 1/工具 2/场景 3）＋
  refreshEditPages 选择驱动分派（Joint/Tool/Scene 树锚自动切页）＋
  buildPoseEditPage 通用位姿页构建＋两出口壳＋分组装配＋L-7 页级禁用。
- ★ 重建策略修正：工具/场景页字段集常量，构造期一次建成**不随换目标
  重建**——ParamTablePanel deleteLater 重建引入旧表悬空引用窗口（实测
  AV）；基线推送 setBaseline 完成对象切换。关节页因字段集随关节类型
  可变仍需重建——两策略并存双登记。
- 测试：PartsTest PartPoseEdits_ToolMountAndSceneWorld_UI_T55＋gui 三
  用例（两选择驱动全链＋L-7 禁用）＋对账排除扩展（两 TU 同步）。

## 验证记录

| 项 | 结果 | 留痕 |
| --- | --- | --- |
| 集成构建（受影响目标全量） | 零错误 | 会话执行记录 |
| 独立冒烟（配置＋构建＋ctest 3/3） | 全绿 | 会话执行记录 |
| sdurws_ird_modeling_test | 324/324（新增 1） | modeling-test.log／.xml |
| sdurws_ird_modeling_gui_test | 28/28（新增 3） | modeling-gui.log／.xml |
| sdurws_ird_modeling_contract_test | 17/17 | 会话执行记录 |
| sdurws_ird_ui_test／ui_contract／ui_gui | 246/246／32/32／78/78（原生窗口平台） | 会话执行记录 |
| sdurws_ird_requirements_gui_test | 44/44 | 会话执行记录 |
| ird_gates | 零新增依赖/目标（域增量仅 Parts 部件呈现面＋插件页）——基线 101 条存量集口径，比对见验收记录 | 会话执行记录 |
| validate-docs／validate-task（UI-T55.json） | PASS／PASS | 会话执行记录 |

## 诚实边界

- TCP 列表结构化编辑（MDL-13 不变量流）登记 UI-T56。
- 建模只存参数不存矩阵——位姿直写零预乘（M-11）。
- gui 套件口径＝原生窗口平台（gate-all 同口径；offscreen 环境性误报见
  F-507 同族教训）。
