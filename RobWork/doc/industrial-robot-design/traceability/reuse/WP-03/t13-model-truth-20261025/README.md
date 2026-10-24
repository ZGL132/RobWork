# T13 模型真值核验记录（2026-10-25）

按 migration §13.1 后续交接段「先验清 T13 模型真值再放行依赖的普通 IK/RNE」与 [WP-03 交接入口](../../README.md) T13 行的待核项，对 REUSE-T13（offset/基座安装变换）模型真值做定向核验。**本记录是核验，不是 T13 独立验收**；T13 契约 acceptance 的独验条件仍由所有者另开会话执行。核验者＝独立核验会话，只核验不修复。

- 核验现场：`redesign-main`，起始 HEAD `0ca544e493813441ff03bfc701757ee7ae496095`（＝WP-03 README 后续交接段记录的 `0ca544e49`），主工作树 `D:\10_Source_Repos\21_robot\RobWork`。
- 核验对象实施版本：`f378f01c5`（[原记录](../../../REUSE-T13/README.md)「独验状态核对」段登记）；本次用 git 内容摘要证明当前 HEAD 的 7 个产品/测试文件与 `f378f01c5` 完全一致（见 §5），故原记录全部证据的输入仍适用，本次重跑即为原实施字节的复现。

## 1. 范围

核验面＝ask 点名的 offset／基座安装变换模型真值：移动关节 offset 正负、关节轴（含近极与一般轴）、安装旋转（BaseMount 唯一写入）、限位映射、旋转（Revolute/Continuous）回归。suitability.md 其余待归因项（OBJ→Indexed、非 Windows 网格并发、上游 mask 差异）不在模型真值面内，状态保持原样未动（见 §6）。

## 2. 读取面（契约/记录/源码/矩阵）

- 契约：[tasks/REUSE-T13.json](../../../../tasks/REUSE-T13.json)——acceptance 第 1 条（真实 PrismaticJoint 按 m 消费 offset，q_auth=offset+q_rw 及限位映射一致；正负偏置/多轴/安装旋转/旋转回归保持 1e-9）、第 2 条（runtime 唯一构造模型，公开 API/名称/受控视图/任务私有 State 契约不变）。
- 原记录：[REUSE-T13/README.md](../../../REUSE-T13/README.md)（305/0/0 等证据表与独验 NOT RUN 状态）、[suitability.md](../../../REUSE-T13/suitability.md)（makeLookAt 手性/滚转声明、近 ±Z 冻结形态）。
- 源码：`runtime/src/WorkCellCompiler.cpp`
  - 行 450-461 `axisAlignmentRotation`：±Z 分支冻结 I／diag(1,−1,−1)，一般轴经上游 `Transform3D::makeLookAt`（上游定义 `RobWork/RobWork/src/rw/math/Transform3D.hpp:499-512`），R·Z=axis、右手；
  - 行 650-667：`tAligned = origin * R_align`；Prismatic/Revolute 构造后 `setFixedTransform(getTransform(zeroOffset))` 把 q=offset 静态结果烘焙回固定变换，q_rw 自 0 起；
  - 行 678-685：限位 `bounds−zeroOffset` 平移（q_rw=q_auth−offset）；
  - 行 716-726：子连杆尾巴 R_alignᵀ 补偿（R_align·Rz(q)·R_alignᵀ=R_axis(q)）；
  - 行 572-590：BaseMount 唯一写入 T_world_base，基座连杆帧恒等；行 775-791 场景对象挂 WORLD 根不预乘安装。
- 测试：`runtime/test/WorkCellCompilerTest.cpp`——`PrismaticOffsetUnitsAndWorldAxes_REUSE_T13_RT_EQ_1`（9 轴×Ground/Wall×origin 旋转×offset ±0.25×q∈{0,±0.125}，独立解析 oracle `p=T_origin·(axis·(offset+q))`＋限位断言）、`RevoluteAndContinuousOffsetRegression_REUSE_T13_RT_EQ_1`（2 类型×offset {0.25,−0.25,1e-8}×4 轴，Rodrigues oracle）、`JointBoundsOffsetMapping_RT_AD_2`、`FkEquivalence_RT_EQ_1`、`BaseMountReadback_RT_BW_4`、`SceneAndBaseFramesCarryNoInstallComponent_RT_BW_4`。
- 矩阵/凭证：[functions/runtime.json](../../../reuse/functions/runtime.json) RUNTIME-WC-MODEL / RUNTIME-AXIS-ALIGN（decision=adapter、status=migrated 未独验、production 消费上游 getTransform）；[changes/REUSE-T13.json](../../../reuse/changes/REUSE-T13.json) manifest 7 文件。

## 3. 真实重跑（构建＋回归）

| 步骤 | 命令 | 结果 | 原件 |
| --- | --- | --- | --- |
| 构建受影响目标 | `cmake --build build --config Release --target sdurws_ird_runtime_test` | exit 0（含 sdurws_ird_runtime、sdurws_ird_testkit 连带构建） | [build.log](build.log) |
| 回归（原记录同口径） | `sdurws_ird_runtime_test.exe --gtest_filter=-ContractSuite.ChildProcessWorker_RT_ID_1 --gtest_output=xml:…` | **305 passed / 0 failed / 0 skipped**（gtest XML：tests=305 failures=0 errors=0） | [runtime.xml](runtime.xml)、[runtime-console.log](runtime-console.log) |
| per-exe 报告 | TestMainReport 自动落盘 | total=305 passed=305 failed=0 skipped=0 decisive=true | [ird-test-report.json](ird-test-report.json) |
| worker 占位单跑 | `--gtest_filter=ContractSuite.ChildProcessWorker_RT_ID_1` | 1 SKIPPED（占位语义；父用例已在 305 内真实派生并通过）——与原记录口径一致 | runtime-console.log 外本次控制台 |

与原记录「最终集成 runtime 305 / 0 / 0」一致；runtime 产品源码自 `f378f01c5` 零变化（§5），增量口径下不重跑对端 modeling（474 项原结果直接引用）、冒烟（195 项原结果直接引用，本核验未改产品/CMake/依赖）。

T13 关键用例逐例核对（runtime.xml＋ird-test-report.json 均为 passed）：`JointBoundsOffsetMapping_RT_AD_2`、`ContinuousJointExplicitInfiniteBounds_RT_AD_2`、`FkEquivalence_RT_EQ_1`、`BaseMountReadback_RT_BW_4`、`PrismaticOffsetUnitsAndWorldAxes_REUSE_T13_RT_EQ_1`、`RevoluteAndContinuousOffsetRegression_REUSE_T13_RT_EQ_1`。

## 4. 独立 oracle 复算

[t13-oracle.py](t13-oracle.py)（Python 3.13＋numpy，纯手写数学，不 import 产品代码；输出 [oracle-output.txt](oracle-output.txt)）——25/25 PASS：

1. **限位映射**：`[qmin,qmax]−offset` 方向与数值——minimal j1 `[−2.97,2.97]−0.5→[−3.47,2.47]`（＝测试断言值）、Prismatic 夹具 ±0.25 两组、threeJoint j3 `0.7→[−2.2,0.8]`，全部 ≤1e-12。
2. **makeLookAt 语义**（按上游 Transform3D.hpp:499-512 独立重实现）：11 个轴（含测试 9 轴＋2 补充一般轴）R·Z=axis、正交、det=+1，误差 ≤4.44e-16。
3. **滚转无关性＋offset 烘焙旋转面恒等**：`R_align·Rz(offset)·Rz(q_rw)·R_alignᵀ ＝ Rodrigues(axis, offset+q_rw)`，11 轴×30 组随机/边界 q，max_err=4.58e-16——即 WC 内部关节帧滚转选择不影响权威旋转（suitability「一般轴滚转变化不产生物理差异」声明的数学面成立）。
4. **平移面恒等**：`R_align·(0,0,offset+q) ＝ axis·(offset+q)`，max_err=5.55e-17；含非恒等 origin（R_y(π/2)）复合——q_auth=offset+q_rw 的 Prismatic 米制语义成立。
5. **记录数值例**：Z 轴 offset=0.25、q=0.125→0.375 m；倒挂 R_x(π)=diag(1,−1,−1) 且 R²=I（AT-37 二次叠加形态）；Wall R_y(π/2)·ez=(1,0,0)（P-RT-4 冻结矩阵）；倒挂 worldPose 独立复合 `R_x(π)·(0.2,0.3,0.275)+(0,0,2)=(0.2,−0.3,1.725)`。
6. **threeJoint 夹具权威 FK**（§4.3.3 链语义 F(link_i)=F(link_{i−1})·T_parent_joint_i·R_axis(q_auth_i)，q_auth=offset+q_rw）：三构型 {0,0,0}/{0.3,−0.7,1.1}/{−1.2,0.9,2.5} 逐关节原点/轴线与法兰位姿数值存档于 oracle-output.txt，全部正交右手（≤4.44e-16）。该公式与运行时 WC 实际的对齐由本次 305 项中 `FkEquivalence_RT_EQ_1`（1e-9）真实通过闭环。

## 5. 逐文件凭证复核

对 manifest 7 文件：git 内容层 `git show HEAD:<path>` 与 `git show f378f01c5:<path>` 逐文件字节相等，LF 规范化摘要与 manifest sha256 **7/7 一致**（reuse UTF-8/LF 摘要口径，与 WP-03 交接核对同口径；工作树 CRLF checkout 形态不改变 git 内容层一致性）。结论：当前 HEAD 的 runtime 产品/测试源码＝T13 实施版本字节，`f378f01c5..HEAD` 期间 `WorkCellCompiler.cpp`、`CompilerImpl.cpp` 零提交（`git log --oneline f378f01c5..HEAD -- <path>` 空输出）。

## 6. 结论与开放边界

**模型真值核验结论：验清**。offset 正负／关节轴／安装旋转／限位映射／旋转回归五个面均通过真实重跑与独立 oracle 复算，未发现缺陷；`clean=true`。原始证据未变、复现一致。

以下开放边界**保持原状、不构成本次核验的缺陷**（原记录已登记，返工/独验安排归所有者）：

1. **T13 独立验收仍 NOT RUN**：本记录不替代独验、不自签 PASS/closure；独验最小核对清单见[原记录「独验状态核对」段](../../../REUSE-T13/README.md)。
2. **上游原测试 8 通过＋1 失败**（Model3D.constructor mask 期望/默认值差异）仍待归因——不在本核验范围，未复现，不能报上游全绿。
3. **OBJ→Indexed、非 Windows 网格并发**两待归因项状态不变（本核验未涉及网格路径）。
4. **一般轴滚转的消费者核对**：本次 oracle 证明滚转不影响权威轴/FK 物理语义（数学面）；suitability 登记的「依赖借持 Joint Frame 基底的消费者须独验核对」仍开放。
5. **模型真值验清≠放行**：「放行依赖的普通 IK/RNE」的裁决归所有者；WP-04/WP-05 准入条件中的模型真值项已由本记录满足，其余准入条件（契约编制等）不变。

回滚边界：本记录只新增核验文档与证据，不回退任何产品/矩阵/凭证。
