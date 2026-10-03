# UI-T45 实施留痕（View3D 阶段 B 宿主三维网关——两桥一源）

分支 `ui-t45`（基点 redesign-main@7503e250）；任务契约 `tasks/foundation/UI-T45.json`；方案 `docs/superpowers/specs/2026-10-03-view3d-stage-b-design.md`（含实施期范围勘误注）。

## 改动面

- **HostView3DGateway**（ui/plugin 新类，编入 ui_plugin 与 studio 两目标同源）：
  - 上行拾取：`handleViewDoubleClick`——pickFrame→帧名→SA-05 反解→两域分发＋View3DPick 选中；反解失败＝零伪造（L3 分支②）；
  - 下行呈现出口：`IUiPresentationOutlet` 宿主半区——HostPresentationObject 约定型（apply/remove，场景绑定下沉适配器）；事务语义＝新挂接失败保旧、成功后整组摘旧、幂等释放；
  - TCP 会话态源：`currentTcpPose`——resolveTcpFrame＋宿主 State→worldTframe（KIN-06 零修订）；
  - 可测形态：Deps 全函数缝（零框架视图依赖，headless 全链路可驱）。
- **UiPlugin 接线**：`assembleView3DGateway`（getView 不可得＝降级留痕不装配；名称映射与 SelectionService 共享同一实例——真映射注入点单一）；事件过滤合并进既有 eventFilter（视图本体＋全部后代挂接，isAncestorOf 判定＋坐标映射视图系，Ctrl+双击消费）；open/close 两拍（open 观测注释更新；close 场景清除拍清呈现残留）；成员 `m_nameMapPort`（HostEmptyNameMapPort 提升共享——WP-24-T08 替换即两消费面同步升级）。
- **九项清单校准**：View3DInteractionItem 增 `provision` 字段（八项"宿主原生提供"＋拾取"阶段 B 网关交付（Ctrl+双击）"）；占位面板行文案括注提供方式；清单头文案退役"将在后续版本提供"计划性口径；View3DContractTest 冻结断言同步。

## 范围勘误（实施期实证，spec/卡同步）

1. **下行高亮桥已在位**（HostHighlightOutlet 完整实现＋SelectionService 装配——spec 现状盘点误记为存根）：高亮零新增代码，acceptance 第 2 条收敛为既有实现回归验证（合并态套件全绿即证）；
2. **呈现出口场景绑定下沉适配器**（WorkCellScene 无头构造不可行）；"ui-t45.no-scene" 失败路径随移（场景缺席＝适配器不产载荷的装配态）。

## 测试（`tests/`，t＝集成树、s＝冒烟树）

| 套件 | 集成 | 冒烟 |
| --- | --- | --- |
| sdurws_ird_ui_test | 239/239（t1——View3DContract provision 断言在列） | 239/239（s1） |
| sdurws_ird_ui_gui_test | **76/76**（70＋6 网关新用例，t2） | 70/70（s2——网关门控不注册） |
| sdurws_ird_ui_contract_test | 32/32（t3——链接块位置纪律见下） | — |
| sdurws_ird_modeling_test | 297/297（t4） | — |
| sdurws_ird_modeling_gui_test | 12/12（t5） | — |

网关六用例：上行拾取命中链路（分发＋View3DPick 来源）、未命中/反解失败零副作用、呈现事务语义（换放/失败保旧/幂等释放/清除拍）、不完整视图失败、TCP 位姿逐分量＋降级路径、构造 fail-fast。

## 构建证据与门禁

- `build-integrated.log`：集成五目标零错误；冒烟复用 `build-smoke-ui-t44` 树重建零错误（网关门控不注册实证）。
- `validate-task` PASS（UI-T45 含勘误注）；`validate-docs.log` PASS。
- **链接块位置纪律**（实施期发现）：LinkageContractTest 的链接块提取取首个 `target_link_libraries(<目标> ` 命中——门控增列必须置于主链接声明之后（违者 testkit/testkit_qt 断言翻红，CMakeLists 注释登记）。

## 诚实边界

- 上行拾取的名称映射＝宿主现值 HostEmptyNameMapPort（诚实空映射）——生产链路当前态＝拾取反解诚实失败分支（零伪造选中）；真映射随 WP-24-T08 呈现装配替换成员实例即全链路激活（注入点单一）。
- TCP 源读宿主会话态（世界系位姿——设备 base 即模型根布置时与 base→TCP 一致）；需求域捕获流接线归 UI-T33。
- 呈现出口的 IUiPresentationSource 适配器（RT-T14 工厂→HostPresentationObject 包裹）未装配——出口单侧在位，全链随 WP-24-T08。
