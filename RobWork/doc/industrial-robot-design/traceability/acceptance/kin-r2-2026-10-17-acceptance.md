# KIN-R2 呈现批次独立验收记录（kin-r2-ui）

| 字段 | 值 |
| --- | --- |
| 验收对象 | 分支 `kin-r2-ui`，冻结 SHA `7345ca4593c3239449e30b812f60617ed409cd66` |
| 基线 | `3f4a6337`（redesign-main，KIN-P1~P5 已合入；已核实为 HEAD 祖先） |
| 提交范围 | 5bec2389 / 8311b3dc / 7345ca45（三提交，均 `[KIN-R2]` 前缀四段式） |
| 验收模式 | **独立子会话验收**（与实施者零共享上下文，对抗式立场；按 acceptance-protocol.md v1.7 §2.2 精神执行——本批次为编排者下发的 R2 验收指令，记录按指令落本分支而非 acc/ 分支） |
| 验收日期 | 2026-10-17 |
| **verdict** | **PASS**（零阻断项；4 条建议级事项见 §5，不阻塞合入） |

## 1. 验收依据

- findings.json F-902/F-903/F-905 条目原文（traceability/findings.json，HEAD 版）；
- audit/kin-current/运动学模块重新审查-20261016.md §4 位姿页/可视化条目（行 89~91）；
- AGENTS.md §2 注释规范 / §5 架构红线八条；
- units/kinematics.md 本批增量 v0.28/v0.29/v0.30（§14.6 三行＋§9.8 面板表两行＋文档版本行）。

## 2. 逐项根因层核对表（任务书 1~6 项）

| # | 项 | 根因层判定 | 独立证据 |
| --- | --- | --- | --- |
| 1 | F-902 域外无穷白名单 | **根因层成立**：收紧点在格式化唯一判定面 `formatKinQuantityText` 第四参数（`KinPanelUnits.cpp:52-72`），非调用点表象修补；行白名单＝域契约判定的单一事实源，`makeRow`（KinPanelModel.cpp:31-45）缺省 `nullopt`，仅条件数/裕量族显式供给"奇异/∞"/"无界"（KinPanelModel.cpp:119-139/194-200/243-259——全调用点逐一核对无遗漏）；`reprojectMetricRows` 对 +∞ 行保留构建期 displayText（KinPanelUnits.cpp:106-136——标记跨制式稳定） | 自构探针：`formatKinQuantityText(+inf,"rad",nullopt)`→"非法值(+∞)"（回归套件未覆盖的 rad 行新面）；+∞ 与显示制式并存→标记优先；NaN/−∞ 含白名单词仍 throw |
| 2 | F-903 NaN Home | **根因层成立**：拒绝态⑤在编排层前置拦截（`KinPanelFlows.cpp:394-407`，逐分量 `std::isfinite`），异常不再离开编排函数；写点 fail-fast（Commands.cpp:114-127）保留为第二道防线 | 自构探针：`KinSessionPose::resetToHome({0.4,NaN})` 仍 throw、会话态保持 {0.4,0.4}；对抗推演"modelView==nullptr＋空 Home"路径被 `KinPanelFlows.cpp:389` 先导析取 `services.modelView == nullptr ||` 拦截——流面封闭 |
| 3 | 采样模式编辑入口 | **成立**：`KinWorkspaceSamplingParams`/`lastWorkspaceRun`（KinPanelTypes.hpp，零 Qt 值面）；三编排函数（KinPanelFlows.cpp:744-926）——装配→域校验拒绝轨（步骤 5，异常不穿 Qt 槽）→内联运行→会话态唯一写点；widget 三值模式下拉＋互斥启用（KinematicsPanelWidget.cpp:943-982）＋按模式装配（:1039-1131，禁用态字段不取值）；CSV v2 sampling-mode 列经域设施 `workspacePointCloudExportCsv`（:881-902，零自拼行） | gui_test 模式切换互斥 3 态断言、摘要表"模式 joint-random"行、CSV 表头 `sample-index,x-m,y-m,z-m,sampling-mode` 钉扎——定向复跑全绿 |
| 4 | 位姿页完整姿态 | **成立**：双参考系六行＋RPY 三行（`rw::math::RPY` 直调零自写三角）＋旋转矩阵九元（KinPanelModel.cpp:66-111）；目标 RPY 六字段编辑（KinematicsPanelWidget.cpp:764-821——`readTargetPoseFromEditors` 单点、`resolveKinAngleDisplayToSi` 角度量纲独立逆换算、空输入=0 兼容、非有限就地拒绝）；参考系标识行随解析现投（:567-627） | 自构探针：RPY(30°,45°,60°) 组合→分解往返精确（1e-12）、万向锁邻域有限；deg 制式 RPY 行回显 30/45/60 deg；`resolveKinAngleDisplayToSi(90,deg)=π/2`（<1e-15）；gui RPY 往返用例复跑绿 |
| 5 | homePose 门控收敛 | **成立**：KIN-P4 接线（UiPlugin.cpp）本批零改动（不在 diff 内——核实）；门控收敛在 widget（KinematicsPanelWidget.cpp:468-470：缝在场且 `homePose()` 有定义才 enable），无定义态禁用＋直达槽防御面提示保留 | 红证日志：修复前 SourceAbsent 用例断言 `isEnabled()==true` 失败（kin-r2-red-before-fix.log:80-82）；复跑 DefinedSource/SourceAbsent/MissingSource/DimensionMismatch 四态全绿 |
| 6 | 投影视图＋PNG（尽力项） | **成立（最小入口）**：画布数据链全域设施直调（KinematicsPanelWidget.cpp:1159-1257：buildWorkspacePointCloud→projectWorkspacePoints→QPainter，退化值域 ±0.05 m 兜底、五色状态＋同源图例）；PNG 经 `exportWorkspaceProjectionPng` 既有 exportWriter 缝（KinPanelFlows.cpp:928-950，空字节拒绝）；三维 View3D 勘察结论已登记 units/kinematics.md §14.6 v0.30（RWStudioView3D 权威＋扩展成本超本批→后续批次） | gui 用例断言导出字节 `\x89PNG` 魔数＋固定文件名——复跑绿 |

## 3. 真实执行的验证清单（independentEvidence）

1. **origin 冻结核对**：`git ls-remote origin kin-r2-ui` → `7345ca4593c3239449e30b812f60617ed409cd66`＝本地 HEAD，零漂移；`git merge-base --is-ancestor 3f4a6337 HEAD` 确认基线。
2. **范围核对**：`git diff --name-only 3f4a6337..HEAD`＝31 文件——仅 kinematics plugin/gui_test/test＋CMakeLists＋doc/traceability；`git diff 3f4a6337..HEAD -- '*kinematics/src*'` 空输出（**计算内核零语义改动**）；框架源码/patches 零触碰（SA-02 成立）。
3. **红线核对**：非 widget 插件文件（KinPanelUnits/Model/Flows/Types）`#include <Q...>` 零命中（L2 零 Qt）；跨单元私有头零命中（新增 include 仅 kinematics 公共头 Workspace.hpp＋evidence Evaluator.hpp——§3.2 登记边）；ird_gates 留痕（kin-r2-ird-gates.log）自测含预期失败检出＋全扫描 R-1~R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中。
4. **定向复跑（min 用例集，亲手执行）**：
   - `sdurws_ird_kinematics_test.exe --gtest_filter='*F902:*F903:*KIN_R2'` → **7/7 PASSED**；
   - `--gtest_filter='*F905:*NonFiniteDisplay*:...'` → **5/5**（含 A03 三用例不回归）；精确名复跑 MixedChainProjection（A05）/ResetHome_NamedSourceFlowGates（A09）→ **2/2**；
   - `sdurws_ird_kinematics_gui_test.exe`（QT_QPA_PLATFORM=windows）filter Sampling/Input/Session → **13/13**；filter `*SixAxis*:KinHostLoop*` → **2/2**（objectName 化修复后 KIN-P4 闭环无回退）。合计 **29 用例全绿**。
5. **自构对抗探针**（验收者独立编写 AccProbe.cpp，直编 KinPanelUnits.cpp/KinPanelModel.cpp＋链 sdurws_ird_kinematics/core/runtime/rw_math，用后即弃）：域外无穷 rad 行/制式并存/双重重投影幂等/RPY 行制式投影/矩阵元保真/组合分解往返/角度逆换算精确值/写点 NaN 拒绝——**PROBE-PASS（0 mismatch）**。
6. **变异测试（4.5 验证验证者）**：将 `KinPanelUnits.cpp:71` 白名单缺省回退为泛化"∞"（变异 TU 单独编译）→ 探针在且仅在两条域外标记断言上**变红**（`[∞] != [非法值(+∞)]`）——修复具因果性、探针具真实失败能力；还原后复编译 PROBE-PASS。
7. **红证留痕核对**：kin-r2-red-before-fix.log（5 失败，断言行号/文案与现行 PluginPanelTest.cpp:1309-1346/1543-1558、InputGuiTest:500、SessionGuiTest:277 一一对应）；kin-r2-task3-red-before-fix.log（2 失败＝采样控件缺位）。留痕与断言真实对应，非事后编造。
8. **测试 XML 抽查**：kin-r2-final-*.xml 五套件头（266/57/305/42/20，failures=0）与提交留痕一致；构建树 exe 时间戳（01:03-01:04）与 XML（01:04）、HEAD 提交（01:08:26）时序自洽。
9. **四段式与账面**：三提交正文 What/Why/验证/影响面齐备，验证数字（259+1→264+1→265+1 递进、gui 17/19/20）与留痕文件一致；findings.json diff 仅 F-902/F-903（fixed+resolution）与 F-905（resolution）三处；units/kinematics.md 版本链 v0.27→v0.28/v0.29/v0.30 连续。
10. **注释规范抽查（4.7）**：公共/私有头全查（KinPanelUnits.hpp/KinPanelModel.hpp/KinPanelFlows.hpp/KinPanelTypes.hpp/KinematicsPanelWidget.hpp）＋实现文件 ≥3 抽查（Units/Model/Flows/Widget cpp）＋新测试文件（SamplingGuiTest 全文件头）——文件头/Doxygen 中文描述/§2.5 高危信息（单位 m·rad/基座系 {B}·世界系 {W}/角度制式显式/Qt 父子所有权/UI 线程约束/错误语义双轨/确定性词表）齐备；CMake 增列带任务注。偷懒扫描：diff 内 TODO/FIXME/stub 零实命中（"toDouble"/"setPlaceholderText" 为误报词面）。

**未独立复验（如实声明）**：全量六目标集成构建与五套件全量未重跑（编排者指令明示不重跑，验收采信其全绿结论＋本记录第 8 条时序自洽佐证＋定向复跑 29 用例亲手执行）；独立冒烟模式构建未执行（本批无新增独立配置面依赖——CMake 仅 gui_test 增一源文件）。

## 4. openIssues 核实结论（实施者自报 5 条）

| # | 自报内容 | 核实结论 |
| --- | --- | --- |
| 1 | 三维 View3D 接入以勘察结论登记 §14.6 v0.30 后续批次 | **属实**（kinematics.md §14.6 v0.30 行＋文档版本行：RWStudioView3D 权威 O-38/HostView3DGateway 已绑公开 API/扩展成本超本批） |
| 2 | 采样"可取消、有进度"后台通道化留后续批次 | **属实**（KinPanelFlows.hpp KinInlineSamplingContext 类注"诚实边界"＋§14.6 v0.29 行；reportProgress 显式空实现不伪造） |
| 3 | F-905 缓存类消费方逐项核对半区仍 open | **属实**（findings.json F-905 status=open＋resolution 明示"核对半区仍 open，owner 不变"） |
| 4 | 面板不暴露姿态采样编辑（退化单方向单 roll）登记为呈现批次边界 | **属实**（KinPanelTypes.hpp 参数注＋KinPanelFlows.cpp 步骤 1 注＋§14.6 语义一致） |
| 5 | 关节网格逐轴异值编辑留后续批次（面板统一值施加全轴） | **属实**（KinPanelTypes.hpp jointGridPerAxis 注＋装配处 `query.jointGridCounts.assign(dof, ...)` 同口径） |

## 5. 问题清单

**阻断级：无。**

**建议级（不阻塞合入；按指令本次仅登记于本记录，未写入 findings.json——转登由治理面顺延处理）：**

- A-1（账面笔误）units/kinematics.md §14.6 v0.28 行"F-903 ……（拒绝态五态扩六态）"——实际为**四态扩五态**（①~⑤，新增⑤；代码 KinPanelFlows.cpp:358-407 与 commit 5bec2389 均为五态）。建议下批增量修订顺延更正。
- A-2（账面笔误）findings.json F-903 resolution 引用用例名"ResetHome_NonFiniteSourceExplicitHintTrack_**F902**"——实际用例名后缀为 **_F903**（PluginPanelTest.cpp:1527；红/绿留痕日志中的名称与代码一致）。建议随下次 findings 修订更正。
- A-3（过期括注）KinematicsPanelInputGuiTest.cpp SolveButton_CompletesRealSolveThroughAssembler_KIN_P2 用例注仍写"目标只编辑位置、姿态恒单位阵——审查 §4 已知边界"——同批已扩 RPY 编辑，该解释性括注过期（用例行为本身有效：恒等姿态仍是合法输入，断言未受影响）。
- A-4（直写面契约缺口，流面不可达）`KinSessionPose::resetToHome({})` 接受空向量静默置 0 维已设态（Commands.cpp:108-111 allFinite(空)==true；探针实测 threw=0/set=1/size=0）。面板流已被 `KinPanelFlows.cpp:389` 的 `services.modelView == nullptr ||` 先导析取拦截，正常装配不可达；仅测试/Harness 直用写点可达。建议后续给写点补维度/空向量防御或头注声明调用方契约。

## 6. 验收者环境说明

- Windows 10 x64，Git Bash；仓库 D:\10_Source_Repos\21_robot\RobWork，分支 kin-r2-ui @ 7345ca45。
- 定向复跑 PATH 前置：build\RobWork\bin\Release;build\RobWorkStudio\bin\Release;vcpkg\installed\x64-windows\bin;Qt 6.11.1 msvc2022_64\bin；QT_QPA_PLATFORM=windows。
- 对抗探针：VsDevCmd（VS2022 Community 17.12）/std:c++17 /utf-8 独立编译于临时目录 acc-kinr2-probe/（验证后已删除，未触碰产品树与实施者工作树状态）。

## 7. 裁决

**PASS** —— 任务书 6 项逐项根因层核对成立；改动范围/红线/注释规范零违例；29 用例定向复跑全绿＋自构探针 PROBE-PASS＋变异测试证明修复因果性与测试真实失败能力；三提交四段式与留痕、版本链、findings 账面一致。建议级 A-1~A-4 留档，不阻塞合入；合入决策归所有者。
