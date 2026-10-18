# Isaac Sim 建模交互界面 × RobWork 建模模块 UI 对照分析（专题 2）

> 性质：外部参考研究笔记（非设计文档链组成部分，不构成需求/架构修订依据）。
> 来源：Isaac Sim 6.2.0 本机安装（`D:\software\isaacsim`，UI 扩展源码级核对）＋ NVIDIA 官方 UI 文档；
> RobWork 侧为 `industrialrobot/` ui/modeling 单元源码与设计文档链（units/ui.md、units/modeling.md、ARCHITECTURE §7.12/SA-18）。
> 分析日期：2026-10-15。
> 问题：RobWork 建模模块的界面交互能否参考 Isaac Sim 的建模交互（如 Stage 层级树、属性 tab 编辑器）？

---

## 0. 一句话结论

**形态上两者已经同构**——RobWork 已落地的"项目树＋属性检查器＋参数表＋三维联动"四件套与 Isaac Sim 的"Stage 树＋Property 面板"是同一交互范式（且 RobWork 的契约更严格）。**真正可参考的是少量具体交互细节**（本文 §4 清单，共 3 项有实际价值）；而 Isaac Sim 建模交互的两大底层哲学——**"就地编辑直写数据"** 与 **"暴露 USD 组合细节"**——与本项目"草稿/权威分离（PM-04/PA-2）"和"业务对象树"的红线**根本冲突，明确不照搬**（§5）。

---

## 1. Isaac Sim 建模交互逻辑梳理

本机 6.2.0 安装的 UI 由扩展族装配（`extscache/omni.kit.window.stage-2.6.4`、`omni.kit.property.*` 适配器族、`omni.kit.manipulator.*` 操纵器族、`isaacsim.gui.property/menu/content_browser`、`isaacsim.robot.poser`），各窗口即扩展、可停靠/撕出/多显示器。建模相关交互分四个面：

### 1.1 Stage 层级树（`omni.kit.window.stage`）
- **呈现内容**：USD 场景的 prim 树——每行含类型徽标（Xform/Mesh/Joint…）、名称、类型列；
- **行内状态控制**：可见性"眼睛"开关（写 USD 属性）、payload 加载/卸载（大场景按需折叠/展开细节）；
- **结构操作**：右键菜单增删 prim、**拖拽重组层级**、停用（deactivate）节点；
- **导航联动**：选中→Property 面板跟随；双击→视口聚焦；外部拖放入树（`external_drag_drop_helper.py`）；
- **选中监视**：`selection_watch.py` 双向同步（树↔视口）。

### 1.2 Property 属性面板（`omni.kit.property.*` 适配器族）
- **多 tab 组织**：按 prim 类别分组呈现属性页（关节/材质/物理各一页），另有 **Raw USD Properties 页**（直接看底层属性）与 **API Schemas 页**（看挂了哪些 API）；
- **属性搜索过滤框**：属性多（一个 prim 动辄数百属性）时快速定位；
- **就地编辑**：字段直接改值、带单位、改完立即写回 USD——**没有"草稿/应用"两级**；
- **适配器架构**：属性页内容由适配器（usd/fabric/自定义）供给，面板框架本身不感知属性语义——这一点与 RobWork 的 `IUiPropertyPagesProvider`（域判定在域）是同一模式。

### 1.3 Viewport 与操纵器（`omni.kit.manipulator.*`）
- 移动/旋转/缩放 gizmo（局部/全局坐标系切换）、捕捉（snap）、点选/框选；
- 工具栏 **Play/Stop 即仿真启停**，建模与仿真验证同屏一体；
- **Robot Poser**（`isaacsim.robot.poser`）：拖滑块摆关节姿态、逐关节微调——机器人专用的交互面板。

### 1.4 内容浏览器与"操作即数据"
- 资产浏览器拖拽引用入场景；变体（variant）选择换配置；
- **交互哲学："操作即数据"**——树上的眼睛、Property 里的字段编辑、拖拽重组，全部直接写 USD 数据；数据可 diff、可回放、可另存。没有"编辑会话/权威版本"的分离概念（USD 层本身有版本化，但 UI 层是直写的）。

---

## 2. RobWork 建模 UI 现状快照（对照基线）

已实现（源码级核实，`industrialrobot/` 下）：

| 交互件 | RobWork 实现 | 契约要点 |
| --- | --- | --- |
| 项目树 | `IndustrialProjectTree`（QTreeWidget；`ProjectTreeModel`） | 五分组封闭词表；节点身份＝ObjectId、显示名经 `IUiNameResolver` 现取（零名称缓存，R-4）；Provider 驱动（域供节点）；双树边界 INV-B1/B3（业务树 vs 框架 TreeView 互不注入） |
| 建模结构树 | `ModelingPanelWidget` 区① `m_tree`（QTreeWidget，隐藏列＝ObjectId 锚点） | 关节链/基座/工具/场景/位姿集/传动分组（modeling.md §9.7.1）；诊断条点击→`focusObject` 定位跳转 |
| 属性检查器 | `PropertyInspector`（六态：NoSelection/Multi/RuntimeOnly/NoProviderAnswer/ObjectFields/PageRejected） | D5 常用字段页 ≤16 字段哨兵（超量整页拒绝）；D6 复杂编辑页经 `IUiPropertyPagesProvider` 域供给；只读徽标是"供给的事实" |
| 参数表 | `ParamTablePanel`＋`FormEditCommon` | 批量粘贴/筛选/单位显示/错误定位（UX-05）；表单级确认应用（UX-07）；三层值语义 `ParamEditModel` |
| 三维联动 | `HostView3DGateway` 三桥（拾取↑/高亮↓/呈现出口）＋`SelectionService` | 选中唯一汇聚点；来源四值封闭词表（项目树/三维拾取/搜索/命令）；`IUiRuntimeNameMapPort` 正反名称映射＋"已应用"判定（L2） |
| 编辑流 | `DraftController` 草稿链 | **编辑只进草稿**→`draft.apply`→命令端口→确认对话→修订事件→树/检查器刷新；两级撤销边界（§8.7） |
| 命令入口 | `CommandRegistry`＋命令面板＋全局快捷键唯一注册点（SA-16） | 工具栏按钮全部路由命令注册表，不直调域服务 |

结论：**Isaac Sim 的"Stage 树＋Property 面板"范式，RobWork 已以更严格的契约形态落地**（六态显式状态机、封闭词表、ObjectId 身份、草稿分离），这不是巧合而是双方都收敛到"场景图编辑器"的成熟形态。

---

## 3. 对照分析

### 3.1 形态同构项（互相印证，无需动作）

| 交互模式 | Isaac Sim | RobWork | 备注 |
| --- | --- | --- | --- |
| 层级树导航 | Stage 树 | 工业项目树＋建模结构树 | RobWork 双树分工更细（业务导航 vs 运行时结构） |
| 选中→属性联动 | 树/视口选中→Property 跟随 | SelectionService 汇聚→PropertyInspector | RobWork 有显式六态与来源标记，契约更强 |
| 属性页域供给 | property 适配器族 | `IUiPropertyPagesProvider`（域判定在域） | 同一模式 |
| 参数就地编辑 | Property 字段直改 | ParamTablePanel 值列就地编辑 | RobWork 编辑进草稿（见 §5） |
| 点选定位 | 双击聚焦视口 | 诊断条点击→`focusObject` 树定位 | 方向相反，各有场景 |
| Play 验证循环 | Play/Stop 仿真 | 预览页（仅已应用修订）＋运动学求解通道 | RobWork 的"预览=已应用修订"边界更严格 |

### 3.2 RobWork 反向更强项（Isaac 无对应，印证本项目设计价值）

- **就绪诊断条逐项点击定位**（L0~L11 分层＋跳转树节点）——Isaac 无"就绪/诊断导航"概念；
- **命令面板＋快捷键唯一注册点**（重复绑定注册期拒绝）——比 Isaac 的菜单体系更严格；
- **属性检查器显式六态**——Isaac 的空态/无属性态无显式状态语义；
- **诊断码/证据链语义**贯穿 UI（只读徽标、恢复横幅、诊断表）。

---

## 4. 可借鉴项点清单（按 价值×成本 排序）

以下每项给：交互描述 / 价值 / 落点 / 红线合规判定 / 涉及需求。**仅为建议，是否立卡由所有者裁决**（本文档不构成任务卡）。

### 借鉴 1：属性检查器/复杂编辑页的"字段过滤搜索"（低成本，条件性高价值）
- **Isaac 交互**：Property 面板顶部搜索框，实时过滤属性行。
- **价值判定**：Isaac 的 prim 动辄数百属性所以必需；RobWork 的 **D5 常用字段页有 ≤16 字段哨兵——搜索对 D5 无必要**（哨兵本身就保证了字段少）。真正的缺口在 **D6 复杂编辑页与参数表**：DH 参数 12 行、物性表、位姿集可能数十行，且 UX-05 已明文要求参数表"筛选"。
- **落点**：先核实 `ParamTablePanel` 的筛选实现覆盖度（UX-05 已要求，大概率已实现）；若 D6 域页（如 `dh-parameters`）复用 `FormEditCommon` 则同享筛选。**判定：大概率已覆盖，核实即可，不必新立交互**。
- **红线**：纯呈现层，无冲突。涉及 UX-05。

### 借鉴 2：树节点"可见性开关"（中成本，需先补语义定义）
- **Isaac 交互**：Stage 树行内眼睛图标，切换 prim 可见性（写 USD 属性，属数据）。
- **价值**：建模对象多时（多条位姿集、多工具安装），快速隐藏无关几何，降低视觉噪声；KIN-09 工作采样点云可视化也有类似需求面。
- **语义判定（关键）**：本项目的可见性归属必须先定义——**呈现态**（视图会话局部状态，不进修订链）还是**模型态**（属对象属性，进草稿/修订）。Isaac 是后者（直写数据）；本项目的修订链服务于设计证据（PA-2），把"临时隐藏"也变成修订是过度追溯。**建议语义：呈现态（会话局部，UI 关闭即弃）**，与 SelectionService 的会话态同层。
- **落点**：`ModelingPanelWidget` 结构树或 `HostView3DGateway` 下行高亮通道的扩展（可见性属呈现下行）。
- **红线**：若按"呈现态"落地则不触草稿/命令边界，合规；若定义为模型态则须走 draft.apply，成本陡增。涉及 UX-11（三维交互）、KIN-09。
- **前置**：units/ui.md 或 units/modeling.md 需增量修订一条"可见性语义"约定（属 B1-SPEC 级别的小增量）。

### 借鉴 3：双击树节点→视口聚焦/fit（低成本，补强现有联动）
- **Isaac 交互**：双击 Stage 树节点→视口相机聚焦该 prim。
- **RobWork 现状**：选中联动 L1~L3 已承诺（高亮下行已有），`focusObject` 已存在但方向是"诊断→树"；"选中→视口相机 fit 到对象"未见明确落点（待核实宿主 `RWStudioView3D` 是否已有 fit-to-selection）。
- **落点**：`HostView3DGateway` 呈现出口或宿主视图工具（若宿主已带 fit 功能则只需注册快捷键/双击手势到命令注册表）。
- **红线**：纯呈现；注意 L4"不承诺"边界——不为此扩宿主 API 面。涉及 UX-11、SA-18 L1~L4。

### 不建议引入的对照项（已评估，列出避免重复讨论）

| Isaac 交互 | 不引入原因 |
| --- | --- |
| 属性多 tab＋Raw USD Properties 页 | D5/D6 分野（按编辑深度）已覆盖组织需求；"Raw 属性视图"无对应也无必要——本项目树/检查器面向工程师而非 USD 专家 |
| 拖拽重组树节点 | 关节链结构编辑已定义为显式四操作（增删/重排/六轴重置，UI-T47）经命令端口＋确认；拖拽易误操作且重排语义重（产生修订），按钮式显式操作＋确认对话更稳 |
| 面板撕出/多显示器 | 宿主 Dock 拓扑已由 UI-T18 冻结（O-43 登记坑），重新开洞成本高、价值低 |
| payload 加载/卸载行内控制 | 本项目无 USD payload 组合形态；"大装配按需加载"在本项目对应"预览仅基于已应用修订"（D-MDL-10），已有边界 |

---

## 5. 不可照搬项（根本差异，写明原因）

1. **"就地编辑直写数据" ↔ 草稿/权威分离**。Isaac 改字段即写 USD，无确认无草稿；本项目 PM-04（保存/应用分离）、PA-2（修订不可变）、ARC-04（可追溯）要求一切模型编辑经 `draft.apply`→命令端口→确认→修订事件。**这是需求根基的差异，不是实现优劣**——Isaac 面向"快速搭仿真场景"，本项目面向"可追溯的设计证据链"。可吸收的是其"编辑循环短"的体验目标（RobWork 以预览页＋60s 草稿落盘逼近），而非直写机制。
2. **Stage 树暴露 USD 组合细节 ↔ 业务对象树**。Isaac 的树呈现 payload/variant/deactivate 等 USD 机制细节，用户须懂 USD；本项目树是五分组封闭词表＋ObjectId，命名权威在 runtime（SA-05），树零名称缓存。**面向用户群不同**：USD 专家 vs 机械工程师。
3. **"操作即数据"哲学**。可部分吸收（操作可追溯、可 diff、可回放的思想与修订链同源），但载体不同——本项目用修订链＋证据链承载，不用 USD diff。

---

## 6. 结论：能否参考、怎么参考

- **能，且已经参考了**（或殊途同归）：四件套形态双方一致，RobWork 的契约化程度更高，无需"向 Isaac 看齐"的大动作。
- **建议参考的实质项**只有 §4 三条：①字段筛选（核实即可，大概率已覆盖）；②树节点可见性开关（**唯一有实质缺口的项**，需先在单元卡补一条"可见性＝呈现态"语义定义再实施）；③双击→视口聚焦（小补强，待核实宿主能力）。
- **明确不参考**：直写式编辑、USD 细节暴露、拖拽重组、Dock 重排（§5＋§4 下表）。
- **反向印证**：就绪诊断定位、六态检查器、命令注册表纪律是本项目比 Isaac Sim 更强的地方，应保持并继续深化。

---

## 附录：证据锚点

- Isaac Sim 本机：`extscache/omni.kit.window.stage-2.6.4`（含 `external_drag_drop_helper.py`、`selection_watch.py`、visibility/payload 逻辑）、`omni.kit.property.usd-4.9.18`、`omni.kit.manipulator.*` 族、`isaacsim.gui.property/menu/content_browser`、`isaacsim.robot.poser`；官方 UI Reference（docs.isaacsim.omniverse.nvidia.com/6.0.1/gui/reference_user_interface.html）。
- RobWork：`industrialrobot/ui/include/sdurws/ird/ui/IndustrialProjectTree.hpp`（INV-B1/B3、Provider 驱动、名称现取）、`PropertyInspector.hpp`（六态冻结文案、只读徽标纪律）、`SelectionService.hpp`（四值来源词表、名称映射端口）、`modeling/plugin/ModelingPanelWidget.hpp`（结构树锚点列、focusObject、诊断条）、units/ui.md §4/§7/§13（UI-T21/T22/T08）、units/modeling.md §9.7（三接入面、十条数据流）、ARCHITECTURE §7.12/SA-18。
