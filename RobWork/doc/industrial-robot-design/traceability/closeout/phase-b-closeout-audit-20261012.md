# 阶段 B（M4）收口独立审计报告

| 字段 | 值 |
| --- | --- |
| 审计时间 | 2026-10-12 |
| 审计基线 | `redesign-main` @ `38af404e`（主工作树 `git worktree list` 实测） |
| 审计性质 | 对抗式只读审计（除本报告文件外零文件改动；全部结论以审计会话内亲手执行的命令输出与亲读文件为准，不采信任何口头/其他会话声称） |
| 审计范围 | M4 里程碑字面判据（development-task-breakdown.md:132）逐项核验＋在途项核验＋工作流产出核验＋findings 账面核验 |
| 未执行项声明 | 本审计未重新执行集成构建与测试（判据 1 的"全绿"依据＝契约 status 账面＋验收记录 verdict＋git 合入关系实测，构建/测试真实执行证据以各验收记录在案留痕为准） |

---

## 一、M4 判据逐项核验

### 1.1 判据原文与映射

M4 权威定义（`development-task-breakdown.md:132`）：

> **M4 阶段 B 业务链** | WP-13/14/15/20/12(B)＋WP-22 入口任务＋WP-04-T17/T18 全绿（AT-01/02/03/09/20 B 段可验）

单元归属实测（node 扫描各契约 `unit` 字段）：WP-13＝modeling（19 份）、WP-14＝requirements（10 份）、WP-15＝kinematics（13 份）、WP-20＝optimization（10 份）、WP-22＝workflow（12 份）、WP-12＝reporting（RPT 家族 16 份，"(B)" 即 B 级部分 RPT-T14）、WP-04＝project（契约编号映射 PRJ-Tnn≙WP-04-Tnn，依据 units/project.md:1410「PRJ-T15（≙WP-04-T15）」）。

### 1.2 契约 status 全量扫描（node）

命令：node 脚本遍历 `tasks/foundation/*.json`（296 份，0 份解析失败），按 masterWp×status 聚合：

```
WP-B {"done":21,"ready":1}   WP-C {"done":26}   WP-D {"done":40,"ready":1}
WP-E {"done":44}             WP-F {"done":28}   WP-G {"done":24}
WP-H {"done":26}             WP-I {"done":84,"ready":1}
非 done 全清单（仅 3 份）：RT-T15:ready、TK-T12:ready、UI-T78:ready
```

三份 ready 卡均为 DTB v0.62/v0.63（2026-10-12）新立的后继批次卡（「先卡后码，待独立评审放行」），DTB 变更记录明文「M4 判据不变——不新增里程碑判据、不改变 M4 语义」（development-task-breakdown.md:5 版本行）。**故 M4 相关各族账面全 done：WP-13 19/19、WP-14 10/10、WP-15 13/13、WP-20 10/10、WP-22 12/12、RPT 16/16（含 B 级 RPT-T14）。**

注意：WP-20/WP-22 乃至 WP-16~T19 多族的 done 翻牌来自全库对账批 `99dfeb2f`（「53 任务 ready→done（0 甄别保留）」，2026-10-12 03:45），逐份在契约 note 登记实现提交 SHA 与验收记录载体（origin/acc/\<task\>/\<n\> 分支 .md 或 findings.json 等价治理登记）。

### 1.3 逐单元验收记录抽查（每单元至少一份最新 done 契约）

| 单元/判据项 | 抽查契约 | 验收记录位置 | verdict | 结论 |
| --- | --- | --- | --- | --- |
| WP-13 modeling | WP-13-T21 | 主树 `traceability/acceptance/WP-13-T21-20261004.md:6` | **pass**（4.1~4.11 全部通过） | 达成 |
| WP-14 requirements | WP-14-T11 | 主树 `traceability/acceptance/WP-14-T11-20260929.md:11` | **pass** | 达成 |
| WP-15 kinematics | WP-15-T18 | 主树 `traceability/acceptance/WP-15-T18-20260929.md:8` | **pass** | 达成 |
| WP-20 optimization | WP-20-T11 | 分支 `acc/wp-20-t11/1`（**未合入主树**）:: `RobWork/doc/.../acceptance/wp-20-t11-20261009.md` | **pass**（零阻断；2 条建议级） | 达成（记录在分支，见残留 R-1） |
| WP-22 入口（T02/T03 等 12 份） | WP-22-T13 | 分支 `acc/wp-22-t13/1`（**未合入主树**）:: `wp-22-t13-20261010.md`；另有契约 note 登记 findings.json 等价治理登记 | **pass**（1 条建议级 S1） | 达成（记录在分支，见残留 R-1） |
| WP-12(B) reporting | RPT-T14 | 分支 `acc/rpt-t14/1`（**未合入主树**）:: `traceability/acceptance/rpt-t14-20261012.md`（verdict=pass，2026-10-12，送验对象 e22a6d06@wp12-t14） | **pass**（无阻断；建议级 2 项） | 达成（记录在分支且路径非标，见残留 R-1/R-2） |
| WP-04-T17/T18 project | —— | —— | —— | **不达成**（见 1.4） |

补充抽查（收官批置 done 的另三份）：RPT-T15（`acc/rpt-t15/1` :: rpt-t15-20261012.md，pass）、RPT-T16（`acc/rpt-t16/1` :: rpt-t16-20261012.md，pass）、WP-13-T18（`acc/wp-13-t18/1` :: wp-13-t18-20261011.md，pass）、WP-13-T17（`acc/wp-13-t17/1` :: wp-13-t17-20261012.md，pass，0 阻断 1 建议）。

### 1.4 WP-04-T17/T18（外部资源固化服务／另存为与项目包实体）——不达成

逐条证据：

1. **无契约**：`tasks/foundation/` 下 PRJ 契约止于 PRJ-T16（PRJ-T01~T16 实测清单），无 PRJ-T17/T18；全目录 grep `WP-04-T17` 仅 PRJ-T01/PRJ-T16 的前瞻性 note 命中（如 PRJ-T16.json:27「阶段 B 包/固化任务 WP-04-T17/T18 届时按 DOC-T10 契约 O-09 处置条款回填」）。
2. **无实现**：project 公共头目录 `industrialrobot/project/include/sdurws/ird/project/` 实测仅 8 个阶段 A 头（ArchivePort/CommandService/DraftService/PersistenceFormat/ProjectStore/QueryPort/README/StoreTypes/UndoRedo）——固化服务的 `ResourceRefs.hpp` 与包服务的 `PackageService.hpp` 均不存在（units/project.md:162~163、:217 登记其为「阶段 B 落位项」）；全 industrialrobot 源树 grep `SolidifyRequest|ISaveAsService` 无 project 侧实体命中。
3. **阻塞登记仍在**：PRJ-TX-11 复制一致性用例仍 GTEST_SKIP（units/project.md:1239「阻塞登记：另存为/包导入导出实体＝阶段 B WP-04-T17/T18」；`project/test/Tx11Tx13SuiteTest.cpp:281` 用例在位）。
4. **无分支无提交**：`git branch -a --list "*wp04*"` 空；`git log --all --grep="WP-04-T17|WP-04-T18" -i` 零实现提交命中。
5. **前置裁决未决**：O-09（P-PR-5 project→io 边）仍 open——development-task-breakdown.md:703「登记（阻塞 WP-04-T18 编译形态，不阻塞注入式先行设计）」；governance-log.md:179 同口径。io 侧已按注入式设计提供 `io/include/sdurws/ird/io/Package.hpp`，workflow 侧 WP-22-T07/T08 编排已 done——**功能面（另存为/包导出导入）经 workflow＋io 注入形态已有载体，但 M4 字面点名的 WP-04-T17/T18 两任务本体未立卡、未实现、未验收。**

**小结：M4 七个判据项中六个达成（其一 WP-12(B) 实现已合入基线），WP-04-T17/T18 一个不达成。M4 不能宣布全绿——且缺口不是任务书预判的 RPT-T14（其已合入，见第二章），而是 WP-04-T17/T18。**

---

## 二、在途项核验：RPT-T14 与 WP-13-T17

### 2.1 实测结论：两项均已合入基线，不再是"在途"

| 检查 | 命令与输出 | 结论 |
| --- | --- | --- |
| RPT-T14 实现合入 | `git merge-base --is-ancestor 2e15e458 redesign-main` → IS ancestor；`git log -1 2e15e458` ＝「[RPT-T14] 实现 B 级真实章节接入（阶段 B）…」 | 实现已合入 |
| RPT-T14 内容等价性 | `git diff --stat e22a6d06 2e15e458 -- .../industrialrobot/reporting/` → **空输出**（送验冻结版 e22a6d06 与合入版 2e15e458 的 reporting 子树逐字节一致；全树差异 21 文件全部为合入基点 cfecffac 侧的 WP-13-T17 产物） | 合入内容＝送验内容 |
| 原实现分支 | `git rev-parse wp12-t14` → fatal（分支已删，合入后清理） | 已清理 |
| WP-13-T17 实现合入 | `git merge-base --is-ancestor cfecffac redesign-main` → IS ancestor；`git log -1 cfecffac` ＝「[WP-13-T17] 实现 WorkCell 反向导入通道（MDL-18/AT-33 R2…）」 | 实现已合入 |
| WP-13-T17 内容等价性 | `git diff --name-only 80bab176 cfecffac -- io/ modeling/` → 0 文件（分支 tip 80bab176 与合入版 cfecffac 产品面一致；全树差 1 文档行） | 合入内容＝分支内容 |
| wp13-t17 分支 | tip `80bab176`（同主题提交），NOT ancestor（rebase 前原身，已被 cfecffac 取代） | 残壳分支，可清理（动作归所有者） |

### 2.2 两个流水线 worktree 只读检查（按任务指定命令）

```
git -C build/pipeline-wt  status --short  → （空，全干净）
git -C build/pipeline-wt  diff --stat     → （空）
分支 pipeline/redesign-main @ ff10aa2d [RPT-T16]：rev-list 领先基线 0 / 落后 9

git -C build/pipeline-wt2 status --short  → （空，全干净）
git -C build/pipeline-wt2 diff --stat     → （空）
分支 pipeline2/redesign-main @ cfecffac [WP-13-T17]：rev-list 领先基线 0 / 落后 13
```

**判断：两 worktree 无任何未提交改动，所在分支零提交领先于基线——"是否接近完成"的答案是已完成并合入；worktree 本体已是滞后现场（落后 9/13 个提交），属可清理的遗留资产（是否清理由所有者决定，本审计未动）。**

### 2.3 收官治理批的账面动作已发生

- `dad5b9e3`「RPT-T14/T15/T16＋WP-13-T17 合入收尾：四契约置 done（验收记录实名登记）」——RPT-T14.json note 末尾实测：「合入收尾（2026-10-12，收官治理批）：实现合入 2e15e458，验收记录在 origin/acc/rpt-t14/1 分支 :: traceability/acceptance/rpt-t14-20261012.md（governance 提交 260fc348，verdict pass）——ready→done。」WP-13-T17.json note 同款（实现合入 cfecffac）。
- 任务书前提「RPT-T14 合入前不能宣布全绿」已被事实超越：**合入已完成**。

### 2.4 残留（合入收尾未闭合面）

- **R-1 验收记录未合入主树**：`acc/rpt-t14/1`、`acc/rpt-t15/1`、`acc/rpt-t16/1`、`acc/wp-13-t17/1`、`acc/wp-13-t18/1`（及 wp-20 全族、wp-22 全族等共 122 个 acc 分支，`git branch -a --list "acc/*" | wc -l`＝122）经 `git merge-base --is-ancestor` 实测**均非 redesign-main 祖先**——主树 `traceability/acceptance/` 目录 ls 实测无 rpt-t14/rpt-t15/rpt-t16/wp-13-t17/wp-13-t18 记录文件。账面 done 与主树文档面不同步。
- **R-2 rpt-t14 记录路径非标**：该分支上记录落在**仓库根** `traceability/acceptance/rpt-t14-20261012.md`（`git ls-tree -r acc/rpt-t14/1 | grep -i rpt-t14` 仅命中契约 json＋根级记录两处），而全库惯例与其余各批记录均在 `RobWork/doc/industrial-robot-design/traceability/acceptance/`（rpt-t15/rpt-t16/wp-13-t17/wp-13-t18 均为标准路径，diff 实测）。契约 note 引用的即是该根级路径——登记口径与实际落盘一致，但与库内路径惯例不一致，合入主树时需归位。

---

## 三、本工作流产出核验（ui-t76/ui-t75 等，全部只读 git 命令实测）

| 项 | 命令与实测 | 结论 |
| --- | --- | --- |
| ui-t76 已被基线吸收 | `ui-t76` tip＝`2e15e458`；`git merge-base ui-t76 redesign-main`＝`2e15e458`＝自身；`rev-list redesign-main..ui-t76`＝0（零独占提交） | **达成**（merge-base＝自身成立） |
| ui-t76 指针快进至基线 | tip `2e15e458` ≠ 基线 tip `38af404e`（`rev-list ui-t76..redesign-main`＝12，redesign-main 非 ui-t76 祖先） | **字面不成立**——指针停在基线历史内的 2e15e458，未指向基线顶端；就"分支无未合入内容"而言等效，就"快进至基线"四字而言不真 |
| ui-t75 分支已删 | `git branch -a --list "*ui-t75*"` → 空 | **达成** |
| ui-t75 修复为基线祖先 | `git merge-base --is-ancestor 7cc052a1d redesign-main` → IS ancestor；`git log -1 7cc052a1d`＝「[UI-T75] F-552 修复：打开协议入口统一会话状态守卫——已打开会话新建项目静默退出消除」 | **达成** |
| wp10-t05 分支 | 存在（本地＋origin），tip `0b859275`「[WP-10-T05] 补登 UI-T78 独立冒烟复跑留痕（全树零错误＋冒烟三套件全绿）」，父 `8b49e277`「[WP-10-T05] 实现 UI-T78 三维模型几何渲染＋ray-cast 拾取对接（阶段 B 三维交互批次 ui 侧）」；领先基线 3 提交，**未合入** | 实现已在分支待验收 |
| 几何挂接分支 | `rt-t15` 存在（本地＋origin），tip `57a8b26a`「[WP-06-T15] 实现 S6 基本图元几何挂接（圆柱连杆＋标记盒＋mesh 通道接线预留）」；领先基线 1 提交，**未合入** | 实现已在分支待验收 |
| ui-t77 | 存在（`ui-t77`、`acc/ui-t77/1` 本地＋远程）；合并提交 `aa9f7aa3`（「Merge ui-t77@d5e73262：三维预览专项合入…」）`merge-base --is-ancestor` → IS ancestor，与 DTB v0.62「ui-t77 merge aa9f7aa3 在线」一致 | **存在且已合入** |

---

## 四、findings 账面：F-550~F-553（node 读 traceability/findings.json 实测）

| ID | status | severity | owner（原文摘录） | 所有者复验要点 |
| --- | --- | --- | --- | --- |
| F-550 | **open** | major | 阶段 B 三维交互批次（WP-10-T05——模型几何渲染＋ray-cast 拾取通道）＋①草稿三维预览通道（需求语义候选，所有者裁定）＋②工位标记可见性复核（UI-T33 链）；note 载 2026-10-08 UI-T77 发布后模型骨架呈现修复落地 | 实拍复验三维通道（草稿预览语义待裁定＋工位标记渲染链复核） |
| F-551 | **open** | minor | 需求域/UI 呈现批次（apply 载荷补名称或树模型名称解析覆盖需求域对象，与 UX-02 口径兼容） | 需求域对象树显示名流转修复后排期 |
| F-552 | **open** | **blocking** | 宿主装配批次（调试器复现定位三类嫌疑；修复后所有者复验；F-553 随修）；note 载 2026-10-08 dev log 定性钉扎（openProject 仅可 NoProject 态调用）＋UI-T75 守卫修复已合入 | **关账前必须所有者复验**（修复已在基线，账面未关） |
| F-553 | **open** | minor | ui 呈现批次（disableReason→tooltip 传递链排查补齐；与 F-552 联动厘清禁用来源） | 与 F-552 复验联动 |

四条全部 open，账面与"所有者复验清单"口径一致。

---

## 五、结论

### 5.1 M4 字面判据达成度

| 判据项 | 结论 |
| --- | --- |
| WP-13（modeling 19 份） | **达成**（done 19/19＋抽查 T21 验收 pass） |
| WP-14（requirements 10 份） | **达成**（done 10/10＋抽查 T11 验收 pass） |
| WP-15（kinematics 13 份） | **达成**（done 13/13＋抽查 T18 验收 pass） |
| WP-20（optimization 10 份） | **达成**（done 10/10＋抽查 T11 验收 pass，记录在 acc 分支） |
| WP-12(B)（RPT-T14） | **达成**（契约 done；实现 2e15e458 已合入且与送验版 reporting 子树逐字节一致；验收 pass 在 acc/rpt-t14/1） |
| WP-22 入口任务（T02/T03，全族 12 份亦全 done） | **达成**（抽查 T13 验收 pass） |
| **WP-04-T17/T18（project 固化/另存为包实体）** | **不达成**——无契约、无实现、TX-11 仍 skip、O-09 仍 open |

**总判定：M4 不能宣布全绿。** 任务书预判的"最后字面缺口 RPT-T14"已在审计时点前合入闭合（实现于基线、验收 pass 在册）；真正的字面缺口是任务书未预判的 **WP-04-T17/T18**（结构性缺口：未立卡未实现，前置裁决 O-09 未决）。另达成项普遍带"验收记录未合入主树"的文档面残留（R-1）与 rpt-t14 记录路径非标（R-2）。

### 5.2 剩余所有者动作清单（按优先级）

1. **WP-04-T17/T18 处置**（M4 语义收口的唯一硬缺口）：推进 O-09 裁决（补登 project→io 边 vs 确认 io 注入形态——DTB:703），随后立卡（PRJ-T17/T18）或按所有者裁决重界定 M4 该项语义；处置前 M4 收口声明须如实排除此项。
2. **F-552（blocking）所有者复验关账**：UI-T75 守卫修复（7cc052a1d）已在基线，按 findings owner 口径须所有者实拍复验后翻账。
3. **F-550 复验**：UI-T77 修复已合入（aa9f7aa3），WP-10-T05 三维交互批次实现已在 wp10-t05 分支（0b859275，未合入）——复验与验收排期，并裁定草稿三维预览需求语义候选＋工位标记可见性（UI-T33 链）。
4. **WP-10-T05 独立验收**：wp10-t05 分支含实现＋冒烟复跑留痕，UI-T78 契约 ready 待放行；同族 rt-t15 分支（RT-T15，57a8b26a）待放行验收。
5. **验收记录合入主树（R-1/R-2 残留）**：将 M4 各族 acc 分支上的验收记录合入 `RobWork/doc/industrial-robot-design/traceability/acceptance/`，rpt-t14 记录合入时归位至标准路径；与 done 账面对齐。
6. 遗留现场清理（非阻塞，决策归所有者）：pipeline-wt/pipeline-wt2 两 worktree（全干净、0 领先、落后 9/13 提交）、wp13-t17 残壳分支（80bab176）。

---

*审计方法声明：本报告全部证据产生于本审计会话内的只读操作——node 扫描（契约 296 份、findings.json）、grep/ls 文件实读、git 只读命令（merge-base --is-ancestor / rev-list / diff / show / ls-tree / worktree list / branch -a / log）。除创建本报告文件及其所在目录外，未修改任何文件。无法在本会话内核验的事项（如各验收记录中构建/测试执行的真实性）以记录在案留痕为准，本审计未重复执行构建。*
