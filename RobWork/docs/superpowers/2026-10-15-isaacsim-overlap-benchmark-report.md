# Isaac Sim 与工业机械臂研发工作台：功能重叠模块及借鉴清单

研究日期：2026-10-15。研究对象：本机 Isaac Sim 与 RobWork 仓库中的 industrialrobot 产品。本文是研究和打磨建议，不是功能验收记录；没有修改产品代码、Isaac Sim 安装目录或机器人资产。

## 1. 结论与证据边界

**最值得借鉴的是“让模型、操作、计算和问题定位保持关联”的工作方式。** 三维视图承担输入辅助和结果解释，属性表承担精确编辑，诊断列表承担问题定位，曲线承担行为验证，分步骤的工作流承担复杂任务组织。这些模式适合本项目“已有六轴臂改型与选型”的第一条研发主线。

Isaac Sim 是机器人仿真与开发平台，强项包括场景和机器人资产组织、物理仿真、运动生成、控制验证、传感器和开发接口。本项目的目标是机械臂设计研发：模型改型、任务要求验证、动力负荷计算、电机/减速器选型、结构与传动联合优化、工程证据与报告。两者重叠明显，但不能把仿真平台的功能直接等同于完整机械设计、器件选型或研发验收。

### 1.1 本次依据

| 标记 | 含义 | 本次实际情况 |
| --- | --- | --- |
| L | 本机安装文件、源码、示例 | 已核对 VERSION、导入器 UI、资产验证 UI、XRDF 编辑 UI、Gain Tuner UI、系统辨识 UI、运动生成接口与示例 |
| D | NVIDIA 官方文档 | 已读取本文参考资料中的界面、机器人检查器、导入、物理、运动生成、系统辨识等页面；`latest` 内容可能随发布改变 |
| A | 既有运行产物 | 已读取用户目录中的 UR10 脚本和运行摘要；摘要记载六自由度、450 步、7.5 s 仿真时间及 PASS，本次没有复跑 |
| U | 尚未现场验证 | 交互操作、面板实际布局、各插件当前启用状态、规划/动力学精度、硬件性能均未实测 |

本机版本文件为 **`6.2.0-rc.14+release.52805.02189a84.gl`**，属于候选构建，不能写成“已验证的最新稳定版本”。安装根目录为 `D:/software/isaacsim/`。本次窗口发现没有取得可操作的 Isaac Sim 主窗口；曾观察到 Kit 进程，后续检查未再返回该进程。**以下界面与操作描述来自 L/D，不宣称已逐项点击体验。**

项目参考基线：`redesign-main`，HEAD `b90a442d7cf5d05f9cff3b2bf22b58240f0789b6`。对照使用现有需求和单元设计；没有逐项复现本项目的当前 UI，因此“建议核验/完善”不代表“目前一定缺失”。已有其他研究文档保持原样，本报告独立核对关键结论。

### 1.2 既有 UR10 示例的使用边界

`run_ur10_sample.py` 在运行舞台中删除了 `root_joint`、改变 articulation root，并写入底座相关属性。它适合证明这个经过处理的场景曾经加载、驱动并生成图像；**不能作为未经改变的官方资产动力学基准**。

脚本中“固定世界关节 body0 为空，所以是悬空关节”的解释不成立：官方物理文档和 Robot Inspector 的 Anchor 说明均允许固定关节的一端为空以表示世界连接。原始资产是否有别的结构、加载或版本问题，需要单独检查，不能由该注释推定。不能将“机器人动起来了”作为运动学、底座约束或力矩计算已正确的证据。

## 2. 共通界面和操作特点

### 2.1 工作区组织

官方界面由菜单、主工具栏、中央 Viewport、Stage 层级、Property 属性面板、资产/示例浏览器等组成。面板可停靠、调整大小和分离到独立窗口，并有布局入口。Move/Rotate 工具有 Local/Global 区分，支持吸附；Play/Stop 承担仿真播放控制。[D1]

其优点在于操作对象始终明确：从树或三维选择对象，再查看关联属性；从工具结果定位对象，再回到三维理解问题。精确输入和空间操作相互补充，不强迫用户只靠拖动或只靠数字表理解机器人。

本项目已有工业项目树、SelectionService、共享 PropertyInspector、宿主三维呈现和名称映射设计。应核验这些链路在真实工程案例中是否接通、是否容易理解，而不是再增加另一套选择服务或对象命名缓存。业务项目树仍是主导航，宿主 WorkCell 树可作为辅助结构视图；USD Stage 不应直接成为本项目的设计数据权威。

### 2.2 逐步暴露复杂度

Joint Inspector 提供关节搜索、可选列、后端分组、多个独立检查器和多行编辑；属性不存在的单元格显示为空，并用提示解释原因，不伪装成 0。系统辨识采用 Robot → Data → Parameters → Check → Solve → Results 六步；结果页没有运行时显示明确的空态说明。[D4/D10，L3/L6]

本项目应将日常编辑放入共享检查器，将批量关节表、轨迹编辑、选型和优化等复杂操作放入专用工作页。既有 D5/D6 通道应保持，不能把全部参数塞入共享检查器。常用布局优先保证三维和当前任务所需信息；多窗口比较可作为后续增强，不宜先增加大量窗口。

### 2.3 直接解释问题

资产验证器将 Profile、Version、Validate、Cancel、进度、结果摘要和 Export Report 放在同一窗口，并提供搜索、Failures only、Actionable only。自碰撞检测器用表格列出两个刚体，选行后在三维用不同颜色定位。[D5/D8，L2]

本项目可复用其“摘要 → 问题列表 → 对象定位 → 修复入口 → 重新计算”的操作结构。工程状态仍遵循本项目的 Completed/Feasible/DataInsufficient 等契约；检查通过、任务执行结束和工程可行不能用一个绿色标记混为一谈。

## 3. 重叠模块逐项分析

### 3.1 建模、导入与物性编辑

**界面特点。** Stage 展示资产结构，Robot Inspector 用父连杆 → 关节 → 子连杆显示运动链；关节连线和方向箭头叠加在视口。密集关节重叠处可通过菜单选择。Joint Inspector 将限位、驱动、状态等参数按列组织，支持搜索、选列和多选编辑。[D3/D4]

**功能特点。** URDF 导入器处理模型、网格资源和 `package://` 路径映射；可以由视觉网格生成碰撞体，选择凸包、凸分解、包围球或包围盒；Base Type 提供 Source/Fixed/Mobile 语义。视觉、碰撞、材料、结构、物理后端和附加功能通过资产层组织，可复用原始资产并叠加配置。[D2/D6]

**典型操作。** File → Import，指定源与输出位置，核对包路径、底座和碰撞选项；导入后用 Robot Inspector 看运动链，用 Joint Inspector 检查关节参数，再执行资产验证。源资产与场景引用的修改范围需要明确。

**本项目可借鉴。**

1. 联动关节树、关节表、三维轴线和相关属性，让轴向、零位、限位错误能被看见。
2. 导入前明确策略，导入后展示字段映射、资源缺失、默认补全、忽略和不支持项，符合 MDL-03。
3. 显式区分视觉几何与碰撞几何，提供独立可见性和来源说明。
4. 对质量、质心、惯量、材料与密度显示单位、参考系、权威来源和估算标志。
5. 批量编辑显示涉及对象及变更预览，应用时按事务校验，不让部分关节悄悄成功、部分失败。

**边界。** Isaac Sim 的资产组织不是参数化 CAD、截面设计或有限元。不能以拖动缩放网格代替臂长/截面和物性一致修改；本项目应通过建模命令更新草稿并进行物理合法性校验。USD 不替代 `.rwdesign`、不可变项目修订或 runtime 编译权威。

### 3.2 工具、负载和工作场景装配

**界面与操作。** Robot Assembler 选择 Base Robot、Attach Robot 与双方 Attach Point；Begin Assembly 后预览装配位置，可用轴向 90°按钮或三维工具调整，随后 Assemble and Simulate 验证；Cancel 可撤销试装。直接编辑资产和在场景中引用资产的保存范围不同。[D7]

**本项目可借鉴。** 工具装配采用“选择法兰/安装点 → 输入或调整变换 → 预览 TCP、工具与负载 → 校验 → 应用”。同步展示工具质量、负载质量、质心、惯量和计入方式；基座、世界、法兰、工具、TCP 坐标系可以分别显隐。项目修订由 project 产生，预览不修改基线。

**边界。** Isaac Assembler 的固定关节通过物理求解器在播放期间约束装配。本项目设计模型的固定变换应具有确定语义，不能要求“先播放仿真”才能建立正确的法兰—工具变换。仿真稳定也不能代替工具连接强度或真实负载能力验收。

### 3.3 运动学与交互式目标验证

**界面与操作。** Follow Target 类示例在三维中展示机器人与可拖动目标；目标位姿变化后，求解器或运动策略生成关节命令。关节和 frame 名称、机器人基座姿态、活动关节配置参与解算。XRDF/机器人描述编辑器配置活动/固定关节、默认姿态、加速度和 jerk 限制等。[D9，L4/L7]

**功能特点。** FK/IK、末端 frame、基座坐标转换和仿真机器人控制具有清楚的接口分工。旧 Lula 教程展示当前构型作为 IK warm start、成功标志和不收敛时停止发出新动作；新安装的示例已采用 cuMotion 和 Robot Motion Experimental。官方旧 Lula FK/IK、RRT 和轨迹教程已标记 Deprecated，应提取操作思想并优先研究新接口，不能照搬旧模块路线。[D11/D12]

**本项目可借鉴。**

1. 同时提供数值目标输入和三维目标操纵器，显示目标使用的坐标系以及 position/orientation 约束模式。
2. 对照目标与实际末端位姿，展示位置/姿态残差、解算状态、搜索预算和初值来源。
3. 提供关节构型、多解、限位裕量和奇异性提示，支持预览某个解。
4. 修改基座或 TCP 后清楚地更新依赖结果状态；界面与计算消费同一编译变换。

**边界。** 目标跟随效果不等同于多解枚举、全域可达性或路径可行性。数值 IK 未收敛只能说明本次搜索没有找到有效解，不能推出“物理不可达”。残差和去重继续按 REQUIREMENTS 附录 D，不采用示例阈值。

### 3.4 碰撞、自碰撞与间隙

**界面与操作。** Self-Collision Detector 有机器人选择、Check Collisions、环境碰撞选项、碰撞对表、搜索、排序、双对象颜色高亮和批量过滤。XRDF 编辑器可创建/调整碰撞球、连接球、生成近似并导出机器人描述。[D8/D9，L4]

**功能特点。** 碰撞数据从抽象的“有/无”转为“哪两个对象、如何表示、为什么发生”。碰撞球用于运动规划近似，物理碰撞网格用于相应仿真求解；这两者未必是同一几何表示。

**本项目可借鉴。** 结果显示对象 A/B、发生工况、轨迹段、时间、几何表示及可用的距离/见证点；点击一行定位双方并回放时刻。视觉网格、计算碰撞体和安全距离分别呈现。已排除的碰撞对要可查看其规则与理由。

**边界。** Isaac 的 Filtered Pairs 用于忽略某些物理接触，Robot Inspector 的 Deactivate/Bypass/Anchor 是瞬态调试工具。这些操作不能直接成为本项目的正式安全结论。静态姿态检测不证明全轨迹无碰撞；碰撞球近似和接触偏置也不能代替工程安全距离。轨迹复检应保留 P-06 的步长、细分预算和 DataInsufficient 语义。

### 3.5 轨迹规划、时间参数化与回放

**界面与操作。** 官方新入口是 Window → Examples → Robotics Examples → Motion Generation → cuMotion，分别提供 Trajectory Generator 和 Trajectory Optimizer 示例。通过关节路点或任务空间路径规格生成轨迹，再按时间采样并跟随执行。[D13/D14]

**功能特点。** 新运动接口区分 Path、Trajectory 和 TrajectoryFollower：路径定义点列，轨迹提供 duration 与按时间查询状态的接口，跟随器负责执行。cuMotion 还区分响应式 RMPflow、图规划和轨迹优化。其世界接口在障碍物或基座移动后需要同步。[D12–D15，L5]

**本项目可借鉴。**

1. 轨迹段/路点表、三维路径和关节曲线互相定位。
2. 用同一个时间游标联动 TCP、关节位置/速度/加速度、力矩与超限诊断。
3. 明确几何路径、时间规律、预期命令轨迹与仿真响应之间的区别。
4. 将生成失败细化为输入、起终点、约束、碰撞、预算或数值问题；给出可操作的下一步。
5. 显示起始构型与当前/指定基线构型的一致性，不在播放时发生不明跳变。

**边界。** 本机 `Path.to_minimal_time_joint_trajectory()` 针对逐段线性关节路点、速度/加速度限制，采用梯形速度并在路点停止；它不是具有 jerk 上界的全局连续工艺最优轨迹。cuMotion 另一套生成器或优化器也不能仅凭名称推定满足本项目全部轨迹要求。RMPflow 是响应式策略，不构成全局无碰撞保证；搜索失败不构成不可行证明。规划后的正式验证仍须覆盖本项目限位、连续性、段内碰撞和工况要求。

### 3.6 动力学、物理状态与驱动参数调整

**界面与操作。** Simulation Data Visualizer 对选中对象显示位置、旋转、速度、加速度、质量和惯量等信息。Gain Tuner 有 Gain Settings 与 Test Gains：参数表和细节区设置驱动参数，测试支持 Snap to Limits、Sinusoidal、Step Function、Stress、dt Sweep，结果页将总结放在位置和 Effort 曲线上方；指令与观测曲线并列。[D16/D17，L3]

**功能特点。** 参数修改后立即用可重复测试解释影响；测试信号、时间步长、物理后端和结果绑定。本机 Gain Tuner 对不提供测量关节力接口的后端不生成 Effort 曲线，避免用简单 PD 估算冒充测量数据。官方区分物理步长、渲染步长和墙钟时间。[D18，L3]

**本项目可借鉴。**

1. 力矩/广义力曲线、峰值、峰值时间窗、完整循环 RMS、驻留段和三维姿态联动。
2. 结果区显示负载工况、重力方向、物性与摩擦参数来源、计算模型和适用假设。
3. 对比基线/改型、空载/带载、节拍改变前后，解释器件需求为何改变。
4. 运行设置与结果保留采样、时间参数、后端/求解器版本和容差。
5. 后端不支持、数据缺失和不适用显式呈现，不用 0 曲线填充。

**计算准确性的借鉴方式。** 在模型、坐标系、底座、负载、摩擦、时间与输出语义对齐后，可用 Isaac Sim 的刚体响应作外部对照的一层证据；必须同时有独立解析算例和物理性质检查。关节侧 RNEA 广义力、电机侧折算力矩、驱动命令 Effort、约束反力和投影关节力不能直接画等号。旋转关节用 N·m，移动关节用 N，并标明正方向。

**边界。** 仿真驱动增益不是实际工业伺服控制系统的完整参数。跟随稳定、仿真力矩曲线合理，不能直接推出电机热能力、减速器寿命、结构强度或真实急停安全。

### 3.7 系统辨识与模型校准

**界面与操作。** 本机 SysID UI 的六步为 Robot、Data、Parameters、Check、Solve、Results。官方工具入口为 Tools → Robotics → Asset Editors → System Identification，UI 默认需要启用。录入同步命令/观测数据，选择参数及边界，先检查质量和可辨识性，再优化，最后比较初始/最终参数、残差与验证数据表现，并导出结果。[D10，L6]

**功能特点。** 文档列出关节驱动/摩擦、执行器和延迟、质量/质心/惯量、限位等参数族，支持多种优化后端、敏感度和 held-out 验证。参数是否可辨识取决于激励与测量，不是增加迭代次数就能解决。本机结果页的“confidence”包含成本上升范围提示，不能自动解读为有统计覆盖保证的置信区间。

**本项目可借鉴。** 优先借鉴参数来源、参数边界、运行前检查、敏感度解释、验证工况分离、拟合前后对比和来源追溯。对工程研发尤其有用的是“一个参数为什么可信、对结果影响多大”。

**范围判断。** 新增实测数据接入、模型辨识算法或控制器校准，是独立需求扩展；本次打磨可先改善现有参数来源与可信度呈现。SysID 优化的目标是拟合已记录行为，本项目结构/传动优化的目标是产生更合适的设计，两者不能混为一个模块。

### 3.8 优化模块：必须进入首轮打磨

Isaac Sim 已有真正的优化相关能力，包括 cuMotion 轨迹优化和 SysID 参数拟合。因此值得借鉴的不只是“拖动目标”或三维效果，还包括有界参数空间、预检、约束与残差解释、验证数据、参数差异和运行记录。[D10/D14，L6]

本项目的 OPT-B/OPT-D 是更广的设计决策闭环：结构、传动和离散器件变量，静态及轨迹/动力/器件联合约束，Pareto 候选，Quick/Verified 分层，复现、鲁棒性和采用事务。**本次核对的 Isaac 模块没有形成与本项目等价的结构—轨迹—动力—电机/减速器联合 Pareto 选型工作流**；这不妨碍借鉴其交互和验证方法。

推荐把优化页面整理为六步：

1. **研究定义**：基线、允许改型范围、变量、上下界、量化/离散候选、硬约束与目标；非授权参数按现有规则锁定。
2. **预检与基线评估**：显示阻塞/警告、缺失工件和定位入口；先获得同工况基线指标。
3. **搜索运行**：进度、当前阶段、候选数、淘汰原因、缓存、随机种子；暂停/检查点按已启用能力呈现。
4. **候选比较**：Pareto 图与候选表、变量差异、八项指标和约束裕量联动；不可算项显示“—”，点击说明原因。
5. **复核**：展示 Verified 复算与要求的灵敏度/鲁棒性结果、工况覆盖和证据是否齐备。
6. **采用**：先只读预览候选；显示变更摘要，通过既有守卫创建方案分支和新修订，完整复算。

上述前后步骤应跨批核验：B0 建立案例和状态基线，B1 检查变量/绑定/编译身份，B2/B3 检查候选依赖的计算，B4 集中打磨搜索和取舍，B5 精修交互。不能到最后才发现优化工作流与上游未接通。

若某项鲁棒性阈值或启用前置尚未完成，应按当时权威需求/契约呈现，不能引入 Isaac 示例数值绕过 P-04 或 OPT-D 门禁。

### 3.9 电机、减速器与传动设计

Joint Inspector 和 Gain Tuner 展示力/速度限制、驱动参数及部分后端性能包络，这可以借鉴为本项目“需求曲线与能力曲线在同一图上”的呈现方式。[D4/D17]

更直接的打磨项是：工作点与电机转矩—转速曲线联动，显示峰值、RMS、持续时间、降额、制动/保持和安全系数；减速器显示速比、效率、输入转速、允许载荷等淘汰理由；候选组合选中后定位最不利时刻和对应姿态。厂商曲线的适用范围、插值和禁止外推应清楚可见。

本次研究的 Isaac 工具没有与本项目 SEL-01～SEL-04 等价的版本化厂商电机/减速器目录和工程筛选报告。其 Joint Drive 参数不能代替真实器件数据、精确传动映射或工作制校核。

### 3.10 项目、诊断、执行与报告

Isaac 的资产层/引用/变体有利于原始资产复用和场景配置；示例浏览器有助于用户快速获得可运行起点；验证器和 SysID 提供 JSON 等导出记录；运行工具有取消、状态和进度。也存在值得超越之处：本机 SysID 结果页说明 headless 结果不会自动导入当前页面，多个工具的导出不自动形成统一研发报告。[D2/D5/D10，L2/L6]

本项目可借鉴示例入口、配置模板、诊断导航和运行结果卡，但应发挥现有 evidence/project/reporting 的优势：统一快照身份、策略、目录版本、工况覆盖、正式判定、候选变更与报告。参数变化后显示结果为何失效，保留旧证据；导入外部结果必须核对身份，不把旧运行当作当前计算。

**范围边界。** 传感器、合成数据、ROS 2 和强化学习是 Isaac 的重要能力，主要服务仿真开发。与当前机械臂设计主线重叠有限，建议在模型导出与外部验证接口阶段再考虑，不作为 ABCD 收口的默认前置。动态扩展管理也不直接迁入本项目正式插件装配；保持既有静态允许清单和架构红线。

## 4. 可借鉴项点及实施归属

优先级：P0＝阻碍工程理解或可能误导结论，优先核验；P1＝首轮完整案例应打磨；P2＝后续增强。这里的 P0/P1/P2 是打磨建议优先级，不修改 REQUIREMENTS 的需求优先级。

范围类型：“既有”表示已有需求或设计承载，先核验实际完成度；“呈现”表示在现有语义内改进 UI/解释；“扩展”表示不能默认作为已授权产品实现，须登记需求与任务。以下验收描述是建议检查方法，正式 AT 与容差仍以权威文档为准。

| 编号 | 借鉴项点 | 归属与批次 | 优先级/范围 | 建议核验方式 |
| --- | --- | --- | --- | --- |
| I01 | 项目树、三维、检查器、搜索选择联动 | ui/runtime；B0/B5 | P0，既有 | 同一关节四处定位一致；纯草稿和仅运行时对象状态如实显示 |
| I02 | 运动链方向、关节轴与坐标系叠加 | modeling/ui/runtime；B1 | P1，既有/呈现 | 在非零偏置、扭角及基座姿态下逐轴核对 |
| I03 | 导入策略与字段映射/补全报告 | modeling/io/diagnostics；B1 | P0，既有 | 缺惯量、网格、包路径和可动分支均有明确报告，无静默猜测 |
| I04 | 关节参数表搜索、分组、批量编辑预览 | modeling/ui；B1/B5 | P1，呈现 | 六轴修改可追踪；非法批量输入不造成部分提交 |
| I05 | 缺失/不适用/不支持状态不显示为零 | ui/各数据提供单元；B1/B5 | P0，既有/呈现 | 用缺物性和不支持字段反例检查表格、图表和报告 |
| I06 | 视觉与碰撞几何分开显隐并注明来源 | modeling/runtime/ui；B1/B2 | P0，既有/呈现 | 视觉复杂、碰撞简化的模型仍能识别实际验证几何 |
| I07 | 工具装配预览、TCP 与负载来源 | modeling/requirements/ui；B1 | P0，既有/呈现 | 改工具后 FK、重力、负载计入和修订记录一致 |
| I08 | IK 数值目标与三维目标联动 | kinematics/ui；B2 | P1，既有/呈现 | 坐标系切换、基座移动后仍对准目标，残差可查看 |
| I09 | 多解、限位裕量、奇异与搜索失败解释 | kinematics/diagnostics/ui；B2 | P0，既有/呈现 | 未收敛不输出不可达；同末端位姿的不同构型仍保留 |
| I10 | 碰撞对列表、双对象高亮、时刻定位 | runtime/trajectory/ui；B2 | P0，既有/呈现 | 段内碰撞能从结果回到对象、段和姿态，过滤理由可追溯 |
| I11 | 路点/段表、三维路径、曲线联动 | trajectory/ui；B2/B5 | P1，既有/呈现 | 选段/选时刻对齐，起始构型与驻留段含义明确 |
| I12 | 路径—时间规律—执行响应明确区分 | trajectory/execution/ui；B2 | P0，既有/呈现 | 规划曲线与仿真曲线不能无标识地互相代替 |
| I13 | 统一时间游标与峰值回放 | dynamics/trajectory/ui；B3 | P0，既有 | 峰值、RMS、最不利姿态来自同一快照和时轴 |
| I14 | 参数来源、单位、参考系和可信度 | modeling/dynamics/evidence/ui；B1/B3 | P0，既有 | 改质心或惯量时基准一致；缺失来源进入缺失清单 |
| I15 | 基线/改型/工况曲线叠加比较 | dynamics/selection/ui；B3/B5 | P1，既有/呈现 | 同要求、同工况比较；关节侧/电机侧和 N/N·m 明确 |
| I16 | 能力曲线、需求工作点与淘汰理由 | selection/drivetrain/ui；B3 | P0，既有/呈现 | 超范围不外推；最不利工作点能定位到时刻和姿态 |
| I17 | 优化分步骤工作流和 Preflight | optimization/workflow/ui；B0/B4 | P0，既有/呈现 | 缺工件、绑定冲突、授权边界阻止错误运行并提供定位 |
| I18 | 变量边界、基线评估和参数差异 | optimization/modeling/evidence/ui；B1/B4 | P0，既有 | 候选补丁与编译身份吻合；对比前有一致基线 |
| I19 | Pareto 图、候选表、三维预览联动 | optimization/ui；B4/B5 | P1，既有/呈现 | 八项指标全部展示；不可算为“—”；不以单分数替代取舍 |
| I20 | Verified 与灵敏度/鲁棒性复核独立展示 | optimization/policy/evidence；B4 | P0，既有 | 搜索改善不等于正式通过；按已冻结协议和启用前置复核 |
| I21 | 采用候选先预览、再创建新修订复算 | optimization/project/workflow/ui；B4 | P0，既有 | 预览不改基线；采用后上游身份/旧证据/新结果归属明确 |
| I22 | 诊断摘要、过滤、修复入口与重新验证 | diagnostics/ui/各领域；B0/B5 | P0，既有/呈现 | 稳定码、中文原因、实际值/阈值/单位、对象和下一步完整 |
| I23 | 进度、取消、失效结果和恢复状态真实 | execution/project/evidence/ui；B0/B6 | P0，既有 | 取消/切项目/迟到结果/检查点恢复不误归属，不重复统计 |
| I24 | 六轴示例工程、配置模板和可复核报告 | io/project/reporting/workflow；B0/B5/B7 | P1，既有/呈现 | 工程师可从样例完成改型到报告；版本、工况和证据齐备 |
| I25 | 实测参数辨识及独立数据验证 | 新增或扩展业务能力；后续 | P2，扩展 | 先定义数据协议、可辨识性、算法和独立验证验收 |
| I26 | USD/ROS/外部仿真交换与结果回导 | io/runtime/evidence；后续 | P2，扩展 | 明确支持子集、名称/单位映射、往返损失和证据归属 |

优先工作不是新增全部项点，而是对 I01/I03/I05/I07/I09/I10/I13/I16/I17/I21/I22 建立真实案例检查清单，复现现况后登记具体缺口。已有功能只需消除歧义或改善联动时，不新增第二套数据结构或计算规则。

## 5. 以六轴改型与选型落地的步骤

### 5.1 第一个交付：可检查的基线工程（B0/B1）

选定已有六轴机型，固定原始来源与参数质量，装配工具、负载和工位，登记软件/模型/策略/目录身份。做一次实际操作记录：导入 → 找到关节 → 看轴线与物性 → 修改参数 → 应用 → 计算。逐项记录阻断、歧义和缺失入口。

交付物是基线工程包、参数来源清单、状态与布局截图/操作记录、借鉴项点的现况核验表。没有实测/厂商数据的部分如实标明；本机经过处理的 UR10 smoke 示例不冒充工程基线。

### 5.2 第二个交付：计算结果能解释原因（B2/B3）

以工作范围改型、工具负载改变和节拍提升为三类变化，检查 FK/IK、碰撞、轨迹、RNEA、传动映射、器件筛选及状态传播。打通“诊断/曲线 → 时间 → 关节/物体 → 三维姿态”的解释路径。

独立计算验证与界面验证分开留痕：解析/独立参考、数学物理性质、边界反例、真实装配流程共同支撑；Isaac 的外部对照只是其中一层。外部对照前统一长度、角度、坐标系、零位、关节顺序、质量/惯量基准、底座、负载、摩擦和时间；各类力的输出语义单独核对。数值容差继续采用本项目权威要求。

### 5.3 第三个交付：完整优化决策闭环（B4/B5）

定义改型允许范围，评估基线，运行约束搜索，对比基线与至少两个候选，检查 Verified、所需鲁棒性与工况覆盖，采用一个候选，产生新修订并完整复算，导出可复核报告。

同时加入反例：缺惯量、器件曲线范围外、搜索未找到解、轨迹预算耗尽、运行取消、候选过期和保存失败。没有失败路径的演示不足以证明研发可用性。

### 5.4 第四个交付：工程师试用与可靠性收口（B6/B7）

在计算和工作流稳定后核验大模型、长轨迹、搜索运行、项目切换、保存与恢复、布局记忆和部署。按既有打磨计划邀请至少五名目标工程师完成贯穿案例，检查任务完成率、误操作、诊断理解和报告复核；采用既有 UX 验收口径，不用画面相似程度作为验收标准。

实施任务仍按项目三段流程推进：登记单契约 → 实施与真实验证留痕 → 全新上下文独立验收 → 所有者决策合入。借鉴不会豁免中文契约注释、L2 零 Qt、业务域互链禁止、唯一权威和不可变修订等要求。

## 6. 可直接用于评审的判断

- **立即借鉴并核验**：对象联动、物理信息明确呈现、可定位诊断、曲线与三维回放、优化预检和候选差异、真实状态和结果导出。
- **适合后续增强**：实测数据辨识、更多布局/并列检查器、USD/ROS/外部仿真交换。
- **保持本项目工程语义**：多工况必验证、Quick/Verified、器件目录与能力判据、联合优化、不可变修订、证据与报告。
- **不能由仿真替代证明**：结构强度/刚度/疲劳、热设计、实际伺服性能及未被当前模型覆盖的器件寿命和安全能力。

本报告完成的是有来源的功能、界面和操作对照，以及借鉴项点与打磨顺序。尚未完成现场 UI 操作、机器人参数标定、两软件数值交叉测试或本项目阶段验收。

## 7. 参考资料

### 7.1 本机核对的文件

- 版本：[VERSION](D:/software/isaacsim/VERSION)
- L1 导入选项：[option_widget.py](D:/software/isaacsim/exts/isaacsim.asset.importer.urdf.ui/isaacsim/asset/importer/urdf/ui/impl/option_widget.py:154)
- L2 验证命令和过滤：[window.py](D:/software/isaacsim/exts/isaacsim.asset.validation.ui/isaacsim/asset/validation/ui/window.py:287)
- L3 Gain Tuner 测试：[ui_builder.py](D:/software/isaacsim/exts/isaacsim.robot_setup.gain_tuner.ui/isaacsim/robot_setup/gain_tuner/ui/ui_builder.py:4964)；测量 Effort 的后端边界见同文件 6542 行附近。
- L4 关节描述：[joint_properties_panel.py](D:/software/isaacsim/exts/isaacsim.robot_setup.xrdf_editor/isaacsim/robot_setup/xrdf_editor/ui/joint_properties_panel.py:143)
- L5 轨迹接口：[trajectory.py](D:/software/isaacsim/exts/isaacsim.robot_motion.experimental.motion_generation/pip_prebundle/isaacsim/robot_motion/experimental/motion_generation/impl/trajectory.py:23)；时间参数化：[path.py](D:/software/isaacsim/exts/isaacsim.robot_motion.experimental.motion_generation/pip_prebundle/isaacsim/robot_motion/experimental/motion_generation/impl/path.py:89)
- L6 SysID 六步：[workflow_model.py](D:/software/isaacsim/exts/isaacsim.robot_setup.sysid.ui/isaacsim/robot_setup/sysid/ui/workflow_model.py:22)；结果与空态：[results_panel.py](D:/software/isaacsim/exts/isaacsim.robot_setup.sysid.ui/isaacsim/robot_setup/sysid/ui/results_panel.py:155)
- L7 新版目标跟随：[follow_target.py](D:/software/isaacsim/exts/isaacsim.robot_motion.examples/isaacsim/robot_motion/examples/manipulation/interactive/follow_target_motion_generation/follow_target.py:38)
- A 既有样例：[run_ur10_sample.py](D:/software/IsaacAssets/run_ur10_sample.py)；[运行摘要](D:/software/IsaacAssets/run_artifacts/summary.json)

### 7.2 NVIDIA 官方资料

以下页面在研究日已读取。路径含 `latest`；实施前应与当时选定版本核对。

- D1 [User Interface Reference](https://docs.isaacsim.omniverse.nvidia.com/latest/gui/reference_user_interface.html)
- D2 [Asset Structure](https://docs.isaacsim.omniverse.nvidia.com/latest/robot_setup/asset_structure.html)
- D3 [Robot Inspector](https://docs.isaacsim.omniverse.nvidia.com/latest/robot_setup/robot_inspector.html)
- D4 [Joint Inspector](https://docs.isaacsim.omniverse.nvidia.com/latest/robot_setup/joint_inspector.html)
- D5 [Asset Validation](https://docs.isaacsim.omniverse.nvidia.com/latest/robot_setup/asset_validation.html)
- D6 [URDF Importer](https://docs.isaacsim.omniverse.nvidia.com/latest/importer_exporter/ext_isaacsim_asset_importer_urdf.html)
- D7 [Robot Assembler](https://docs.isaacsim.omniverse.nvidia.com/latest/robot_setup/assemble_robots.html)
- D8 [Robot Self-Collision Detector](https://docs.isaacsim.omniverse.nvidia.com/latest/robot_setup/ext_isaacsim_robot_setup_collision_detector.html)
- D9 [Robot Description and XRDF Editor](https://docs.isaacsim.omniverse.nvidia.com/latest/manipulators/manipulators_robot_description_editor.html)
- D10 [System Identification](https://docs.isaacsim.omniverse.nvidia.com/latest/robot_setup/ext_isaacsim_robot_setup_sysid.html)
- D11 [Lula Kinematics Solver（已标弃用）](https://docs.isaacsim.omniverse.nvidia.com/latest/manipulators/manipulators_lula_kinematics.html)
- D12 [cuMotion RMPflow](https://docs.isaacsim.omniverse.nvidia.com/latest/cumotion/tutorial_rmpflow.html)
- D13 [cuMotion Trajectory Generator](https://docs.isaacsim.omniverse.nvidia.com/latest/cumotion/tutorial_trajectory_generator.html)
- D14 [cuMotion Trajectory Optimizer](https://docs.isaacsim.omniverse.nvidia.com/latest/cumotion/tutorial_trajectory_optimizer.html)
- D15 [Trajectory Planning and Execution](https://docs.isaacsim.omniverse.nvidia.com/latest/motion_generation/trajectory_planning.html)
- D16 [Simulation Data Visualizer](https://docs.isaacsim.omniverse.nvidia.com/latest/physics/ext_isaacsim_inspect_physics.html)
- D17 [Gain Tuner](https://docs.isaacsim.omniverse.nvidia.com/latest/robot_setup/ext_isaacsim_robot_setup_gain_tuner.html)
- D18 [Simulation Fundamentals](https://docs.isaacsim.omniverse.nvidia.com/latest/physics/simulation_fundamentals.html)

### 7.3 项目权威与打磨计划

- [REQUIREMENTS.md](D:/10_Source_Repos/21_robot/RobWork/RobWork/doc/industrial-robot-design/REQUIREMENTS.md:165)：评估模式、RequiredEvidenceProfile、MDL/KIN/TRJ/DYN/SEL/OPT、UX 与数值容差。
- [ui.md](D:/10_Source_Repos/21_robot/RobWork/RobWork/doc/industrial-robot-design/units/ui.md:251)：选择服务、工业项目树、共享检查器与复杂页通道。
- [optimization.md](D:/10_Source_Repos/21_robot/RobWork/RobWork/doc/industrial-robot-design/units/optimization.md)：OPT-B/OPT-D、候选、运行和采用契约。
- [ARCHITECTURE.md](D:/10_Source_Repos/21_robot/RobWork/RobWork/doc/industrial-robot-design/ARCHITECTURE.md)：分层、端口和权威归属。
- [工程质量打磨计划](D:/10_Source_Repos/21_robot/RobWork/RobWork/docs/superpowers/plans/2026-10-14-engineering-quality-polish-plan.md)：B0–B7 与六轴贯穿案例。本报告补充借鉴对象及检查项，不替换该计划。
