# REUSE-T55（工作包3）阶段〇结论登记：axisAngleRotation 换上游 EAA 适用性独立解析对照——FAIL

登记批次：WP3 登记批（2026-10-27，`redesign-main`）。本文件是计划 v3（`.zcode/workflow-drafts/plan-v3.md` §3 工作包3「适用性待证」块）所设阶段〇（纯分析、零生产改动、无阻断）的结论入册材料。**总体结论＝FAIL，已经所有者确认（2026-10-27）**：`modeling.axisAngleRotation` 换用上游 `rw::math::EAA(axis,angle).toRotation3D()` **不适用**——该行保留本地实现并登记缺口，EAA 路线撤出本包确认复用面。阶段〇期间生产代码零改动。

## 1. 证据口径（如实声明）

- 本登记的**结论**（FAIL、EAA 路线撤出、REUSE-T11/REUSE-T29 均不放行）以所有者确认与计划 v3 为准。**补证提示**：「所有者 2026-10-27 确认 FAIL」这一确认事实在仓库内暂无独立凭证（波次1确认记录/会话原文未入库，本批不代所有者补写）；独验会话须向所有者核对该确认事实本身，核对通过前本文件的结论状态＝已登记待补证，不构成独验依据。
- 波次1运行的阶段〇分析/复核两段报告为工作流运行产物、未落盘至仓库（本登记批以全盘 `rg` 检索确认无落盘副本）；`stage0-report.md` 现内容为八契约独验交接核查报告，非本报告。因此本文件的**消费者清单与解析推导为登记批现场实测重建**——全部锚点于 2026-10-27 登记批逐点亲读（命令与命中见各节），事实底座与计划 v3 :63 证据锚一致；如与波次1原报告存在行号级出入，以本文件现场实测为准并须经独验会话复核。

## 2. 总体结论：FAIL

**判定**：`modeling.axisAngleRotation`（`DhConvert.cpp:228-241`）按上游 EAA 换核**不适用**。失效机理＝上游 `EAA::axis()` 的小角截断使 θ∈(0, 1e-6) rad 全开区间内的换核结果丢失全部非对角一阶项，对 DH/FK 逐元素 1e-9 容差无保证（见 §4 解析推导）。

**放行面裁决（三重负面，均计划 v3 高1 口径）**：

1. **不沿用 REUSE-T11 接受**：REUSE-T11 的接受语义限于附录 D 第 13 项「矩阵→EAA、EAA→矩阵表示转换」的 1e-6 对照（`REQUIREMENTS.md:784`），且 migration §13.2 明文「T11 EAA 上游补丁保持撤销，局部旋转转换 1e-6 不推广到其他功能」（`reuse/migration.md:318`——本批插入前 HEAD 该句在 :316，随本批登记行 +2 行漂移）——该锚不放行本行。
2. **REUSE-T29 适配批准不默认覆盖本包**（计划 v3 §3 工作包3/裁决清单第 2 项）：T29 保持 blocked（`migration.md:285`），其批准面为 runtime 基座世界数学，不含本行。
3. **需求条款本身排除换核**：附录 D 第 13 项语义＝「固定，仅表示转换」（`REQUIREMENTS.md:784`），其边界段明文「该项不改变本表第 4/5 项 FK/编译链/DH 容差」「业务链如仍有实质语义差异必须保留缺口」「不能批量替换所有 1e-12」（`REQUIREMENTS.md:786`）。本行消费面恰是 FK/DH 链（见 §3），1e-6 表示转换语义不覆盖其 1e-9 数值契约——EAA 路线撤出确认复用面。

## 3. 逐消费者判定清单（登记批 `rg -n "axisAngleRotation"` 全量实测）

检索命令：`rg -n "axisAngleRotation" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/`，命中 10 行、去重后消费点如下；产品源码区之外（工具/脚本/冒烟夹具）无命中。

| # | 消费点 | 语义 | 判定 |
| --- | --- | --- | --- |
| 1 | `DhConvert.cpp:841`/`:843`（`DhConvert.cpp:833` 守卫） | DH 双解轴一致性校验核：`z0 != 0` 时对 trial/chosen 两轴构造 `axisAngleRotation(axis, z0)` 后经 `rotationAngleBetween` 比对（容差 `kDhExactAxisAngleToleranceRad`）。**`:833` 的 `if (z0 == 0.0) { continue; }` 为精确零比较——零角守卫仅覆盖 θ=0，不解决 (0,1e-6) 区间**（计划 v3 :63 同款结论，此处为代码实证） | 数值契约面（DH 判定），不可接受 1e-6 级失真 |
| 2 | `DhConvert.cpp:1716` | S5 零位折叠：`folded = rotMul(originModel.R(), axisAngleRotation(src.axis.value(), src.zeroOffset))`——`src.zeroOffset` 为建模显式权威入参，无 (0,1e-6) 排除约束 | FK 载荷面（jd.origin 进 runtime 模型），不可失真 |
| 3 | `DhConvert.cpp:1789`（`zeroConfigFK:1780-1799`） | 零位 FK：`zeroRot = axisAngleRotation(joint.axis, joint.zeroOffset)`，axis 为 S5 规格化单位向量（`:1787-1788` 注释自证与 axisAngleRotation 同规） | FK 载荷面，不可失真 |
| 4 | `CanonicalBridge.cpp:465`（注释 `:431`） | CanonicalBridge 零位折叠：`rotMul(originModel.R(), axisAngleRotation(foldAxis, src.zeroOffset))`，foldAxis 已按 A14 规格化（`:439-463`） | 同构消费面——**援引本结论，不重复验证**（见 §6） |
| 5 | `DhConvertTest.cpp:181-194`（注释 `:179`） | 测试参考实现 `refAxisAngle`：与产品逐元素同式、无共享代码（`:178-180` 注释自证），作一般位姿 FK 对照的组合序参考 | 测试 oracle 同式——随产品行一并保留，不换核 |

**死代码核查**：五个消费点全部在生产/测试主动路径（S5 编译、零位 FK、DH 等价校验、CanonicalBridge 折叠、测试 oracle），无死代码需要排除。**消费面定性**：全部消费点的角度入参均为 DH/S5 链语义量（zeroOffset 或等价量），处于附录 D 第 4/5 项 FK/编译链/DH 容差（1e-9 逐元素）管辖下，第 13 项 1e-6「仅表示转换」语义不适用（`REQUIREMENTS.md:784`/:786）。

## 4. 解析推导（登记批亲读上游实现逐步核实）

产品自持实现（三处同式直写：`DhConvert.cpp:228-241`、`CanonicalBridge.cpp:94-107`、`DhConvertTest.cpp:181-194`）按 Rodrigues 公式 `R = I + sinθ·[k]× + (1−cosθ)·[k]×²` 用**入参轴分量直接展开**（`DhConvert.cpp:237-240`），不经过任何阈值分支——θ∈(0,1e-6) 时一阶项 `sinθ·[k]×` 完整保留。

上游 EAA 路径（`RobWork/RobWork/src/rw/math/`，登记批亲读）：

1. `EAA(axis, angle)` 构造：`_eaa(axis * angle)`（`EAA.hpp:114`）——单位轴下无信息损失。
2. `angle()` ＝ `_eaa.norm2()`（`EAA.hpp:202`）＝ θ。
3. **`axis()`（`EAA.hpp:219-226`）：`if (theta < 1e-6) return Vector3D(0,0,0);`**——θ<1e-6 时返回零轴（计划 v3 :63 证据锚，本批亲读吻合）。
4. `toRotation3D()`（`EAA.cpp:130-151`，矩阵 return :142-150）取 `theta = angle()`、`k = axis()` 后按同一 Rodrigues 展开式构造矩阵。**两者小角口径不一致**：θ∈(0,1e-6) 时 sa=sinθ>0 而 k=0，得 `diag(cosθ, cosθ, cosθ)`——非对角一阶项 `±sinθ·k_{x,y,z}` **全部丢失**。
5. 与正确旋转之差：对角元 |cosθ−1|≈θ²/2（二阶，θ=1e-6 时约 5e-13，可忽略）；**非对角元 ＝ sinθ（一阶主导项）**。数值代入：θ=1e-8 时误差 ≈1e-8，已超 DH/FK 逐元素 1e-9 容差一个数量级；θ 逼近区间上界 1e-6 时误差趋近 1e-6、超容差至三个数量级——**(0,1e-6) 全开区间无容差保证**。
6. θ=0 时 `cos0=1` ⇒ 结果恰为单位阵，正确——即失效区间恰为开区间 (0,1e-6)，产品侧 `DhConvert.cpp:833` 的 `z0 == 0.0` 精确短路（与任何「零角守卫」思路）只覆盖单点 θ=0，**不构成解决手段**（计划 v3 :63/:66 同款结论）。
7. 结论：上游 EAA 的小角语义（1e-6 截断，源于表示转换场景的工程取舍）与产品 DH/FK 链的 1e-9 数值契约**结构性不兼容**，非适配可消除（截断点在上游符号内部，H2/H3 禁止改上游；migration §13.2 :318 亦明文 T11 补丁保持撤销）。

## 5. CanonicalBridge.cpp:94-107 援引口径（工作包4 消费）

`CanonicalBridge.cpp:94-107` 为与 `DhConvert.cpp:228-241` **同式**的本地轴角辅助（`:91-93` 注释自证「与 DhConvert.cpp axisAngleRotation 同式——两侧同规是等价性的前提」；登记批逐行比对两处函数体，公式逐元素一致），消费点为 `:465` 零位折叠。**援引口径**：按计划 v3 :63/:85，WP4 对该辅助的处置**援引本结论、不重复验证**——阶段〇证实 EAA 路线不适用，CanonicalBridge 侧同式辅助同样**不换 EAA、维持同式**，结论随 WP4 实施批在其矩阵行登记；其「冒烟不链 rw 库」失真注释（`:60-63`）的订正按计划归工作包4 处置，不入 WP3 阶段一范围。

## 6. 建议探针（阶段一 verification ④ 落地口径）

计划 v3 :79 verification ④（T42 先例口径）在阶段〇 FAIL 后的定论：

- **换核面 8 处（适用）**：①multiply 同序位模式恒等探针——`Rotation3D::operator*(const Rotation3D&)`（`Rotation3D.hpp:317`，实现 `Rotation3D.cpp:112-155`）的逐元素三项求和序（如 `a20*b00+a21*b10+a22*b20`，`Rotation3D.cpp:148-152`）与产品 `rotMul` 内层 k 递增循环（`DhConvert.cpp:185-191`）同序，IEEE754 位模式应逐位恒等（T42 位模式 12 用例先例）；`operator*(Rotation3D,Vector3D)`（`Rotation3D.hpp:358`→`Rotation3D.cpp:106-108`）同口径。②`isProperRotation(precision)`（`Rotation3D.hpp:291`→`LinearAlgebra.hpp:188-193`，det 检查 `<=precision`）边界与 Sampling.cpp 自写逐列校验（`validateBoxOrientation`，`Sampling.cpp:56-78`，1e-9 阈值——与 §3/§7 函数全段口径统一）对照探针。
- **axisAngleRotation 行（不适用）**：EAA 面探针**撤出**（无换核对象）；该行处置改为缺口登记——EAA 小角事实（θ<1e-6 零轴、(0,1e-6) 失效区间、本报告 §4 推导）落 functions/modeling.json 对应矩阵行，防再评估/防误换（计划 v3 :69「改善」项）。

## 7. 阶段一范围（本包实施批，随本登记批启动）

以计划 v3 §3 工作包3 五问为唯一范围依据：

1. **确认换核 8 处**（登记批行号亲测吻合）——modeling `DhConvert.cpp` 5 个逐元素数学替身核：rotVec `:169-176`、rotMul `:181-193`、transformMul `:196-203`、rotateTranspose `:888-895`、rotateTransposeFrame `:898-910`；kinematics `Sampling.cpp` 3 个自写校验/构造核：validateBoxOrientation `:56-78`、orientBoxPoint `:87-96`、orientSampleRotation `:105-113`。换上游直接 API：`Rotation3D::operator*` 两型（`Rotation3D.hpp:317`/:358）、`Transform3D::operator*`（catalog 在册）、`isProperRotation(precision)`（`Rotation3D.hpp:291`）、RPY 表示转换（`RPY.cpp:74`）。
2. **保留逐项定案 4 行**（计划 v3 中3 三段式）：`modeling.rotationAngleBetween` 定案＝保留（直接引 `migration.md:270` T32 实测反例，不再重复调查）；`modeling.rotZ`/`modeling.rotX`、`kinematics.poseFromDirectionRoll` 包内逐项评估后定，不预先兜底。**axisAngleRotation 一行不动**（`:228-241` 函数体保持原样，仅矩阵行/注释登记 §2/§4 缺口）。
3. **失真注释订正 2 处**：`DhConvert.cpp:178-180`、`Sampling.cpp:366-367`——「冒烟模式不可链接/不链 rw 库」理由已失效（两模式均实链 `sdurw_math`：`modeling/CMakeLists.txt:388`、`kinematics/CMakeLists.txt:278-279`〔集成〕与 `:317-318`〔冒烟分支〕，登记批亲测；计划 v3 :28 记 modeling :381-388／kinematics :279/:288 为 v2 轮范围口径，kinematics 冒烟实链现测 :317）。`CanonicalBridge.cpp:60-63` 第三处归工作包4。
4. **配套 H6 义务**：catalog MATH-ROT upstream 补 `Rotation3D::operator*` 符号行（catalog.json :529-532 现仅 inverse/isProperRotation，登记批亲测）——**catalog 增量待 T44 独验裁决后按顺序应用并新增提交，不 rebase**（计划 v3 编排3）；矩阵 functions/modeling.json、kinematics.json 同批刷新；T32 反例与 EAA 小角事实落矩阵行。
5. **验证**：按计划 v3 :79 verification ①—⑧（受影响目标构建、相关 gtest 真实执行、探针、断开红例、运行效果验收、独立验收所有者侧执行）。

## 8. 阶段二（requirements.targetEditRotation）

`requirements.targetEditRotation`（`RequirementsPanelWidget.cpp:72-78`，4165 行面板覆盖墙）换核以 **REUSE-T42 独验收口 done 为前置**（计划 v3 §2 顺序约束/工作包3 prerequisites）；届时按 T42 同款位模式恒等探针口径实施，不入阶段一实施批。

## 9. 契约与查重登记

正式编号 REUSE-T55（查重：tasks/ 目录 T50—T54 已占、无 T55；traceability/reuse/ 无同名目录——登记批 `ls` 实测）。**本登记批不建 tasks/ 契约文件**：~~T42/T53 先例为实施批建立 ready 契约（ready 契约经 validate-reuse.ps1:85-87 强制 `reuse/changes/REUSE-T55.json` 凭证同批在位，该文件属实施批授权面）；阶段一实施批建立 `tasks/REUSE-T55.json`（status=ready）~~（2026-10-27 裁决取代：阶段一候选留存未落地，tasks/REUSE-T55.json 按 :258 枚举立为 **blocked**、不建 reuse/changes/REUSE-T55.json（草案留档 candidate-changes-manifest.json）——见 §10）。查重援引照旧：REUSE-T35 系 migration.md:262 历史规划编号占位（无契约文件），与本编号无关。起始 SHA=`220189b2f42ea2a824bf9de3fc23affbd2abfe61`；候选留存批提交文件面＝tasks/REUSE-T55.json＋reuse/migration.md（REUSE-T55 行）＋本目录。

## 10. 阶段一候选登记（2026-10-27 实施批；**候选留存，生产未落地**——2026-10-27 所有者裁决路径 B）

按 §7 范围完成实施与全部验证，起始 SHA `220189b2f42ea2a824bf9de3fc23affbd2abfe61`。**结构性阻断**：REUSE-COVERAGE 全文件解决规则（reuse-contract.ps1:285-290）要求被改产品文件上所有绑定行 resolved，而 DhConvert.cpp 尚余 8 行非本包 pending（MATH-DH 3 行〔WP4 终审〕、LM 4 行〔分类B〕、zeroConfigFK〔前置认定未做〕）、Sampling.cpp 尚余 3 行非本包 pending（黄金锁定采样实现，非本包 MATH-ROT 行）——validate-reuse/validate-docs 实测 REUSE-COVERAGE 红。所有者裁决（2026-10-27）＝**候选留存**（T33 A/D 与 T29 先例同型）：生产两文件与两矩阵已回退至 HEAD 原位（git diff 实测为空；两生产文件 git restore 系裁决特别授权），候选完整 diff 留档 `stage1-candidate.patch`，全部验证证据留档本目录；**解阻路径＝所有者裁决「WP3+WP4 同文件整文件解决批」重排（以 zeroConfigFK 前置认定为前置），并一并裁决 §10.4 bench 回退处置**。本节以下小节为候选验证记录（真实执行，非生产事实）。

### 10.1 换核与定案结果（探针＝`run/probe-swap.log`，源＝`probe-swap-equivalence.cpp`）

- **8 处换核**：modeling rotVec（替身核删除，消费点 transformMul 并入——上游 Transform3D::operator*）、rotMul/transformMul/rotateTranspose/rotateTransposeFrame（Rotation3D::operator* 两型＋自由函数 inverse 精确转置，`Rotation3D.hpp:317/:358/:542-557`、`Rotation3D.cpp:106-108/:112-155`）——探针 P1—P5 各 256 例**位模式逐位恒等、最大差 0**；kinematics validateBoxOrientation（isProperRotation(1e-9)——P6 判定面 561 例含 ±1.5e-9 边界带 300 例扫掠**判定映射完全一致**；三分诊断消息集合经非有限预检＋isOrthonormal 细分保留，**但双重无效输入的类别优先级变化＝P6b 4/4 实测翻转**：基线列序扫描中非有限元位于后位列而前列已违规时「非正交」先抛，新核全元素预检前置使「非有限」恒先抛——异常类型同为 std::invalid_argument，RegionCoverageTest 仅钉类型不受影响）、orientBoxPoint（R·d 部分位模式恒等，**全表达式中心加法结合序语义差异＝P9 实测**：c+((t0+t1)+t2) vs 原 ((c+t0)+t1)+t2，768 例中 243 例 ULP 级位差、maxAbsDiff 4.441e-16、maxUlp 160——附录 D 容差与 RegionCoverageTest 1e-14 之内）、orientSampleRotation（位模式恒等）。
- **rotZ/rotX 定案＝换核**（中3 逐项评估，非预先兜底）：候选 RPY(ψ,0,0)/RPY(0,0,α).toRotation3D()（`RPY.cpp:74-99` 亲读；首角绕 z/末角绕 x）。P7 探针 84 角例（0/±1e-8/±1e-6/±π/6/±π/4/±π/3/±π/2/±2π/3/3π/4/±π/±2π/±2.35619/黄金角族/64 随机）**数值恒等最大差 0**；位模式仅 (2,0) 元 +0→−0 零符号差——±0 同义为产品 canonical 身份面既登记契约（`Sampling.cpp` sampleSetIdentity 注「-0与+0是同义旋转」＋RegionCoverageTest 负零恒等用例 :620-621），FK/DH 判定均为附录 D 容差口径。「上游无同名自由函数」＝无同名符号事实记录（rg 零命中），不构成能力缺失证据。
- **poseFromDirectionRoll 定案**：第一段 Rodrigues 轴角→矩阵与 ±ẑ 双退化分支**保留自持**（EAA 阶段〇 FAIL 撤出后上游无「轴角→矩阵」直接等价候选；退化分支黄金锁定语义无上游对应）；第二段滚转组合换上游 RPY(roll,0,0).toRotation3D()＋Rotation3D::operator*——P8 黄金方向/roll 集（斐波那契 N∈{1,2,8,13,64}＋退化 ±ẑ＋近退化×roll 格心 M∈{1,4,8}＋0＋32 随机）**4186 例数值恒等最大差 0**（33 例 (0,1) 元零符号差）。
- **rotationAngleBetween 定案＝保留**：migration.md:270 T32 反例直接引用（EAA acos 分支边界 4.4e-11/1.6e-10 超 C7 容差 40~160 倍），不再重复调查；函数体不动，仅原 :375-376 失真冒烟注订正。
- **axisAngleRotation 一行不动**（本登记 §2/§4 FAIL）：函数注＋矩阵行双登记缺口（DhConvert.cpp 现 :218-236 函数注，函数体现 :223-236）。

### 10.2 失真注释订正

DhConvert.cpp：原 :178-180（rotMul「冒烟模式不可链接」）、:150 节横幅、:32 文件头纪律句——两建模构建模式实链 `sdurw_math`（modeling/CMakeLists.txt，本批复核）；Sampling.cpp：原 :366-367（「冒烟模式不链 rw 库」）、:407-408、:102——kinematics 两构建模式实链 `sdurw_math`（kinematics/CMakeLists.txt 本批复核；§7.3 口径成立）。CanonicalBridge.cpp:60-63 第三处归工作包4 未触碰。

**登记完整性订正（2026-10-27 复核批）**：初版登记有两处语义差异遗漏，已随探针扩展（P6b/P9）补测并订正至候选注释/manifest/本节——①orientBoxPoint 中心加法结合序（P9，数据见 10.1）；②validateBoxOrientation 双重无效输入的异常类别优先级（P6b，数据见 10.1）。`stage1-candidate.patch` 内对应候选注释已同步订正并经 `git apply --check` 干跑验证可干净应用（该 --check 为只读校验，未触碰生产树）。

### 10.3 验证记录（PASS/SKIP/FAIL 分列）

| 项 | 结果 | 证据 |
| --- | --- | --- |
| 受影响目标构建（modeling/kinematics 库＋四测试目标，VS2022 x64 Release 集成树） | PASS | 零 error（构建日志未全留，错误计数 grep=0） |
| sdurws_ird_modeling_test | PASS 480/480 | run/modeling-test.{log,xml} |
| sdurws_ird_modeling_contract_test | PASS 17/17 | run/modeling-contract.{log,xml} |
| sdurws_ird_kinematics_test | PASS 296＋SKIP 1（既有 GUI 占位 PluginPanelLinks.KinGuiPresentationRegisteredNotExecuted_ACC1，非本批域） | run/kinematics-test.{log,xml} |
| sdurws_ird_kinematics_contract_test | PASS 57/57 | run/kinematics-contract.{log,xml} |
| 断开红例（rotMul→return rb；orientBoxPoint→return p） | 真实红 exit 1（4＋1 FAILED）→恢复原字节（sha256 逐位核对）转绿（24＋1） | production/{modeling,kinematics}-disconnect.{xml,log}＋mutated-*.cpp＋run/disconnect-*-recheck.log |
| 逐行四象限 production 证据（8 行 migrated） | PASS | production/production-*.json（normal/oracle/disconnected/upstreamComparison；upstreamComparison 按 REUSE-T53 同 XML 绑定先例并注明独立探针位置） |
| 运行效果前后测量（wp3-bench 同 harness） | **退化如实登记，待所有者判断**（无既有预算，policy §1.1-4） | run/bench-pre-swap.log / bench-post-swap.log |

### 10.4 运行效果回退数据（policy §1.1-4 强制项，交所有者判断）

同机同面（VS2022 x64 Release /O2 /MD，26 组固定输入，oldcore 组为换核前逐字副本对照组）：

- **path.modeling.dhToExplicit#6r：1796.8 → 2257.6 ns/op（+25.6%；复跑 2050.8/2108.9）**
- **path.kinematics.generateGridPositions#rotated-box：3.966 → 7.138 ms/op（+80%；复跑 7.27/7.58 稳定）**
- **path.kinematics.generateSampleSet#rotated-box：19.34 → 23.74 ms/op（+22.8%；复跑 23.69/24.64）**

机理（实施批分析，供独验复核）：rw::math `Rotation3D::multiply`/`Vector3D multiply` 定义于 Rotation3D.cpp（**外联符号**——本批亲读），替身核原为 TU 内内联且可自动向量化的逐元素循环，换核后每算子经真实函数调用且阻断向量化；RPY 路径（rotZ/rotX/Rz(roll)）每组多 2 次 cos/sin 与零因子乘积。三路径均为采样/展开建立型路径（非逐帧热循环），无既有预算登记。按 policy §1.1-4「无既有预算且出现退化交所有者判断」——实施批不自行裁决是否接受或部分回退；回退边界＝本包一提交整体可回滚。

### 10.5 登记面与遗留（候选口径）

- **生产树零改动**：DhConvert.cpp/Sampling.cpp/functions/modeling.json/functions/kinematics.json 均已回退至 HEAD 原位（两生产文件 git restore 系 2026-10-27 裁决特别授权；两矩阵经 `git show HEAD:` 内容回写，`git diff` 实测为空）；候选完整 diff＝`stage1-candidate.patch`（1116 行，含两生产文件与两矩阵；第三轮凭证重钉后经 git apply --check 干跑通过——该 --check 为只读校验，未触碰生产树），候选凭证草案＝`candidate-changes-manifest.json`（未落地不登记 ird-reuse-change 凭证）。
- catalog.json **零触碰**（编排3：Rotation3D.hpp operator* 符号行增量待 T44 独验裁决后按序另批新增提交）；候选内 rotMul/orientBoxPoint/orientSampleRotation 的 production.upstream 以能力级在册符号 `operator*` 绑定（校验按能力级符号平表；文件级归位随增量批）。
- 阶段二 requirements.targetEditRotation（T42 done 前置）与 runtime 5 行（T29 blocked）不在本候选。
- 独验侧 NOT RUN：独立验收未执行（无送验对象——生产未落地）；verify-task-scope 未执行；commit-lint 未执行（候选留存批提交由收口流程执行，建议标题 `[governance] REUSE-T55 阶段一候选留存：验证完成待整批重排`）。
- 两项待所有者事项：①zeroConfigFK 前置认定（接口/owner/消费者/权限，policy §1.1-2）；②§10.4 bench 回退处置（policy §1.1-4）。
