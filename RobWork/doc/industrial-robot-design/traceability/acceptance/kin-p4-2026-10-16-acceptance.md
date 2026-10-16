# KIN-P4 独立验收记录（attempt 1）

| 字段 | 值 |
| --- | --- |
| 任务 | KIN-P4 正式产品闭环（宿主服务装配） |
| 送验对象 | 分支 `kin-p4` @ `8d6ece01565294380eb9fb0cdd2cf262dee26029`（与 `origin/kin-p4` 一致——`git ls-remote` 核对，零漂移） |
| 验收人 | P4 验收者（对抗式独立验收——与实施者零共享上下文，全新会话） |
| 验收日期 | 2026-10-16 |
| **结论** | **PASS（attempt 2 复核——零 blocker；attempt 1 的 FAIL 见 §8）** |

---

## 1. Verdict 与一句话理由

**attempt 2（复核 21432b0b）**：阻断项 B-1 已消除——端到端用例 `SixAxisVariantClosedLoop_KIN_P4` 载体已换为六轴无旋 TCP 变体（黄金算例 C 六转动关节链＋TCP 纯平移偏置），验收探针 A~F 场景全部转正为产品测试断言；验收人重建目标并全量复跑 `sdurws_ird_kinematics_gui_test` **14/14 通过**（六轴端到端闭环真实执行），`ird_gates` 整改后独立复跑零命中，单元卡 v0.26 勘误段与留痕重生成入库，整改提交范围收敛（4 文件）、产品源码零改动。五项必做修复在 attempt 1 已全部根因层核实（§2），本轮零回归。**最终结论：PASS（零 blocker）**。

**attempt 1（8d6ece01）原始裁定**：五项必做修复（降级服务接通／通道真实分发／TCP 三入口／导出复算闭环／配置持久化）在根因层全部真实落地，回归测试 31 项断言强、可追溯，定向复跑与门禁独立复跑全绿——但任务书验收标准的核心句「以已有六轴臂改型案例……走通全过程」未达成：GUI 端到端闭环用例名为 SixAxis、实际载体是二连杆平面臂；验收人以独立探针实证六轴臂变体在同一 GUI 流程下全过程可行，选型依据不成立。判 fail（1 项阻断级）。

---

## 2. 逐项核对表（任务书必做项 → 根因层证据）

| # | 必做项 | 核对结论 | 根因层证据（path:line） |
| --- | --- | --- | --- |
| 1a | 降级服务逐项接通（KinematicsPluginAssembly.cpp:163 起） | ✅ 七缝真实接通 | 通道值面表尾增列七缝 `KinematicsPanelChannels.hpp:499`（configPersist 等）；翻译单点 `KinematicsPluginAssembly.cpp:160-190`（指针/函数缝直传）；宿主半区 `ui/plugin/KinHostServicesAssembly.cpp`（网关 fetchBaseline 真实查询端口取数 ：49-103、submit 信封转译 ：105-170、modeling Codec 补丁 ：176-205、io AtomicFile 真实导出 ：211-257、QSettings×canonical 配置持久化 ：263-312、named-pose-set Home 来源 ：318-342）；产品物宿主值成员持有 `UiPlugin.hpp:572-585`（真实 `kinematics::IkSolver`/`FkEvaluator`/`KinSessionPose`——非替身）；七缝注入 `UiPlugin.cpp:3049-3059`；项目打开拍命令门面 `UiPlugin.cpp:3064-3090` |
| 1b | 会话姿态（KIN-06/KIN-P3 A09 收尾） | ✅ | `resetSessionHomeFromNamedSource` 消费 homePose 具名来源缝（`KinematicsPanelWidget.cpp:623-631`）；Home 闭包从项目闭包 named-pose-set 保留键解析（`UiPlugin.cpp:2936-2986`） |
| 2 | KinEvaluationChannel SessionSolve/TaskPointsBatch 真实分发（原 332/337 拒绝） | ✅ 拒绝退役为真实分发 | 受理分流 `KinEvaluationChannel.cpp:485-545`（SessionSolve 装配完整 IkRequest→`launchSolveTask` 真实 `solver->solve` :702-760；TaskPointsBatch 切片→`launchBatchTask` 真实 `runBatchComputation` :762-830）；原「两 kind 整体拒绝」语义退役，载荷完整性拒绝保留 ERR-01（:488-494 缺目标、批量切片失败 ：520-526）；换绑置位任务级 atomic 取消标志（:109 注、launchSolveTask 取消探针 :717-721）——真实可取消路径 |
| 3 | 默认 TCP 三入口统一名称解析 | ✅ 解析单点贯通 | 单点 `resolveSessionTcp`（`KinPanelFlows.cpp:46-85`，KIN-14 默认工具权威＋KIN-NO-TCP 显式拒绝）；入口①位姿页刷新 `KinematicsPanelWidget.cpp:392-405`（修复前空 TcpRef）；入口②求解装配 `KinPanelFlows.cpp:92`（消除第二份解析）；入口③设默认 TCP `KinematicsPanelWidget.cpp:634-648`（修复前恒传 `TcpRef{}`）；全仓 grep 产品代码零残留空 TcpRef 构造（仅修复说明注释） |
| 4 | 结果导出真实写出与复算重绑定闭环 | ✅ | 内联装配补 requestIdentity 步骤 8（`KinPanelFlows.cpp:166-176`——修复前解集身份恒缺省→导出头快照锚全零）；exportSessionResults 真实写出（:698-760，域设施打包＋exportWriter 缝）；复算编排 `recomputeSessionFromIdentity`（:568-680：拒绝态三前置、`buildKinRecomputeRequest` 消费、seed/referenceQ 自原身份显式携带、快照重绑定当前锚）；结果页「复算（当前快照）」按钮 `KinematicsPanelWidget.cpp:677-715` |
| 5 | 求解配置持久化缝 | ✅ | 保存 `makeKinConfigPersist`（`KinHostServicesAssembly.cpp:263-288`，canonical 编码＋sync 落盘）；恢复 `restoreKinConfig`（:290-312，双段校验、损坏如实 nullopt 回退）；宿主恢复注入 `UiPlugin.cpp:2868-2895`；面板编辑保存消费 `KinematicsPanelWidget.cpp:796`（`applyConfigurationEdit` → configPersist 缝） |

## 3. 验收标准与尽力项核对

| 项 | 结论 | 说明 |
| --- | --- | --- |
| 六轴臂改型案例全过程（验收标准核心句） | ❌ **阻断** | `KinHostLoopGuiTest.cpp:73-97`：`twoLinkV1Model`/`twoLinkV2Model`＝二连杆平面臂（2 dof），非六轴臂；用例名 `SixAxisVariantClosedLoop_KIN_P4`（:231）与载体名实不符。全过程其余环节（建改型→两任务点→求解/比较→预览→导出→重开复算）在真实控件事件＋真实求解器＋真实文件写出层完备（:231-339），但案例载体不满足任务书字面。**验收人独立探针实证六轴可行**（§4.5）——选型依据不成立 |
| 跳过的 GUI 呈现登记用例转真实执行 | ✅ | `KinHostLoop.PanelPresentation_RealExecution_KIN_P4`（:349-377，四页签/按钮门控/状态行真实执行）；`PluginPanelTest.cpp:819-836` 登记行注释互链更新（skip＝无界面目标环境事实，真实执行面已迁移 gui_test——如实登记非验证替代） |
| 尽力项：长求解响应性（真实后台可取消） | ◐ 按 openIssues 处理 | 真实后台＋真实可取消已落地（SolveChannel 后台通道＋换绑置位取消——`SolveChannel_RebindCancelsInflight_KIN_P4` 钉扎）；面板「取消」按钮入口未做（实施者 openIssues② 如实登记——任务书允许） |
| 尽力项：位姿页完整姿态编辑/显示 | ✗ 未做（openIssues 登记） | 目标位姿仍固定单位旋转、姿态矩阵完整编辑未实施（实施者 openIssues① 如实登记——任务书明示「做不到写 openIssues」）；**但其不构成六轴案例不可行的理由**（见阻断项） |
| 构建门禁含 sdurws_ird_ui_plugin 与 sdurws_ird_studio | ✅ | 实施者构建命令含两目标（builds/kin-p4/build-integrated.log）；验收人独立复跑 ird_gates 零命中（§4.6） |

## 4. 验收人真实执行的验证（独立证据）

### 4.1 分支与推送
- `git rev-parse HEAD` → `8d6ece01…6029`；`git status -sb` → `## kin-p4...origin/kin-p4`（无 ahead/behind）；`git ls-remote origin kin-p4` → 同 SHA。零漂移。

### 4.2 代码审查
- §2 表格全部 path:line 由验收人逐文件读源码核对（非采信自报）。

### 4.3 定向复跑（验收人亲测，非复述留痕）
命令：测试 exe 绝对路径＋指定 PATH＋`QT_QPA_PLATFORM=windows`＋`--gtest_filter`：
- `sdurws_ird_kinematics_gui_test.exe --gtest_filter=KinHostLoop.*` → **2/2 通过**（SixAxisVariantClosedLoop_KIN_P4、PanelPresentation_RealExecution_KIN_P4）
- `sdurws_ird_kinematics_test.exe --gtest_filter=KinP4Flows.*` → **6/6 通过**
- `sdurws_ird_ui_test.exe --gtest_filter=KinHostServicesAssembly.*:KinEvaluationChannel.*KIN_P4*` → **12/12 通过**（8 网关/补丁/导出/持久化＋4 通道）

### 4.4 门禁独立复跑
- `cmake -DIRD_ROOT=…\industrialrobot -P …\cmake\ird_gates.cmake` → **「全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中」**。

### 4.5 ★ 六轴 GUI 可行性探针（本验收的决定性证据）
验收人构造独立探针 `audit/p4-acceptance/SixAxisGuiFeasibilityProbe.cpp`（审查工作材料，不入库；不改任何产品代码），以**六轴无旋转 TCP 偏置变体**（黄金算例 C 去 Rz(π/2)——零位姿态＝单位阵，与面板目标编辑面同构）走真实 `KinematicsPanelWidget` 控件事件＋真实 `IkSolver`/`FkEvaluator`＋真实文件写出：
- A3：六轴 v1 GUI 求解（目标＝零位 TCP XYZ＋单位姿态）→ **解表非空** ✅
- B1：双击预览 → 会话姿态回写 **dof=6** ✅
- C2/C3：真实导出 → 文件存在＋`ird-kin-export` 头＋快照锚非零 ✅
- D2/D3/D4：改型（j2 高度 0.4→0.5 m）复算 → **身份重绑定为 v2 锚＋seed=7 携带** ✅
- E2/E3/E4：缩短改型（全展 1.65→0.3 m）复算 → **「新解 0 个」解析不可达如实翻转＋身份仍重绑定** ✅
- F1：改型后可达目标（v2 零位 TCP）GUI 求解 → **解非空（强断言）** ✅

**结论：六轴臂改型案例在同一 GUI 流程下全过程可行——实施者交付说明中「姿态约束下平面臂可达位置仅全展点」的载体选型依据不构成六轴不可行的证明。**

探针附记（非阻断）：场景 D5 显示六轴改型后温和偏移目标（v2 下原目标 (1.65,0,0.5)，相对 j2 俯仰 ≈0.06 rad 的解析可达点）复算 0 解——局部搜索从零位初值未收敛，状态行「原解 1 个 → 新解 0 个」如实呈现（DataInsufficient 保守语义正确，审查 §3 A02 段确认该语义应保留）。这提示 IK 局部收敛在改型复算场景有工程风险（审查 §1 已登记「不保证全局收敛」——既有特性，非本包新缺陷），也是六轴端到端测试设计时应选 F1/E 形态确定性场景而非 D 形态的原因。

### 4.6 红线与注释抽查
- **L2 零 Qt**：P4 对 `kinematics/include/` 公共头零改动；`KinematicsPanelChannels.hpp` 无 Qt include；P4 新增 Qt 消费全部在 ui/plugin（宿主面合法）与 gui_test。
- **R-1/R-2**：新 TU `KinHostServicesAssembly.{hpp,cpp}` include 全部公共头（core/kinematics/project/runtime——O-31 装配层特权）；kinematics 侧 include 的 `ird/ui/` 头均为既有公共头消费（UI-T08 先例，非本包新增）。
- **SA-02**：`git diff --stat e53a0c3f..8d6ece01` 40 文件全部位于 `industrialrobot/` 与 `doc/`——零框架源码触碰。
- **PA-2**：复算/导出/设默认全部经命令或会话态，零历史修订改写（`recomputeSessionFromIdentity` 零修订调用，代码面核实）。
- **注释规范**：抽查 KinHostServicesAssembly.cpp/.hpp、KinEvaluationChannel.cpp 新增段、KinPanelFlows.cpp 新增编排、KinHostLoopGuiTest.cpp——文件头/类/函数 Doxygen 中文注齐全，单位（rad/m/ms）、坐标系（基座系 {B}）、错误语义（ERR-01/KIN-NO-TCP/NoDevice）、确定性来源（canonical 编码/seed）标注完备。仅两处过期/失实注释见 §5 建议级。

## 5. 问题清单

### 阻断级（1 项）

| 阻断级（1 项） |

| # | 问题 | 证据 |
| --- | --- | --- |
| B-1 | **【attempt 2 已消除——见 §8】**验收标准核心句未达成：GUI 端到端闭环案例不是六轴臂。任务书验收标准「以已有六轴臂改型案例……走通全过程并留证据」；交付用例 `SixAxisVariantClosedLoop_KIN_P4` 载体为二连杆平面臂（twoLinkV1/V2，2 dof），且用例名保留 SixAxis 之名——后来者会据测试名误读覆盖面。验收人探针（§4.5）实证六轴变体全过程可行，选型依据（六轴不可行）不成立。修复建议：将该用例载体六轴化或增设六轴闭环用例（探针 A/B/C/D/E/F 场景可直接转正——含 dof=6 预览回写、六轴真实导出、改型复算重绑定与解析不可达翻转）；同步更正用例名/注释与单元卡 §14.6 载体描述 | `kinematics/gui_test/KinHostLoopGuiTest.cpp:73-97`（twoLink 模型构造）与 ：231（用例名）；对照探针输出（audit/p4-acceptance 运行留痕） |

### 建议级（不阻断，随返工或后续包消账）

| # | 问题 | 证据 |
| --- | --- | --- |
| A-1 | 六轴改型后温和偏移目标（解析可达）复算 0 解——局部搜索未收敛，结论诚实呈现但工程上有「改型复算误报不可行」风险；建议随 IK 收敛性任务（审查 P5 范围）评估多初值/初值策略在复算路径的表现 | 探针 D5 现象（§4.5 附记） |
| A-2 | 【attempt 2 已消除】`KinHostLoopGuiTest.cpp:112-113` LoopHarness 字段注释过期——整改重构中 modelV1/modelV2 字段删除、新注释「当前视图（内容随改型换绑）」名实相符 | 同左 |
| A-3 | 【attempt 2 已消除（间接）】`KinP4FlowsTest.cpp:73` 注释「与 gui_test 端到端同一计算面」——整改后 gui_test 端到端载体亦为六轴（sixAxisNoRotTcp 同关节链），该注释名实相符 | 同左 |
| A-4 | 位姿页完整姿态编辑/显示、面板「取消」按钮、批量归档链、named-pose-set 编辑入口（建模侧）——实施者 openIssues 已如实登记，归后续包/所有者 | 实施者 openIssues①②③④；任务书尽力项条款 |

## 6. 留痕与提交核对

- 留痕 `traceability/builds/kin-p4/` 16 文件真实存在；XML 数字与自报一致（kinematics 251＝250 过＋1 登记 skip、contract 57/57、gui 14/14、ui 305/305、ui_contract 42/42，零失败）。
- 提交 `8d6ece01` 标题 `[KIN-P4] 动词开头中文摘要`、正文四段（What/Why/验证/影响面）齐备，验证结论与留痕一致——**未发现粉饰**。
- `traceability/kin-p4-gate-registrations.md` 两条测试面直链登记在册。
- validate-docs（实施者跑，PASS）——验收人抽查单元卡 v0.26/v1.104 版本行与 §14.6/§16.7 详情行在位。

## 7. 验收者环境

- Windows 10.0.26200 x64；VS2022 17.12（VsDevCmd -arch=x64）；CMake D:/software/CMake/bin；Qt 6.11.1 msvc2022_64；仓库根 `D:\10_Source_Repos\21_robot\RobWork`（分支 kin-p4）。
- 未执行：全量五套件重跑（编排脚本已跑且全绿，验收人按任务书只做定向复跑）；sdurws_ird_studio 实机人工全交互（任务书明示归所有者）；独立冒烟模式重配（集成树复跑＋门禁已覆盖关键面）。

---

**验收人落款**：P4 验收者（独立会话）。本记录只陈述验证过的事实与可复现的命令；未验证之处已如实标注。

---

## 8. attempt 2 复核记录（2026-10-16，送验对象 21432b0b）

### 8.1 复核范围
attempt 1 裁定的阻断项 B-1 逐条消除核查（整改提交 `21432b0b65232947a6870d532489db045ca09d8c`，基点 `5a334db6`＝attempt 1 验收记录提交）。

### 8.2 逐条核查结果

| 核查点 | 结论 | 证据（验收人真实执行/阅读） |
| --- | --- | --- |
| 载体换六轴（B-1 核心） | ✅ | `KinHostLoopGuiTest.cpp`：`twoLinkV1Model/twoLinkV2Model` 删除，新增 `sixAxisNoRotTcp(j2z,j3x,j4x,j5x,j6x,tcpx)`（黄金算例 C 六转动关节链＋TCP 纯平移偏置——零位姿态＝单位阵，与面板目标编辑面对齐）＋`sixAxisV1/V2/V2sModel` 三代改型（diff 通读确认；与验收探针模型构造逐参一致） |
| 探针 A~F 场景转正 | ✅ | 用例三阶段：①v1 全链（两任务点→真实求解解非空→rank 稳定序→双击预览 `EXPECT_EQ(...size(), 6U)` dof=6 钉→真实导出＋快照锚非零钉）→温和改型 v2 复算（身份重绑定 v2 锚＋seed=7 携带；新解数**如实呈现不强断言**、断言「已按原任务定义复算」——验收探针 D5 现象同款口径，依据审查 A02 登记）；②缩短改型 v2s 复算 `EXPECT_TRUE(solutions.empty())`＋状态行「新解 0 个」＋身份重绑定 v2s（解析不可达确定性翻转钉）；③改型后可达目标（v2 零位 TCP）GUI 求解 `EXPECT_GT(rowCount, 0)` 强断言（阶段 F） |
| 断言强度（防回退） | ✅ | dof=6 回写钉使载体回退二连杆（2 dof）立即红；v2s 缩短几何＋空解钉、v2/v2s 身份锚钉、导出锚非零钉均绑定六轴载体。温和改型新解数不强断言**不构成断言弱化**——可行性强断言移至阶段 F 确定性场景，覆盖面净增（整改提交正文同口径自评，验收人独立核实成立） |
| 重建与复跑 | ✅ | `cmake --build build --config Release --target sdurws_ird_kinematics_gui_test` 零错误；全量复跑 gui_test → **14/14 通过**（`SixAxisVariantClosedLoop_KIN_P4` 13 ms 真实执行；P1/P2/P3 既有 12 项零回归） |
| 门禁 | ✅ | `ird_gates` 整改后独立复跑：「全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中」 |
| 文档同步 | ✅ | units/kinematics.md v0.26 行头部嵌勘误段（初版载体选型与代码事实/验收实证不符——v0.24 追加勘误先例同款）；行内「平面臂全展点」遗留句与 §14.6 用例描述更正为六轴载体与三阶段场景 |
| 留痕 | ✅ | traceability/builds/kin-p4/kinematics-gui-test.log/xml 随整改提交重生成（14/14 PASSED，SixAxisVariantClosedLoop 22 ms）——验收人另以自跑复跑独立确认 |
| 提交质量与范围 | ✅ | 标题 `[KIN-P4] 验收整改：端到端用例载体换六轴无旋 TCP 变体`；正文四段齐备；diff 仅 4 文件（gui_test 425 行/units md 4 行/log/xml 留痕）——产品源码零改动 |
| 建议级 A-2/A-3 | ✅ 已消除 | A-2 随 LoopHarness 字段重构消除；A-3「与 gui_test 端到端同一计算面」因 gui_test 亦六轴化而名实相符（KinP4FlowsTest.cpp:73 现读） |

### 8.3 attempt 2 验收者环境
与 attempt 1 同环境（VS2022 17.12 x64、Qt 6.11.1、仓库根同）；复核命令均为本会话真实执行。未重跑 ui_test/kinematics_test 全量（整改未触碰其代码路径——gui_test 全量已覆盖整改面与既有回归）。

### 8.4 attempt 2 裁决
**PASS（零 blocker）**。建议级遗留：A-1（六轴温和改型局部搜索收敛风险——已按「如实呈现不强断言」口径钉扎，风险本体归 IK 收敛性任务/审查 P5 范围评估）与 A-4（openIssues 已登记项：位姿页完整姿态编辑、面板「取消」按钮、批量归档链、named-pose-set 编辑入口在建模侧、sdurws_ird_studio 实机人工交互归所有者）。
