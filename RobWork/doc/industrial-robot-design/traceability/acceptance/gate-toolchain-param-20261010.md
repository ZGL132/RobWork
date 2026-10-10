# gate-toolchain-param 验收记录（attempt 1）

| 字段 | 值 |
| --- | --- |
| 验收对象 | 任务 `gate-toolchain-param`（F-628 批次：gate-all.ps1 增设 `-Toolchain` 可选参数） |
| 分支 / 冻结 headSha | `gate-toolchain-param` / `db3beaaa1a68da02f155c55403396dc298812695`（commit 范围 `ebf86026..db3beaaa`，2 提交） |
| 验收方式 | **独立子会话对抗式验收（所有者 2026-10-10 指令授权）**——验收者与实施者不共享会话记忆，全部结论以验收者亲手复现产物为准 |
| 验收日期 | 2026-10-10 |
| **verdict** | **pass**（11 项全过，零阻断；建议级 F-629 强制转登本册 findings.json） |
| 验收环境 | 复现树＝独立 detach worktree `C:/Users/zgl18/AppData/Local/Temp/acc-gtp-repro`（checkout db3beaaa）；**无 junction 运行**（三轮日志逐轮自检 `ACC_VCPKG_DIR_PRESENT=False`）；Defender 实时防护开启；操作员前置仅 ini 模板手工复制（§5.1④）＋进程 PATH 前置 Miniconda（§5.1③，驱动脚本进程内完成） |

## 分支冻结核对

- `git fetch origin` 后 `rev-parse origin/gate-toolchain-param` ＝ `db3beaaa1a68da02f155c55403396dc298812695`，与冻结 headSha 一致，无漂移。
- 远程无既有 `acc/gate-toolchain-param/*` 分支 → attempt 1，记录分支 `acc/gate-toolchain-param/1`。

## 逐项结论（4.1～4.11）

### 4.1 送验 diff 范围——pass

`git diff --name-only ebf86026..db3beaaa`：13 件＝gate-all.ps1＋development-task-breakdown.md＋traceability/findings.json＋traceability/builds/gate-toolchain-param/ 留痕目录 10 件（分类核对：README/三轮日志/汇总/studio 补建日志/驱动脚本/冒烟探针与证据汇总，与提交声明一致）。patches/ 零触及、框架源码零触及。gate-all.ps1 diff 逐行核仅三处：①头部 `-Toolchain` 用法注释块；②param 块 `[string]$Toolchain = ""`（含一行中文注释）；③原硬编码赋值改为注释块＋条件赋值 `if (-not $Toolchain) { $Toolchain = "$RepoRoot/vcpkg/scripts/buildsystems/vcpkg.cmake" }`——回落字符串与旧硬编码**逐字节一致**。检查逻辑（第 0 步 Test-Path、Ok/Fail 计数、汇总、exit 0/1）零改动。

### 4.2/4.3 无 junction 全量实测——pass

验收者自建复现树三轮 gate-all（同一命令，`-Toolchain` 直引主仓 vcpkg，全程无 junction、无预配置）：

| 轮次 | 耗时 | EXIT | 引擎汇总（验收者独立 awk/grep 复核计数一致） |
| --- | --- | --- | --- |
| run 1（全新树冷跑） | 52.7 min | 1 | 83 PASS / 1 FAIL——从零配置吃参直证（CMakeCache 记录参数路径）；唯一失败＝`sdurws_ird_testkit_test` 瞬态（见 F-629） |
| run 2 | 40.5 min | 1 | 84 PASS / 1 FAIL——testkit 瞬态再现；冒烟 40/40 全绿 |
| run 3 | 44.1 min | **0** | **85 PASS / 0 FAIL（85/85 通过）**——集成 40＋冒烟 40 全绿，含 `sdurws_ird_ui_test`（无需补建 studio：各测试目标 `TARGET_RUNTIME_DLLS` POST_BUILD 已复制运行期 DLL，PATH 前置 python313.dll 即可） |

ird_gates 三轮全 PASS（R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中）；验收者独立执行 validate-docs EXIT=0（20 units，12 trace entries，312 task files）。链上留痕（实施者三轮日志、toolchain-evidence.txt 四组证据、run3-summary 85/0）逐项抽查与声称一致，无粉饰（run 1 ui_test 0xc0000135 失败与 run 2 中止如实保留）。

### 4.4 findings——pass

F-628 → `fixed`（resolution 与本批实测一致＋fixedAt 2026-10-10）；F-619/F-620/F-625/F-626/F-627 均保持 `open`（微批修复不属本批）。基点最大编号核实为 F-628（603 条），本次验收建议项自 **F-629** 顺延。

### 4.5 真实失败能力（三件，全过）

1. **参数消费负例**：`-Toolchain D:/nonexistent/vcpkg.cmake` → 第 0 步环境自检明确失败（`vcpkg toolchain 不存在: D:/nonexistent/vcpkg.cmake`）＋同路径 cmake 配置失败，EXIT=1——参数真实被消费而非摆设（neg1-bad-toolchain.log）。
2. **缺省回落负例**：不带 `-Toolchain`、无 junction → 第 0 步按推定路径失败（`<复现树>/vcpkg/scripts/buildsystems/vcpkg.cmake`），EXIT=1——缺省行为与改动前一致（neg2-default-fallback.log）。
3. **变异测试**：临时还原条件赋值为旧硬编码 → 带**有效**主仓参运行仍按旧推定路径第 0 步失败（参数被无视），EXIT=1——参数生效确由新增条件赋值接线；事后还原并经 `git status` 核验与 HEAD 一致（neg3-mutation-old-hardcode.log）。

### 4.6 架构红线——pass

AGENTS §5 八条不涉越（本批仅治理脚本参数化＋文档账目）：SA-02 零框架源码修改（diff 证）、patches/ 零触及、**零 Python 约束不变**（脚本内唯一 "python" 字样＝约束声明注释本身）、检查逻辑/计数/口径零改动（4.1 逐行 diff 即证据）。

### 4.7 注释规范——pass

头部用法注释（含"为什么"：junction 穿透事故史＋系统性对策＋缺省行为承诺）、param 注释行、条件赋值注释块（有参用参/无参回落/赋值位置依据/三消费点清单）全部中文齐备，符合 AGENTS §2.7 脚本口径。

### 4.8 文档同步——pass

DTB v0.56 头部变更记录行（F-628 落地三要点）＋§5.1「fresh 树运行时前置」行补 ⑥vcpkg toolchain 供给口径，与实现逐点一致；三消费点 grep 实证：L89 环境自检 `Test-Path $Toolchain`、L103 集成配置 `-DCMAKE_TOOLCHAIN_FILE=$Toolchain`、L156 冒烟配置 `-DCMAKE_TOOLCHAIN_FILE=$Toolchain`，脚本内无其他硬编码 vcpkg 路径。

### 4.9 commit 质量——pass

2 提交均 `[governance]` 前缀＋四段式（What/Why/How/影响面）：8cdc504c（落地）如实标注「★ 全量实测尚未执行」；db3beaaa（留痕）补实测 EXIT=0 85/85。实施者三轮日志含两轮中止（run 1 ui_test 0xc0000135＝§5.1① 前置、run 2 ui_test 挂起＝§5.1③ PATH）如实保留，无带病前进、无粉饰。

### 4.10 留痕——pass

链上 `traceability/builds/gate-toolchain-param/`（10 件）在库且与声称一致；验收者复现产物（三轮日志、85/85 汇总、三件负例/变异、validate-docs、testkit 稳定性复测、冒烟配置探针、驱动脚本、README）已入本分支 `traceability/builds/acc-gate-toolchain-param/`。

### 4.11 偷懒扫描——pass

diff 新增行零 TODO/FIXME/stub/XXX/HACK；「只加参数不接消费点」式空改被 4.5①（neg1 传坏路径第 0 步即失败）＋集成 CMakeCache 记录＋冒烟配置探针三重证伪——参数真实接入全部三处消费点。

## 问题分级

- **阻断项：无。**
- **建议项（已强制转登 findings.json，编号 F-629）**：`sdurws_ird_testkit_test` 计时敏感用例在门禁负载下间歇失败——验收三轮 gate-all 集成段 2 次失败（冷/暖构建各一，ctest 退出码 8），同批冒烟段与空闲直跑复测 5 次全 PASS；根因画像＝ProcessRunnerTest 树终止用例观察窗 3500ms 对孙进程延迟 3000ms 仅 500ms 余量（源注释自述余量只计落盘开销），Defender 实时防护对 relink 后首次执行的扫描可吃尽余量。**与本批 diff 无关**（该批零触碰产品代码，testkit 源码与 redesign-main 基线一致），不影响本批 verdict；已留 testkit-retest-stability.log 供后续评估。

## 复现声明

- 本记录全部构建/负例/变异由验收者于自建临时树亲手执行，实施者树 `gate-tc-work` 未进入；主树仅 git 只读与 worktree 元数据操作。
- 三轮 gate-all 引擎计数均由验收者独立 awk/grep 复核（85/0 逐行计），非转录实施者数据。
- 验收完成后复现树/记录树按纪律清理（本批无 junction，无遗留链接面）。
