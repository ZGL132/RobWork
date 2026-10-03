# View3D 阶段 B 宿主三维网关方案（UI-T45 ＋ UI-T33 承接）

> 状态：已评审待实施（2026-10-03 起草；立项配套＝`tasks/foundation/UI-T45.json`〔网关〕＋`UI-T33.json` 转可执行〔需求域消费侧〕）。
> 红线前置：框架零源码修改（SA-02——本方案全部经框架公开 API＋Qt 事件过滤器外挂实现，零 patch）；不接管宿主三维场景生命周期（O-38 裁决③/O-43）；KIN-06 会话姿态零修订。

---

## 0. 结论（三桥一源）

阶段 B 的全部增量收敛为一个宿主侧网关类＋三个既有端口的实现：

| 链路 | 方向 | 既有端口/缝（已冻结在位） | 阶段 B 新增 |
| --- | --- | --- | --- |
| 拾取 | 上行 | 两域 `reportView3DPick` 出线＋`SelectionService` L3 分支＋`IUiRuntimeNameMapPort`（SA-05 双射） | viewport 事件过滤＋`pickFrame` 反解分发 |
| 高亮 | 下行 | `IUiHighlightOutlet`（SelectionService L2 出线，可空声明） | 出口实现（WorkCellScene drawable 高亮） |
| 呈现 | 下行 | `IUiPresentationSource/Outlet`＋`RuntimePublishBridge` 四步刷新事务（UI-T20）；供给侧 RT-T14 工厂已完成 | 出口实现（呈现对象挂接 WorkCellScene） |
| TCP 位姿 | 源 | 需求域捕获流（`flowCaptureTcp` 降级壳）＋`RobWorkStudio::getState()` | 网关状态源出口（会话态只读，KIN-06） |

即：**架构面零新概念**——阶段 B 只是把 UI-T20/UI-T21 已冻结的端口从"可空诚实缺席"接到"宿主真身"。这与 B1-SPEC D10（宿主运行时发布桥）的既定分工一致。

## 1. 现状盘点（依赖全部在位，逐项实证）

1. **框架能力**（`rws/RWStudioView3D.hpp`，公开 API）：`pickFrame(x,y)`（射线选 Frame）、`pick(x,y)`（选 DrawableNode）、`getWorkCellScene()`（场景图——高亮/挂接呈现对象的操作面）、`getState()/setState()`（当前 State）、`getSceneViewer()/update()`；宿主入口＝`RobWorkStudio::getView()`（UiPlugin `open(workcell)` 回调已注入观测，现状"零视图操作"——阶段 B 起升级为受控消费）。
2. **上行缝**：`ModelingPluginAssembly::reportView3DPick`/`RequirementsPluginAssembly::reportView3DPick` 均已出线（域内适配器齐——"拾取未命中本域＝诚实 false"）；`SelectionService::handleTreeViewFrameSelected` L3 四具名分支（反解成功/失败/零伪造/树未命中）已冻结。
3. **下行缝**：`SelectionService` 构造 Deps 携带 `IUiHighlightOutlet`（可空＝无三维场景的显式声明）；宿主装配处现注入的是树定位回调＋空高亮出口（`m_highlightOutlet` 存根）。
4. **呈现缝**：`RuntimePublishBridge`（UI-T20）四步刷新事务＋三类身份投影已落位；`IUiPresentationOutlet` 宿主半区未装配（grep 零命中）——供给侧 RT-T14（宿主呈现 WorkCell 契约）已完成。
5. **需求域消费缝**：`RequirementsPanelWidget::setRegionPreviewSink(RegionPreviewSink)` 注入口在位（"空＝文本摘要"）；TCP 捕获流降级文案点（RequirementsCommandFlows"需要三维视图关节状态数据源"）即本方案 §2.5 的解除点。
6. **契约面**：`View3DContract.hpp` 九项交互清单＋KIN-06 会话姿态四常量（sessionStateOnly/writesDesignModel=false/producesRevision=false/invalidatesResults=false）——阶段 B 验收边界即契约边界。

## 2. 架构设计

### 2.1 HostView3DGateway（唯一框架三维消费面）

- 落位：`ui/plugin/HostView3DGateway.{hpp,cpp}`（编入 `sdurws_ird_ui_plugin`——与 UiPlugin 同目标，零新目标）。
- 装配：UiPlugin `initialize` 末段（域装配与共享面就位后）经 `getRobWorkStudio()->getView()` 取 `RWStudioView3D`；未取到＝诚实降级（三桥不接、 outlets 保持可空声明——与现状等价，不虚构三维能力）。
- 场景就绪：`open(workcell)` 回调从"只读观测"升级为"场景可用通知"（通知网关缓存 WorkCell/Scene 指针供反解与挂接）——**不接管场景生命周期**（O-38③：场景归宿主；网关只在打开/关闭两拍同步引用）。
- 事件注入：`QObject::eventFilter` 外挂到 `RWStudioView3D` 的 viewport（SA-02 零框架修改——不改 `mouseDoubleClickEvent`，过滤器监听 `MouseButtonDblClick`＋Ctrl 修饰键）。
- 清理：`teardownSharedSurfacesForClose` 编排增一步（网关：摘除过滤器＋清场景引用＋清高亮/呈现残留——"项目关闭清理高亮"纪律）。

### 2.2 上行拾取链（旧版 Ctrl+双击语义等价承接）

```
Ctrl+双击 → eventFilter → gateway
  → RWStudioView3D::pickFrame(x,y) → Frame* → frame->getName()
  → IUiRuntimeNameMapPort 正向解析 → ObjectId（失败＝分支②诚实跳过，不伪造）
  → ① SelectionService（来源词表 View3DPick——L3 既有分支消费）
  → ② modeling/requirements reportView3DPick 分发（未命中本域＝域内诚实 false）
```

处置约束：ObjectId 解析失败（运行时未发布/非业务对象）＝状态行诚实提示（可选）＋零选中变化——延续 L3 分支②语义；拾取不产生修订（选中是会话态，KIN-06/AT-04）。

### 2.3 下行高亮链

```
SelectionService 选中广播 → IUiHighlightOutlet（网关实现）
  → ObjectId 反解帧名 → WorkCellScene::getDrawable → DrawableNode 高亮位置位
  → 清除对称：选中清空/失效/项目关闭三拍全清（"不残留旧高亮"纪律）
```

处置约束：反解失败＝无操作（不报错不残留）；高亮属会话显示面（零修订）。

### 2.4 呈现应用链（工位标记/区域边界框/采样格着色的宿主半区）

```
RuntimePublishBridge 刷新事务（既有四步：事件过滤→完整构造→身份对账→原子替换）
  → IUiPresentationOutlet（网关实现）：
     apply(呈现对象集)＝挂接 WorkCellScene 渲染分组（rt-presentation 组）
     detachHostSession()＝整组摘除（会话拆除释放——零残留）
```

处置约束：呈现对象由 RT-T14 工厂产出（呈现视图与计算快照隔离——hostPayload 对 ui 不透明，ui 不解引用）；刷新失败＝保留旧呈现＋UI-PRESENTATION-REFRESH-FAILED 既有双通道；着色判定（Good/Weak/Failed）归域侧采样纯函数，网关零判定。

### 2.5 TCP 数据源出口（需求域捕获流解除降级）

```
需求域"捕获 TCP"命令 → 网关 currentTcpPose(deviceName)
  → RobWorkStudio::getState()（宿主当前 State——只读）
  → 设备 base→TCP FK（rw::kinematics::Kinematics::baseToTcp，只读计算）
  → 位姿＋参考系名回填需求草稿（确认凭据语义不变）
```

处置约束：无设备/无 WorkCell/状态不可得＝诚实失败原因（ERR-01——不虚构位姿）；读的是**宿主会话态**（KIN-06：零修订、不写设计模型——View3DSessionPoseContract 四常量即验收口径）；需求语义零变化（捕获语义＝UI-T31 既有的字段写回流，数据源换真）。

### 2.6 九项交互清单的阶段 B 处置（对账 View3DContract）

| 清单项 | 阶段 B 处置 |
| --- | --- |
| view3d.host / standard_camera_views / zoom / projection_toggle / wireframe_transparency / render_group_visibility / collision_highlight_mask / screenshot_png | **宿主框架原生已提供**（RWStudioView3D 自带交互/菜单/快捷键）——占位面板清单文案校准为"宿主原生提供"（登记校准，非新实现） |
| view3d.pick_ray_cast | **阶段 B 新增**（§2.2 网关拾取链）＋高亮/呈现两出线（UX-11 扩展登记） |

## 3. 任务分解与验收边界

| 卡 | 范围 | 验收锚 |
| --- | --- | --- |
| **UI-T45**（新建，ui 单元，分支 `ui-t45`） | HostView3DGateway 三桥一源（§2.1~§2.5）＋装配/清理接线＋九项清单文案校准 | 上行（gui 替身视图发拾取事件→选中/分发断言）、下行（outlet 高亮位断言＋三拍清理）、呈现出口（替身对象挂/摘断言）、TCP 出口（替身 State→位姿断言）；双模式构建零错误＋ird_gates＋validate-docs |
| **UI-T33**（转 ready，dependsOn 增 UI-T45） | 需求域消费侧：RegionPreviewSink 注入（工位标记/区域边界框/采样格着色预览）、Ctrl+双击拾取流程壳解除引导、TCP 捕获流接真数据源 | 卡内既有四条 acceptance（工位标记/边界框/采样格着色/拾取收口）不变＋TCP 捕获真数据源一条 |

堆叠序：`ui-t45`（基点合流后主线）→ `ui-t33`（基点 ui-t45 head——串行堆叠，与 UI-T36→T37 先例同款）。

## 4. 风险与红线自查

1. **SA-02 零框架修改**：eventFilter 外挂＋公开 API 消费；若实施中发现必须改框架（如双击事件被框架吞），按红线先登记 patch 再动——方案不预授权。
2. **O-38③/O-43**：网关不建顶层窗口、不接管场景；引用随打开/关闭两拍同步。
3. **残留三拍清**：选择失效/会话切换/项目关闭（§2.3/§2.4/§2.1）。
4. **KIN-06**：TCP 位姿＝会话态只读，零修订；View3DSessionPoseContract 四常量为 gui 断言锚。
5. **只读会话**：浏览/拾取/高亮/呈现可用（显示面）；捕获/拾取**写回**仍走域命令既有只读门控（L-7 命令半区——UI-T43 已收口）。
6. **诚实降级**： getView() 不可得＝三桥不接（与现状等价）；运行时名称映射未发布＝拾取反解失败分支。
