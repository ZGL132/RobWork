# KIN-P3 独立验收记录

- 验收对象：工作包 KIN-P3 指标与会话操作（审查 20261016 A03/A05/A09 修复批）
- 分支 / 提交：`kin-p3`——对象 `7b301477287cac7dda57620eedbc82819a8fd393`（local = origin/kin-p3，推送已核实）
- 验收人：P3 验收者（对抗式独立验收会话，与实施者零共享上下文；本记录为唯一写入库文件）
- 验收日期：2026-10-16
- **结论：PASS（零 blocker）**

---

## 1. 结论摘要

A03/A05/A09 三项的**代码级根因修复经读码定位与独立对抗探针验证均成立**（§2–§4）；回归测试真实存在、断言有强度、名带需求/AT 追溯，修复前红证真实可查（§5）；红线与注释规范合规、门禁独立复跑零命中（§6）；留痕、文档同步、提交四段式均属实（§7）。零 blocker；3 项 advisory 不阻断（§8）。

## 2. A03 合法无穷显示——根因层核对 ✓

**根因定位（读码核实）**：修复前 `formatKinQuantityText` 对一切非有限值抛 `invalid_argument`（red-run-record.md 实录异常文本"非有限 SI 值不可渲染"），把域契约内的合法 +∞（Fk.hpp:108-112 奇异条件数 +∞＋conditionNumberIsFinite 标记、KinTypes.hpp:272-273 全无界裕量 +∞）也拒之门外——奇异状态完全无法显示，与审查 A03 反例一致。

**修复核实**（均为本验收人读码确认）：
- `KinPanelUnits.cpp:53-59`：+∞（isinf>0）→ 返回行级 infinityText 语义词；NaN/−∞ → 保留 `invalid_argument` fail-fast（拒绝面不松动）。三分类齐备：合法无穷（语义词）/未知不适用（既有文本行"未评价"KinPanelModel.cpp:41-47、"零样本——比率不存在" :237-242 注明原因）/非法 NaN（fail-fast）。
- 行级语义词落点：条件数行"奇异/∞"（KinPanelModel.cpp:80-81）、最小裕量"无界"（:84-85）、逐关节裕量"无界"（:96-100）、解表列（:156-161）、检查器行（:204-207, :217-220）——审查任务书"关节裕量的无界 +∞ 一并覆盖"逐点落实。
- `reprojectMetricRows` 幂等保词（KinPanelUnits.cpp:108-112）：+∞ 行语义词与制式无关原样保留——L-K8 单位切换不退化。
- **禁止硬截断核实**：siValue 保持 +∞ 原值（测试断言 `std::isinf(cond->siValue) && cond->siValue > 0`），displayText 为语义词非有限数字。

## 3. A05 混合链逐轴单位——根因层核对 ✓

**根因定位**：修复前显示投影接口只有 double 数组，widget 对全部轴统一 `projectAngle`（git diff 实录删除行 `- ? m_session.displayUnits->projectAngle(q[i])`）——移动轴 0.1 m 被误换算 5.72958（审查 A05 反例数值）。

**修复核实**：
- `projectJointAxisToDisplay`（KinPanelUnits.cpp:118-141）：逐轴天然量纲唯一判定点——Prismatic→projectLength、Revolute/Continuous→projectAngle、Fixed→`invalid_argument`（不做量纲猜测）；非有限分量 fail-fast。
- `reprojectSolutionColumns`（:161-191）与 `tcpBackfillText`（KinPanelFlows.cpp:362-378）签名增逐轴类型表参数；维度失配/Fixed 入表/非有限分量三装配错误轨 fail-fast——审查报告指认的"接口缺逐轴类型"根因面被正面修复（接口本身改造，非调用侧补丁）。
- `dofJointTypes(model)`（KinPanelModel.cpp:105-120）链序滤 Fixed 提取，与求解器/FK 的 dof 口径一致。
- widget 全部调用点接线：关节表（KinematicsPanelWidget.cpp:395-412，越界防御占位不虚构）、解表 refreshResultsPane（:433-436）、检查器联动（:650-653）、双击回写（:676-679）；模型视图缺失＝如实清空（:426-431 防御面）。

## 4. A09 复位 Home 具名来源——根因层核对 ✓

**根因定位**：修复前 `onResetHomeClicked` 读当前 q 原样 reset（git diff 实录删除行 `- homeQ = m_services.sessionPose->jointConfiguration();`）——无目标来源的假成功，与审查 A09 反例一致。

**修复核实**：
- `KinHomePoseProvider` 缝（KinPanelTypes.hpp:282-304）：三态语义（有值/nullopt 未定义/缝空未装配）契约注释完整，指向 MDL-17/V-27 命名位姿集保留键 homeConfiguration；`KinPanelServices.homePose`（:342）。
- `resetSessionHomeFromNamedSource`（KinPanelFlows.cpp:320-360）：四拒绝态编排（会话缝缺/来源缝缺/未定义 nullopt/维度≠dof），全部显式提示零写回；通过态＝`resetSessionHome` 唯一写点（:308-318）。
- 按钮缝门控（KinematicsPanelWidget.cpp:310-315）：缝缺省＝显式禁用（"绝不出现以当前姿态充当 Home 的可达路径"）；只读会话不禁用（reset-session-pose＝readOnlyAllowed——gui 用例对照验证）。
- **PA-2/KIN-06 零修订核实**：本流无任何命令提交通道（结构面）；测试以命令出口计数断言钉扎（PluginPanelTest `EXPECT_EQ(h.commandSubmitCount, 0)`、gui_test `EXPECT_EQ(submit.count, 0)`）——Home 只写会话姿态。

## 5. 回归测试与红证核对 ✓

| 项 | 核对结果 |
| --- | --- |
| 用例存在性 | PluginPanelTest.cpp 6 项（:1134/:1167/:1199/:1287/:1327/:1374）＋gui_test 新文件 KinematicsPanelSessionGuiTest.cpp 5 项（:174/:205/:237/:264/:286），共 11 项，均在本验收真实复跑通过 |
| 追溯名 | 11 项全部 `*_KIN_P3` 后缀＋`IRD_TEST_INFO` 需求/AT 字段（KIN-01/KIN-12/KIN-06＋AT-27/AT-04） |
| 断言强度 | 非恒真：精确文本钉扎（"奇异/∞"/"无界"/"5.72958, 100"/"100 mm"）、`EXPECT_THROW` 异常断言（NaN/−∞/维度失配/Fixed）、状态断言（isinf、会话姿态向量全等、命令出口计数 0、按钮 isEnabled）；无恒真比较、无断言删除 |
| 既有用例适配 | LK1/LK5 随新签名适配（diff 核实：仅增 dofJointTypes/allRev 实参，断言零删除零弱化）；本验收定向复跑 LK1+LK5 → 2/2 PASSED |
| 红证真实性 | red-run-record.md 含修复前红段实录（RED_A03×2 抛 invalid_argument、A05 "5.72958, 5.72958"、A09 谎报"已复位 Home"），与审查反例数值一致；red-model-test.json/red-gui-test.json 留痕在库 |

## 6. 红线与注释规范抽查 ✓

- **对抗探针（独立打破尝试）**：本验收人在 build/kin-p3-accept-probe/（不入库）自建探针 accept_probe.cpp，用**与实施者用例不同的数据**共 44 项检查，全部通过、修复未被打破：
  - A03：cm/deg↔SI↔cm 三跳重投影链 +∞ 语义词幂等（实施者仅测单次 mm）；1e300 有限大条件数直显不被语义词吞；NaN/−∞ 在裕量行（非条件数行）fail-fast；同解混合（有限条件数 3.5＋无穷裕量）正交组合。
  - A05：cm/rad、mm/rad 非实施者制式组合（"0.1, 10"/"0.1, 100"）；全移动链（"500, 250"）；负值移动轴（"-100"）；三轴转-移-续混合链；2v3 维度失配；+∞ 分量 fail-fast；真实 revPrismModel 经 FkEvaluator 全链路＋cm 重投影零异常。
  - A09：Home==当前值成功写回（具名语义）、NaN 来源写入口拒绝且会话态零污染、五维 vs 六轴拒绝、modelView 缺失拒绝、全负 Home 写回。
- **红线**：改动文件 include 全清单核实——仅同单元公共头＋L2 core/runtime 公共头（`sdurws/ird/core/*`、`sdurws/ird/runtime/*`），零业务域互链；KinPanelUnits/Model/Flows/Types 四文件零 Qt include；提交未触碰 industrialrobot/ 与 doc/ 之外任何文件（框架零修改）；audit/ 未入库。
- **门禁**：`cmake -DIRD_ROOT=... -P cmake/ird_gates.cmake` 本验收人独立直跑 → "全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中"。
- **注释规范**：新增/改动代码中文注释齐备（Why 导向＋审查条目/需求 ID 追溯＋物理单位/线程约束/错误语义标注——抽查 KinPanelUnits.cpp、KinPanelTypes.hpp KinHomePoseProvider 契约注释、KinPanelFlows.cpp 四拒绝态注释，符合 AGENTS.md §2）。

## 7. 留痕 / 文档同步 / 提交核对 ✓

- 留痕 traceability/builds/kin-p3/ 23 件全部入库；抽查数字一致：kinematics-test.xml `tests="245" failures="0"`、gui `12/0`、contract `57/0`、ird-gates.log、red-run-record.md、smoke-* 全在。
- validate-docs 独立执行 → PASS（20 units, 12 trace entries, 355 task files）。
- 文档同步：units/kinematics.md 版本头行 v0.25（kinematics.md:7，A03/A05/A09 逐条实现登记＋11 测试项＋红证＋验证数字）＋§14.6 v0.25 详情行；v0.24 行完整保留。
- 提交 `7b301477` 四段式齐备（What 按文件/Why 对审查条目与需求 ID/How 真实构建测试数字/影响面含私有头签名变更边界与 KIN-P4 遗留登记）；local=origin 同值。

## 8. Advisory（不阻断）

- **A-1 域外无穷呈现松动**：`formatKinQuantityText`（KinPanelUnits.cpp:53-55）对一切 +∞ 返回语义词，不区分该量域契约是否允许无穷——探针实测位置行 siValue=+∞（域契约外的上游深度违约）静默显示"∞"而非 fail-fast（修复前此处抛异常）。未截断未谎报、仅在上游双重违约时可达，不阻断；建议 KIN-P4 接线 FK 刷新时一并评估是否按行收紧合法无穷白名单。
- **A-2 NaN Home 异常轨**：来源缝返回含 NaN 的 Home 时，维度检查通过、写入口（Commands.cpp:126-130 fail-fast）异常穿越编排函数直达 Qt 槽，与维度违约的显式提示轨双轨不一致（探针实测异常传播、会话态零污染）。正常装配下 Home 来自命名位姿集解析不会含 NaN（装配错误轨 fail-fast 符合单元错误语义），不阻断；正式宿主装配（KIN-P4）落地时建议在编排层补非有限检查转提示轨。
- **A-3 GUI 用例点击路径**：gui_test 经 `QMetaObject::invokeMethod` 直触槽函数（与审查探针同形态），绕过 disabled 按钮的 click() 拦截；按钮可达性已由 `EXPECT_TRUE/FALSE(button->isEnabled())` 单独覆盖，组合证据完整，不阻断。

## 9. 验收人真实执行的命令清单

```
git branch --show-current / git log --oneline -6 / git rev-parse HEAD / git log origin/kin-p3 -1
git show --stat 7b301477 / git log -1 --format=%B 7b301477 / git show 7b301477 -- <各文件>（diff 核对）
build/kin-p3-accept-probe/：accept_probe.cpp（自建）＋cl @probe.rsp 编译＋运行 → ALL PROBE CHECKS PASSED（44 项检查）
sdurws_ird_kinematics_test.exe --gtest_filter=*KIN_P3* → 6/6 PASSED
sdurws_ird_kinematics_gui_test.exe --gtest_filter=*KIN_P3* → 5/5 PASSED
sdurws_ird_kinematics_test.exe --gtest_filter=PluginPanelLinks.LK1_...:PluginPanelLinks.LK5_... → 2/2 PASSED
cmake -DIRD_ROOT=<industrialrobot> -P cmake/ird_gates.cmake → 全部检查通过零命中
pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1 → PASS (20 units, 12 trace entries, 355 task files)
未重跑全量构建与四套件全量（编排脚本已执行且留痕数字经抽查一致——验收要求 4 口径）
```
