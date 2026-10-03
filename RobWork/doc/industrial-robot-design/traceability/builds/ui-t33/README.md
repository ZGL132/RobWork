# UI-T33 实施留痕（需求域三维消费侧——TCP 捕获解除降级；部分交付登记）

分支 `ui-t33`（基点 ui-t45 head）；任务契约 `tasks/foundation/UI-T33.json`（保持 open——四条 acceptance 中三维呈现/拾取收口两项受 WP-24-T08 前置未合入，本批交付 TCP 捕获真数据源与拾取命令骨架，见诚实边界）。

## 改动面

- **TCP 捕获解除降级**（本批主交付）：
  - 门面新增 `RequirementsView3DSeams`（listDevices/tcpWorldPose/resolveFrameObjectId/lastPickedObjectId/sessionRevisionId 五缝）＋`bindView3DSeams` 转发；
  - `flowCaptureTcp` 真实现：设备清点（零台诚实失败/多台宿主下拉）→网关 TCP 世界系位姿→REQ-08 确认门（confirmImport 通用摘要确认缝）→域服务 `captureTcpAsFixedPoint`（REQ-08/STALE/I-REQ-3/5 全在域侧）→确定性命名 TcpCapture＋去重后缀；
  - `flowPickFeature` 真实现：lastPicked＋选中工位→域服务 `applyPickToOrientation`；未拾取/未选中＝交互指引（取代"版本未提供"）；
  - `executeRequirementCommand` 带缝重载（无缝指针＝降级原文，back-compat）；面板/模块透传链（面板 executeDomainCommand 重载＋qtDialogHost 绑定）；
  - `RequirementsUiModule::bindView3DSeams`（lastPicked 自引用重写）＋`reportView3DPick` 成功登记 `m_lastView3DPick`；
  - 面板 `selectedPointId()` 访问器。
- **宿主绑定**：UiPlugin `assembleView3DGateway` 内 bind 需求域三缝（listDevices 宿主 WorkCell 现查/tcpWorldPose 经网关/resolveFrameObjectId 经共享名称映射实例；sessionRevisionId＝空＝无基线诚实缺省）。

## 测试（`tests/`，t＝集成树、s＝冒烟树）

| 套件 | 集成 | 冒烟 |
| --- | --- | --- |
| sdurws_ird_requirements_test | 200/200（t1） | —（同源） |
| sdurws_ird_requirements_gui_test | **39/39**（含 1 新用例，t2） | — |
| sdurws_ird_ui_test | 239/239（t3） | — |
| sdurws_ird_ui_gui_test | 76/76（t4——网关回归面） | 70/70（s1——门控不注册） |
| sdurws_ird_modeling_test | 297/297（t5） | — |
| sdurws_ird_modeling_gui_test | 12/12（t6） | — |

新用例：`CaptureTcp_SeamsPresent_CreatesFixedPoint_UI_T33`（缝在位＋确认接受→新建固定点·位置替身位姿直投·REQ-08 确认门呈现面）。

## 诚实边界（部分交付登记）

1. **三维呈现/拾取收口两项 acceptance 受 WP-24-T08 前置**：宿主名称映射现值＝HostEmptyNameMapPort 诚实空映射——网关拾取反解走失败分支（零伪造选中），flowPickFeature 当前生产态＝"未拾取"指引；工位标记/边界框/采样格三维呈现的宿主半区（IUiPresentationOutlet 挂接）已在 UI-T45 在位，但 RT-T14→呈现适配器随 WP-24-T08——三项留待前置合入后收口；
2. 捕获写回＝位置分量（世界系）＋新建固定点；姿态不自动派生（域侧 OrientationRule 规则语义——"姿态规则未变更"随成功摘要明示）；
3. 确认凭据主体＝"ui"（CommandDialogHost 无主体语义——REQ-08 主体登记随域确认凭据任务细化）；
4. requirements_test 目标（QCoreApplication）不能构造 widget 面板——流测试落位 gui_test 目标（QApplication main；源清单同步）。
