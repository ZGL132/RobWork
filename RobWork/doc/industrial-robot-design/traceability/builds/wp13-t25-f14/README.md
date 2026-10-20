# WP-13-T25 F14 批证据——耦合矩阵编辑入口＋正式宿主 R2 装配

任务：WP-13-T25 第 4 项（F14／审计 M31／MDL-21/AT-38）。
日期：2026-10-20。执行：ZCode 旁支会话（独立 worktree C:/zcode-wt/wp13-t25）。

## 两问题与闭合

| 审计 M31 问题 | 闭合面 |
| --- | --- |
| ① 无耦合矩阵编辑入口（域原语 applyDrivetrainCouplingEdit 随 T18 落位但零 UI 载体） | 传动页耦合区（摘要＋编辑/移除钮）＋模态编辑器（窗口/阶数/n×n 表格、Δq_joint=C·Δθ_motor 约定明示、逐格有限性校验、拒绝保留模态原位重试、取消零写入、移除两阶段允许）；能力位注入链 面板/模块/门面 setCouplingStage（缺省 R1Locked＝公共默认红线——构建期即门控防初值失实） |
| ② 正式宿主 HandlerServices 未提供尾部 couplingStage，落到默认 R1Locked（有效矩阵被解码门拒 MDL-21-COUPLING-STAGE-LOCKED） | UiPlugin.cpp:5312 改传 R2Enabled（MDL-21 已随 2026-10-10 GOV-UNFROZEN 解冻；单一决策点同拍注入面板能力位）；公共默认 R1Locked 保留；mimic/planar/floating/闭环维持阻断（M-6） |

## 顺带修复的域 bug（本批新用例实测暴露）

applyDrivetrainCouplingEdit 原序"先 ensureDrivetrainObject 后数值校验"——数值拒绝（奇异/病态/窗口违例）残留已创建传动对象与根 drivetrainRef，违"拒绝时工作集字节不变"。修复＝校验先行（通过后 ensure+提交，无拒绝路径）。红证：red-atomicity-residue.{xml,log}（修复前 GUI 原子性用例失败）。

## 红证

| 文件 | 内容 |
| --- | --- |
| red-entry-absence-grep.log | 基线 09faf7f0（F14 前）：`git grep ird_modeling_coupling_edit` 命中 0（入口缺失）＋`git show ...UiPlugin.cpp` 第 5312 行传 R1Locked（宿主锁死）——两问题在实现前状态成立 |
| red-atomicity-residue.{xml,log} | 修复前原语：奇异拒绝后工作集残留（drivetrainObject 创建＋design 变更） |

## 绿证

| 文件 | 结果 |
| --- | --- |
| green-standalone-modeling.{xml,log} | 344 通过/0 失败/0 跳过（含 PartsTest 既有 T18 用例回归——顺序修正不破坏 R1 锁/R2 接受/清除/权威中立） |
| green-standalone-modeling-contract.{xml,log} | 8/0/0 |
| green-standalone-modeling-gui.{xml,log} | 51/0/0（含新增 3 例） |
| green-integration-modeling.{xml,log} | 421/0/0 |
| green-integration-modeling-contract.{xml,log} | 17/0/0 |
| green-integration-modeling-gui.{xml,log} | 65/0/0（含新增 3 例） |
| green-ui.{xml,log} | 326/0/0（python313 PATH 前置） |
| green-ui-gui.{xml,log} | 91/0/0 |

ird_gates 零命中（R-1..SA02，exit 0）。

## 新增 GUI 用例断言面

1. CouplingEditorEntryAndStageGating：入口/摘要/移除钮存在；缺省 R1 编辑钮禁用（公共默认红线）；R2 注入放行；只读再禁用；回 R1 再禁用（能力位×可写性正交）。
2. CouplingEditorNonDiagonalAcceptThenRemove：窗口 J4..J6＋3 阶；交接固定探针 C=[[1,0.2,0],[0,1,-0.1],[0.3,0,1.5]] 录入→应用→工作集落位（元素行主序逐项/窗口/阶数/申报条件数有限>1/变更记录）；摘要呈现窗口与阶数；探针数值独立手算期望核对（Δq=C·Δθ=[1.4,1.7,4.8] rad、τ_motor=Cᵀ·τ=[3.2,-0.6,6.1] N·m、双侧瞬时功率 20.3 W——虚功守恒 DYN-04）；移除后回未配置。
3. CouplingEditorSingularRejectedAtomicCancelSafe：秩 1 矩阵（全 1）域拒绝——模态保留＋稳定 token "singular"＋原子性回执注明；取消后工作集与进入前逐字段相等（顺序修正后成立）。

## 四层状态（F14 项口径）

- 数据：PASS（域原语接受/拒绝/清除/原子性——GUI 链＋PartsTest 既有回归）。
- Qt 交互：PASS（真实模态链：录入/拒绝原位重试/取消/移除；门控三态）。
- 正式宿主：PASS（R2 装配位＋面板能力位同拍注入——装配点单处决策；R1 公共默认保留，负例＝PartsTest DrivetrainCouplingR1Locked＋面板缺省门控用例）。
- 真实三维：NOT RUN（本项无三维消费面——姿态变化经既有应用/编译链，统一验收口径）。
- 矩阵映射消费（位置恢复/限位映射/RNEA 交叉耦合）：归 drivetrain 既有 MappingR2GoldenTest＋统一验收（本批不复制映射实现——M-12/NFR-MNT-07）。

## 环境口径

同 F06 批（wp13-t25-f06/README.md）；框架 RobWorkSim 编译失败＝基线固有（v0.47 先例＋F06 批 stash 复现）。
