# KIN-P1 验收记录（对抗式独立验收）

| 字段 | 值 |
| --- | --- |
| 工作包 | KIN-P1 坐标与输入准确性（审查报告 audit/kin-current/运动学模块重新审查-20261016.md §3 A01/A07 两优先项） |
| 验收对象 | 分支 `kin-p1`，实施提交 `4ca1228b`（4ca1228b2104c9a5281fad272b780e499d5ff9bb，基点 c1f901f4） |
| 推送状态 | `git ls-remote origin kin-p1` ＝ `4ca1228b2104c9a5281fad272b780e499d5ff9bb`——本地与远程一致 ✅ |
| **verdict** | **pass**（零阻断；5 条建议级留档，见 §6） |
| 验收日期 | 2026-10-16 |
| 验收者环境 | Windows 10.0.26200 x64、Git Bash、VS2022 Community 17.12（VsDevCmd -arch=x64 探针编译用）、Qt 6.11.1 msvc2022_64、vcpkg 经典模式；独立会话，与实施者零共享上下文 |
| 留痕抽查 | traceability/builds/kin-p1/ 23 个文件在库（双模式构建日志＋六套件 gtest XML＋ird-gates.log＋README） |

---

## 1. 根因层核对（读代码定位，不是确认"有改动"）

### 1.1 A01 世界→基座坐标转换 —— 根因确认且已修复

**根因证据链（全部本会话亲手读取）**：

- runtime 权威读取点的值语义：`runtime/src/Snapshot.cpp:776-779`——`RuntimeSnapshot::worldToBase()` 直读 `m_model.world().T_world_base`；`Snapshot.hpp:193` 注释"T_world_base（世界系→基座系；§9.1 唯一存储在 CanonicalModel.world）"，相邻 `baseToWorld()` 行明示"T_base_world＝inverse(T_world_base)"。
- 修复前旧代码（`git show 4ca1228b~1` 实取）：`baseTref = m_snapshot->worldToBase() * worldTref;  // T_base_ref＝T_base_world·T_world_ref`——注释自称 T_base_world，实际值是 T_world_base，复合少取一次逆；且旋转检测对任何非恒等旋转整体拒绝（倒装/侧装安放下一切区域误拒）。
- 修复后：`ui/plugin/KinEvaluationChannel.cpp:172-182`——`tBaseWorld = runtime::invertWorldBase(m_snapshot->worldToBase())`（BaseWorldTransform.hpp:258 权威反解纯函数，§6.3 不新增第二套基座变换规则）后 `baseTref = tBaseWorld * worldTref`（T_base_ref＝T_base_world·T_world_ref，core §4.6 读法正确）。非正交 fail-fast 转译为切片失败（:175-179，不静默、异常不穿越受理面）。
- 盒中心/尺寸投影：`KinEvaluationChannel.cpp:233-242`——中心＝P＋R·c（精确点变换）、尺寸＝|R|·size 逐行（符号置换下＝被置换原边长）；旋转支持面按符号置换阵划定（:200-225），任意角旋转保持显式拒绝（reason"任意角旋转（盒变换后非轴对齐）"），未静默扩权。
- **连带修正核实**：`ui/plugin/UiPlugin.cpp:4086-4097` 着色回投改 `worldTbase * record.position` 直乘；`git show 4ca1228b~1` 旧代码确为 `rw::math::inverse(worldToBase())`——修复前通道投影（镜像错）与回投（反向取逆）两处方向双错恰好抵消、显示碰巧正确，通道修正后若本处不同步修则闭环破断。连带修正必要性与正确性均成立。

### 1.2 A07 目标输入单位换算方向 —— 根因确认且已修复

- 修复前旧代码（`git show 4ca1228b~1` 实取）：`onSolveClicked` 直读显示值后 `x = m_session.displayUnits->projectLength(x)`——projectLength 是 SI→显示**正向**投影，作用于已是显示制的值＝方向反转（mm 输入 0.1→100 m）；标签构造期硬编码"（m）"。
- 修复后：`kinematics/plugin/KinematicsPanelWidget.cpp:509-525`——解析→`std::isfinite` 非有限就地拒绝（:516-520，NFR-COR-03）→`resolveKinDisplayToSi` 逆换算（:523-525）→SI 真值进 target。
- 逆换算唯一换算点：`KinPanelUnits.cpp:143-156`——`displayValue * displayUnits->lengthUnit().siFactor()`；siFactor 契约＝`core/Units.hpp:68`"value_si = value × factor"、注册因子 `core/src/Units.cpp:33-35`（m=1.0/cm=0.01/mm=0.001）——方向正确（mm 0.1→1e-4 m）。
- 标签随制式：`KinematicsPanelWidget.cpp:327-355` `refreshTargetUnitPresentation`——显式 QLabel 成员＋单位符号取当前制式 token＋"基座系"参考系标识；`setDisplayUnits→refreshAll`（:320-325）驱动重建。

## 2. 回归测试核对（存在性/断言强度/追溯）

共 10 个新用例，三处落位，全部带 `IRD_TEST_INFO` 需求/AT 追溯字段：

| 用例 | 位置 | 断言强度评估 |
| --- | --- | --- |
| ProjectPlan_WorldToBaseTranslation_KIN_P1 | ui/test/KinEvaluationChannelTest.cpp:532 | 审查反例原数据（基座 X=1、世界点 0.65→期望 −0.35，容差 1e-12）＋`expectWorldRoundTrip` 世界闭环——与审查探针 HostProbe.inc 反例等价（审查 pre-fix 失败 XML 实测 1.6499≠−0.35），恒真不可能 |
| ProjectPlan_InvertedBasePreset_KIN_P1 | :591 | 期望 (0,0,1.7) 手算核对成立（R_x(π)·((0,0,0.3)−(0,0,2))）＋闭环回 (0,0,0.3)；修复前被旋转检测整体误拒（双面钉扎） |
| ProjectPlan_WallBasePreset_KIN_P1 | :653 | 非立方盒尺寸轴置换（(0.6,0.4,0.2)）＋两体心样本＋两点闭环——尺寸置换与中心变换同时可观测 |
| ProjectPlan_ModelFrameAxisPermutingRotation_KIN_P1 | :724 | 经 resolveFrame 缝注入 R_z(π/2)，精确置换无保守放大 |
| ProjectPlan_ArbitraryRotatedFrameStillRejected_KIN_P1 | :796 | 保持拒绝＋拒绝原因含"旋转"＋先绑快照保证归属精确（非"快照缺位"误拒） |
| TargetLabels_FollowDisplayUnitSystem_KIN_P1 | kinematics/gui_test/KinematicsPanelInputGuiTest.cpp:147 | 精确全串比对"目标 X（mm，基座系）"＋反断言不得残留"（m）"——修复前恒"（m）"必红 |
| TargetSubmit_ConsistentSiAcrossUnitSystems_KIN_P1 | :190 | m/cm/mm 三制式 0.1/10/100 → 提交 SI 均 0.1 m（容差 1e-12）——修复前 mm "100"→100000 m 必红 |
| TargetSubmit_mmInput01IsTenthMillimetre_KIN_P1 | :223 | A07 反例钉扎：mm "0.1"→1e-4 m（容差 1e-15）——修复前 100 m 必红 |
| TargetSubmit_RejectsNonFiniteInput_KIN_P1 | :244 | 求解器零触达（received 无值）＋状态行含"非法"——非有限拒绝面 |
| DisplayToSi_ConsistentSiAcrossUnitSystems_KIN_P1 | kinematics/test/PluginPanelTest.cpp:502 | 三制式一致/A07 反例/与 projectLength 互逆/因子 0.001/0.01/1.0 钉扎 |

测试真实通过（本会话亲手复跑，见 §3）；"修复前必红"由旧代码读取得出（数学上旧行为与新断言矛盾：1.65≠−0.35、100≠1e-4、恒"（m）"≠mm 标签、旋转整体拒绝≠倒装可评估），并与审查探针 pre-fix 失败记录（probes-integrated.xml 实测 1.6499/1.75）互证。

## 3. 本会话真实执行的验证（与实施者自报区分）

| # | 命令 | 结果 |
| --- | --- | --- |
| 1 | `git rev-parse kin-p1`＋`git ls-remote origin kin-p1` | 均为 4ca1228b2104c9a5281fad272b780e499d5ff9bb——已推送 ✅ |
| 2 | ui_test 定向：`sdurws_ird_ui_test.exe --gtest_filter=KinEvaluationChannel.ProjectPlan_*`（PATH 前置 build/RobWork bin＋vcpkg bin＋Qt bin；QT_QPA_PLATFORM=windows） | **5/5 PASSED**（101 ms） |
| 3 | `sdurws_ird_kinematics_gui_test.exe`（全量＝新目标四用例） | **4/4 PASSED**（22 ms） |
| 4 | `sdurws_ird_kinematics_test.exe --gtest_filter=PluginPanelLinks.DisplayToSi_ConsistentSiAcrossUnitSystems_KIN_P1` | **1/1 PASSED** |
| 5 | **对抗探针**（验收人自建全新数据，与实施用例零重叠）：`build/kin-p1-acc-probe/AccProbe.cpp`（VS2022 x64 `cl @acc-probe.rsp` 编译，链接集成树产品库；gitignored 不入库） | **6/6 PASSED**（含同 TU 载入的产品用例共 19 PASSED） |
| 6 | ird_gates 自跑：`cmake -DIRD_ROOT=<industrialrobot 绝对路径> -P cmake/ird_gates.cmake` | 退出码 0，"全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中"，自测 17 项全过 |
| 7 | `git show 4ca1228b --name-only` 过滤 | 提交仅含 industrialrobot/＋doc/ 路径，零 audit/ 等审查材料、零框架源码 ✅ |

### 3.1 对抗探针详情（打破尝试——未破）

全部为本验收人独立手算的**新数据**（非实施用例数值）：

1. `A01_InvertedMount_NewTranslationAndRoundTrip`——倒装 R_x(π)＋基座世界位 (2,−1,0.5)、世界区域中心 (0.4,1.3,−0.2)（三轴非零平移，实施用例未覆盖的组合）：实测基座系 (−1.6,−2.3,0.7)＝手算 R_x(π)·(p_w−t) 逐位一致，闭环直乘回 (0.4,1.3,−0.2)。若仍为修复前形态，此数据下错值 (1.6,−2.3,0.3)（或旋转整体误拒）——具备打破能力。
2. `A01_WallMount_NonCubicFourSampleAxisPermutation`——侧装 R_y(π/2)＋基座 (−0.5,2,1)＋非立方盒 (0.1,0.3,0.5)＋Grid 1×2×2 四样本：投影盒中心 (−1.5,−0.3,0.8)、尺寸置换 (0.5,0.3,0.1) 全部精确（体心规则 extent/(2×count)），并逐样本世界闭环落回原盒内。
3. `A01_ModelFrame_Rx90_AxisPermutingSupported`——模型帧 **R_x(π/2)**（实施用例用 R_z(π/2)，本探针换轴探测定的一般性）＋平移 (0.1,0.2,0.3)：实测 (0.75,−0.1,0.3) 精确。
4. `A07_CmMode_FractionalAndNegativeThreeAxes`——cm 制式 X 2.5/Y −40/Z 0.75 → 提交 (0.025,−0.4,0.0075)（负数＋分数三轴；修复前形态 Y 将呈 −4000）。
5. `A07_MmMode_ThreeAxesFractionalValues`——mm 制式 3.75/0.02/−250 → (0.00375,2e-5,−0.25)。
6. `A07_LabelSwitchBack_FollowsEachSystem`——m→cm/deg→m/rad 来回切换，每拍标签均携带当前制式＋基座系标识（防"切换只生效一次"）。

诚实记录：探针首轮曾在用例 2 报 4 处断言失败——经手算复核为**验收人自己期望值的算术笔误**（y 体心 ±0.075 误写 ±0.0375；体心规则与产品用例 size 0.2/2 格→±0.15 同律一致），修正期望值后 6/6 全绿；期间世界闭环断言即使带笔误期望也全过，进一步佐证产品侧正确。产品代码零改动嫌疑排除。

## 4. 注释规范与架构红线抽查

- **中文注释（AGENTS.md §2）**：A01 修复块（KinEvaluationChannel.cpp:154-199，含值语义根因、复合读法、容差来源、错误语义逐段说明）、resolveKinDisplayToSi（hpp:119-142＋cpp:143-156，含修复前缺陷形态与 core Units 因子语义出处）、refreshTargetUnitPresentation/onSolveClicked（含 KIN-P1 验收半区映射）、探针形态 gui_test 文件头（用例与验收映射表）、BuildGraphContractTest 白名单注释——单位/坐标系/需求 ID（KIN-04/KIN-07/KIN-12/MDL-22/AT-27/NFR-COR-03）齐备，无复述式废注释。
- **L2 零 Qt**：resolveKinDisplayToSi 为纯函数零 Qt；KinEvaluationChannel.cpp 之 Qt 面仅 QMetaObject 回投（ui 单元允许）。
- **R-2 跨单元公共头**：ui→runtime/BaseWorldTransform.hpp、runtime/CanonicalModel.hpp、kinematics/requirements 各 include 均为对方 `include/sdurws/ird/<unit>/` 公共面；ird_gates R-2 自跑零命中互证。
- **SA-02 框架零修改**：提交文件清单无 RobWork/RWS/RobWorkSim 框架源码、patches/ 无新增。
- **PA-2 只增不改**：units/kinematics.md v0.23、units/ui.md v1.103 均为表尾追加版本行；ui.md §13 历史 UI-T65 行"worldToBase 逆变换"的错误方向语义**未删除原行**，由 v1.103 行显式纠正登记——历史登记不可变纪律保持。

## 5. 留痕与文档同步

- traceability/builds/kin-p1/ 实存 23 文件；XML 汇总亲手抽查：ui-test 294/0 失败、kinematics-test 232/0（含 1 既有登记跳过形态）、kinematics-gui-test 4/0、ui-gui-test 84/0、ui-contract 42/0、kinematics-contract 57/0——与提交信息验证段数字一致；ird-gates.log"零命中"与本会话自跑结果一致。
- 提交信息四段式（What/Why/How/影响面）齐备，验证数字与留痕一致、无粉饰。
- 文档同步：kinematics.md §3.1 目标表增 gui_test 行（:217）、§9.8 L-K2 补行（:793，目标输入单位语义全量登记）、§14.6 v0.23；ui.md §16.7 v1.103（A01 根因＋旋转支持面＋连带修正＋五用例＋纠正 UI-T65 行方向语义）。

## 6. 建议级事项（不阻断）

1. **A01 置换判定的列唯一性未显式校验**（KinEvaluationChannel.cpp:201-220 只查每行恰一主导元＋无杂散元）：对正交输入（编译链 S5 校验过的 T_world_base×刚体 worldTref）行条件已蕴含置换阵，产品路径不可达；仅当 resolveFrame 缝返回病态非正交"位姿"时可能漏判——建议后续给 resolveFrame 返回值加正交性防御（现无生产者会这么注入）。
2. **验收行"世界/基座/工具目标转换闭环"的工具半区**：区域参考系 Tool 种类仍走既有显式拒绝面（"仅 World/ModelFrame"——旧代码 4ca1228b~1 同款存在，非本批引入）；实施者 openIssues 已如实声明，未静默扩权，留后续任务。
3. **修复前红复现未由本验收人在旧构建上重跑**（需重建旧树）：以旧代码读取推导＋审查探针 pre-fix 失败 XML（1.6499/1.75 实测值）互证替代；推导无歧义。
4. ird_gates 未产出 base..head 归一化比对附件（实施者 openIssues ③）——门禁口径以主扫描零命中为准，本会话已自跑复核。
5. ui 侧亦可直接消费 `RuntimeSnapshot::baseToWorld()`（Snapshot.cpp:782 已封装同一 invertWorldBase）；现显式调用 `invertWorldBase(worldToBase())` 与之等价且同为 runtime 权威设施——仅记录，无需改动。

## 7. 结论

A01/A07 两项在根因层修复（复合方向取逆、提交面逆换算＋标签随制式），连带修正（着色回投直乘）必要且正确；10 个产品回归测试真实存在、断言强、带追溯；验收人以全新数据构造的 6 项对抗探针未能打破修复；ird_gates 自跑零命中；留痕、文档同步、提交规范齐备。**verdict＝pass，可进入合入段（所有者决策）。**
