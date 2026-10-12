# FIX-T1 验收记录（attempt 2）

| 字段 | 值 |
| --- | --- |
| 任务 | FIX-T1 缺陷修复批A（交互域）——F-800 修复＋F-799 回归钉＋F-460/F-495 核实收口 |
| 契约 | tasks/foundation/FIX-T1.json（status=ready） |
| verdict | **fail**（阻断级 1 项 B-2＝留痕申报残留失实——README 回归表与返工提交正文中 requirements_test/ui_test 两行用例计数互换＋ui_test 行「含环境条件 skip」注记不实；**B-1 本体（gtest XML 零落库）已闭环**：六份 XML 真实、全绿、同次运行在库；技术面全量重验**全部通过**，见 §3/§4） |
| 验收模式 | 独立子会话验收（所有者 2026-10-12 指令，ACC §2.2，attempt 2）——与实施者及 attempt 1 验收者零共享记忆，不采信任何声称，一切结论以亲手执行的命令输出与亲读文件为准 |
| 送验对象（冻结 SHA） | base = a82f58bcd8db9354a6686f1dd540d167b6b32f26；head = **3d41a15c1f6f5770989ff3af7bf13830befb636d**（attempt 1 范围 25a7f92a＋B-1 返工 3d41a15c；`git ls-remote origin refs/heads/pipeline3/redesign-main` 亲证＝3d41a15c，零分支漂移；三提交 3e540267＋25a7f92a＋3d41a15c） |
| 复现现场 | worktree build/pipeline-wt3（HEAD==headSha 已核，tracked 零 diff，产品区变异恢复后两度亲证 git status 净）。**构建树口径（attempt 2 亲证厘清）**：使用该 worktree 自有集成构建树 `build/pipeline-wt3/build/`——其 CMakeCache `CMAKE_HOME_DIRECTORY=D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt3/RobWork`（指向冻结 worktree 源码）且 `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON` 已 grep 确认；仓库根 `build/` 的缓存指向主工作树源码（redesign-main），**未使用**（避免误构建非送验对象）。attempt 1 记录中「其集成构建树 build/」表述经本次厘清即此树。ACC §3 detached worktree 口径偏差与 attempt 1 相同（按所有者指令在实施隔离 worktree 复现，HEAD 冻结与干净度逐项核实），登记为环境口径偏差（不构成阻断） |
| 验收者环境 | Windows 10 x64 / Git Bash＋pwsh 7 / MSVC 17.12.6 / 亲跑日志与中间产物留存于系统临时目录 fixt1a2/（构建 7 目标 log＋六套件 log＋两变异四轮 build/run log＋gates log） |
| 三元组 | branch = acc/fix-t1/2；path = traceability/acceptance/fix-t1-20261012-attempt2.md；commit = 见本分支尖端（推送后回填于验收报告） |

## 0. 对 attempt 1 唯一阻断 B-1 的闭环结论（主项）

**B-1 本体已闭环，但闭环核验发现同类残留申报失实（新阻断 B-2）。**

B-1 本体（attempt 1：六套件 gtest XML 零落库而多处声称落库）逐项复核：

| attempt 1 B-1 返工输入 | attempt 2 亲证结论 |
| --- | --- |
| ①六套件真实重跑产出 XML 落库（禁止手造） | **满足**——`git ls-tree -r 3d41a15c -- …/builds/fix-t1/` 共 19 件＝README＋6 log＋6 report JSON＋**6 XML**（xml/ 子目录）；六份 XML 均非空（132 331／15 995／76 149／17 562／94 757／30 994 字节）；真实产物特征充分：内容域绑定正确（requirements_test.xml 全部 Req* 套件、ui_test.xml 全部 Ui*/Shell* 套件）、逐份 testcase 实数＝suite tests 合计＝顶层 tests 属性（机器断言三层一致）、时间戳 2026-10-12T21:24:02~08 成批且早于提交时刻 21:25:04 约 1 分钟 |
| ②六份 XML 与六件 report JSON 同次运行产出 | **满足**——六份 report JSON 计数与 XML 完全一致（含 skip 归类：modeling_gui 41+1skip／requirements_test 213+1 envUnavailable／ui_gui 80+1skip／其余全过）；六份 log 的套件耗时与 XML time 字段逐一吻合（316ms↔0.316、103↔0.103、1631↔1.631、2429↔2.429、1259↔1.259、9454↔9.454）；且 attempt 1 验收者独立亲跑的计数（375／214／267／42／47／81）与本轮 XML 完全一致 |
| ③commit/README 的 XML 声称如实修正 | **部分满足**——README「留痕构成更正」段**如实**登记了 attempt 1 fail 事实与根因（MSYS 风格 `/d/...` 路径在 `xml:` 前缀后不被 Windows 运行时解析、静默未产出），不粉饰；返工提交 3d41a15c「XML 落库」声称属实（19 文件 stat 亲证）。**但**：README 回归表与 3d41a15c 提交正文 What 段中 requirements_test/ui_test 两行计数互换（详见 §4 B-2）——申报与库内事实仍不符，B-1 同类残留 |
| ④返工后重新发起全量验收 | 本记录即 attempt 2 全量验收（非增量） |

## 1. 批背景核对结论（不作为结论依据，仅与亲证对照）

实施者返工申报（六套件 Windows 风格路径真实重跑、双件落库、README 申报更正）——XML/JSON/log 三方证据链**真实成立**（§0）；唯一残留为两行计数互换的申报失实（B-2）。

## 2. 指令验收动作逐项结论表（attempt 2 六动作）

| # | 动作 | 结论 | 关键证据 |
| --- | --- | --- | --- |
| 1 | B-1 闭环核验（主项） | **本体闭环；残留失实（B-2）** | 详 §0。XML 6/6 在库非空、failures=0（顶层＋testcase 级双断言）、计数 375/42/214/47/267/81；README B-1 更正段如实；3d41a15c `--stat` 19 文件全在 traceability/builds/fix-t1/ 内＝纯留痕面、零产品源码改动；**但 README 表 requirements_test 行写 267（XML/JSON/log 实值 214）、ui_test 行写 214（实值 267）且 ui_test 行「含环境条件 skip」注记不实（267/267 全过零 skip）；3d41a15c 提交正文同源互换（「requirements_test 267/ui_test 214」）** |
| 2a | diff 面核查 | **通过** | `git diff a82f58bc..3d41a15c --name-status` 全量 25 文件：modeling 三件＋DTB＋FIX-T1.json（A）＋findings.json＋builds/fix-t1/ 19 件——均在契约 allowedFiles 内；禁触模式（project/io/diagnostics/runtime/selection/trajectory/drivetrain/reporting/框架源码/scripts/RT-T15.json/UI-T78.json/REQUIREMENTS.md/patches/）pwsh 全量断言零命中；SA-02 无登记需求 |
| 2b | 六套件亲跑＋四组核心用例 | **通过** | 七目标重建 exit=0（error/warning 零命中，日志 610 行）；六套件亲跑全 exit=0：modeling_test **375/375**、requirements_test 213+1skip（GuiRegistration.V22RegisteredNotExecuted_ACC1 既有豁免）、ui_test **267/267**、modeling_gui 41+1skip（F-620 剪贴板面）、requirements_gui 47/47、ui_gui 80+1skip（ACC1）——**与在库六份 XML 计数逐一相符**；四组核心用例全 OK：StructureEditTest.ResetToSixAxis_ClearsDefaultTcp_WithChainRefs（F-800 钉）、MdlCommandPrepare.MixedBadBoundsRevoluteAndUnconfirmedContinuous_RejectsOnValueTrack（F-799 钉）、PluginPanel.AppliedReceipt*三用例（F-460）、KinEvaluationChannel 三 UI_T65 用例（F-495） |
| 2c | 变异验证（≥2 处） | **通过** | 变异1：注释 StructureEdit.cpp `ws.design.defaultTcp.reset();` → 重建 exit=0 → F-800 钉**红**（exit=1，FAILED）→ `git checkout --` 恢复（git status 净、HEAD 不变 3d41a15c）→ 重建复绿（filtered PASSED，全套件 375/375）。变异2（指令指定形态）：CommandHandlersTest.cpp:682 rangeFound 置位行短路（`{ /* MUTATION-2: rangeFound = true; */ }`）→ 重建 exit=0 → F-799 钉**红**（exit=1，「缺少 MDL-ASSERT-RANGE-NOT-FINITE 范围诊断」）→ 恢复（status 净、HEAD 不变）→ 全套件 375/375 复绿。两钉非恒真实证 |
| 2d | ird_gates＋双 validate | **通过** | ird_gates 构建 exit=0：「全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中」，引擎自测 fail_* 九例按预期检出（引擎健康）；validate-docs PASS（20 units, 12 trace entries, 317 task files）；validate-task（FIX-T1.json）PASS（1 tasks） |
| 2e | findings 账面核对 | **通过** | 新旧版经临时文件落盘后 pwsh ConvertFrom-Json 均合法解析、**815 条目不变**；F-460/F-799/F-800 status=open→**fixed** 且新增 fixedBy（diff 亲读：追加段均为【2026-10-12 缺陷修复批A …（FIX-T1）】纯 append，原文逐字保留）；F-495 status 维持 **open**、无 fixedBy、owner 纯追加；F-550/551/552/553 条目序列化逐字节一致（ConvertTo-Json -Compress 严格比对 True×4）；diff 全量仅 3 hunk 覆盖上述四条目 |
| 2f | 契约与 DTB | **通过** | validate-task 句法 PASS；DTB 版本行 **v0.64**（:5）＋WP-10-T79（:373，§2.11）＋WP-13-T22（:425，§2.14）＋WP-14-T12（:442，§2.15）三任务行亲读在位 |
| 3 | 注释规范核查 | **通过** | modeling 三文件改动面全查（改动面小，非抽查）：StructureEdit.cpp 修复段注释解释**为什么**（I-MDL-9 悬挂引用机理、与「不静默保留悬挂引用」承诺的矛盾、poseSetRef 同语义、默认 TCP 为链派生选择）＋追溯 F-800/FIX-T1，且 recordChange 摘要同步扩列；CommandHandlersTest.cpp 新用例 Doxygen 中文块注（缺陷形态＋F-746① 历史＋红阶段如实登记＋单位 ±1 rad＋decode 层 fail-fast 边界缘由）；StructureEditTest.cpp 新用例块注（F-800 机理＋断言处「悬挂引用不可入库」）——AGENTS §2 合格 |

## 3. 协议 4.1~4.11 逐项证据（ACC §4，attempt 2 全量重验）

### 4.1 改动范围 — 通过
见 §2 动作 2a。base..head 25 文件全部在契约 allowedFiles 内；forbiddenFiles 零触碰；patches/ 无新文件。

### 4.2 可复现构建 — 集成模式通过；独立冒烟模式未执行（口径偏差，与 attempt 1 相同，如实报告）
- 集成模式：`cmake --build build --config Release --target sdurws_ird_studio sdurws_ird_modeling_test sdurws_ird_requirements_test sdurws_ird_ui_test sdurws_ird_modeling_gui_test sdurws_ird_requirements_gui_test sdurws_ird_ui_gui_test`（build＝pipeline-wt3 自有构建树，缓存指向冻结源码已亲证）exit=0，error/warning 零命中。
- 独立冒烟模式：**未执行**——所有者 attempt 2 指令口径仍为集成树复跑＋六 exe 直跑；与 ACC §4.2 双模式要求口径偏差延续（attempt 1 S-1），如实报告。

### 4.3 契约 verify 命令 — 与 attempt 1 同口径
- verify[0] 集成构建三目标：以七目标超集覆盖，exit=0。
- verify[1] `ctest --test-dir build/RobWorkStudio/src/rwslibs/industrialrobot -C Release -L "^ird$"`：attempt 2 亲跑复核仍「No tests were found!!!」（该 worktree 构建树 ctest 零注册——基线既成形态非本批引入，attempt 1 S-2 延续）；等效替代＝六套件 exe 直跑＋双件留痕（DTB §5.5 口径），证据更强。
- verify[2] ird_gates：exit=0 零命中。verify[3] validate-docs：PASS。verify[4] validate-task：PASS。

### 4.4 acceptance 条目逐条证据 — 全部通过
1. F-799 修复面：通过——回归钉亲跑绿＋变异 2 翻红实证非恒真；断言域既有用例零回归（375/375）。
2. F-800 修复面：通过——变异 1 实证修复前真实红（注释 reset 行即 FAILED）、恢复后绿；drivetrainRef 保留／recordChange／guardChainInvariants 零变化（diff 亲读）。
3. F-460 收口面：通过——三用例亲跑全 OK；findings fixedBy 附三提交 SHA（2fe3da66f/a17df640a/c028c550d）与账面一致。
4. F-495 登记面：通过——三 UI_T65 用例亲跑全 OK；findings append 核实注、status=open 维持、不代翻口径与账面一致。
5. 回归门槛：**部分**——受影响三单元 gtest 全绿（亲跑）且 XML＋ird-test-report.json 双件落库 ✓；ird_gates 零新增 ✓；validate 双 PASS ✓；批外单元零 diff ✓；双模式构建之冒烟模式未执行（口径偏差见 4.2）；**留痕 README 表格两行计数申报失实**（B-2，计入 4.9）。

### 4.5 测试真实失败能力 — 通过（两断言变异均翻红并恢复复绿，§2 动作 2c）

### 4.6 架构红线 — 通过
三代码文件零新增 include（diff 亲读，L2 零 Qt／R-1/R-2 零新依赖边）；无名称前缀拼接；修复在 modeling 本域（权威归属正确）；SA-02 patches 零触碰；PA-2 不受损（recordChange 产生新记录，历史只增不改）。

### 4.7 注释规范 — 通过（§2 动作 3；改动面全查）

### 4.8 文档同步 — 通过
DTB v0.64＋三任务行（§2 动作 2f）；findings 四条目账面与代码事实同步（§2 动作 2e）；F-554~F-563/F-801~806 现状与移交登记于契约 note，无静默积压。

### 4.9 commit 质量 — **不通过（B-2）**
- 25a7f92a：attempt 1 已核的 XML 声称失实已由 3d41a15c 在库事实覆盖（XML 现已真实在库，历史提交正文按约定不改写）。
- **3d41a15c：四段结构齐备、B-1 根因与 attempt 1 fail 事实登记如实、验证段「六套件返工轮实跑全绿」属实；但 What 段计数申报「modeling_test 375/requirements_test 267/ui_test 214/…」中 requirements_test 与 ui_test 两值互换**（库内 XML/JSON/log 三方实证 requirements_test=214＝213 过＋1 环境跳过、ui_test=267 全过零 skip）。ACC §4.9「验证结论与留痕一致」。

### 4.10 留痕完整 — **部分不通过（B-2 落点；B-1 本体已闭环）**
- gtest XML：**6/6 在库且真实**（attempt 1 缺件已补）✓
- ird-test-report.json：6/6 在库 ✓；构建日志 6/6 在库 ✓
- 留痕 README 回归表：**6 行中 2 行与在库 XML 不符**（requirements_test 行 267↔实值 214；ui_test 行 214↔实值 267；且 ui_test 行「含环境条件 skip」注记不实——该套件 267/267 全过零 skip；「含环境条件 skip」应属 requirements_test 行）✗。同一 README 的「attempt 1 验收者独立口径」括注（requirements 213+1／ui 267 全过）反而正确——证明实施者掌握正确数值，表行为转抄错误而非伪造，但仍构成申报与事实不符。

### 4.11 偷懒/缩水实现扫描 — 通过
- ① diff 范围 TODO/FIXME/stub/placeholder/未实现/not implemented 零命中（grep 亲跑，无匹配）；
- ② 空实现：产品代码改动仅一行 reset＋注释与摘要同步，无空接口；
- ③ acceptance 逐条证据表：五条均有具名测试/命令输出/在库文件证据（XML 补齐后第五条 XML 半边证据成立）；
- ④ 测试强度：两新用例断言行为与边界，两变异翻红证明非恒真。

## 4. 发现的问题清单

### 阻断级

**B-2 留痕申报残留失实：README 回归表与返工提交正文中 requirements_test/ui_test 两行计数互换＋ui_test 行 skip 注记不实（协议 4.9/4.10；attempt 1 B-1 同类残留）**
- 事实：库内三方证据（xml/requirements_test.xml tests=214＋reports/…requirements_test.json total=214＋requirements_test.log「214 tests ran」；xml/ui_test.xml tests=267＋reports/…ui_test.json total=267＋ui_test.log「267 tests ran」）一致证明 requirements_test=214（213 过＋1 环境条件跳过）、ui_test=267（全过零 skip）；而留痕 README 回归表写「sdurws_ird_requirements_test｜267 用例全 PASS」「sdurws_ird_ui_test｜214 用例全 PASS（含环境条件 skip）」，返工提交 3d41a15c 正文 What 段同源写「requirements_test 267/ui_test 214」——两行申报与库内事实不符，ui_test 行「含环境条件 skip」注记亦不实。
- 定性：与 attempt 1 B-1 同属「申报与留痕不一致」纪律缺口（ACC §4.9「发现粉饰即不通过」、AGENTS §7 如实陈述）。**不涉及任何技术面缺陷**——六套件真实全绿、XML/JSON/log 证据链完整自洽、且同 README 括注中数值正确（转抄错误非伪造、无夸大无隐瞒失败）；但 B-1 返工的目标正是恢复「声称＝事实」，交付件中再次出现同源失实，不能计为闭环完成。
- 返工输入（attempt 3 前须完成）：①修正 README 回归表两行——requirements_test 行改「214 用例（213 过＋1 环境条件 skip）」、ui_test 行改「267/267 PASS（failures=0，无 skip）」；②以新提交落库并在提交正文如实声明本更正（3d41a15c 已推送，按约定不改写历史，原正文失实由新提交声明覆盖）；③重新发起全量验收（ACC §5，不做增量验收）。预计无技术风险——本记录其余全部结论在 attempt 3 可直接复核。

### 建议级（不阻塞；按 ACC §5 转登义务由后续批次登记 findings）

- S-1（attempt 1 延续）独立冒烟模式未执行：所有者两轮指令口径均为集成树复跑＋exe 直跑；后续验收指令如需 ACC §4.2 完整双模式，建议显式列冒烟步骤。
- S-2（attempt 1 延续）契约 verify[1]（ctest -L ird）在 worktree 构建树恒「No tests were found」（基线既成，非本批引入；attempt 2 亲跑复核）：建议后续治理批修订 verify 命令模板或为 worktree 树恢复 ctest 注册。
- S-5（新增，随 B-2 返工消解）留痕 README 表格数值转抄无自校对环节：本轮互换恰被同文档括注反证，建议留痕表生成后与 XML 机器比对一次（一条 pwsh 断言即可）再提交。

## 5. 裁决

- **verdict = fail**：所有者 attempt 2 pass 判据＝「技术面全过＋B-1 闭环」。技术面（范围/注释/构建/六套件/四组核心用例/双变异/门禁/双 validate/账面/契约/DTB）经全量重验**全部通过**；B-1 本体（XML 零落库）已闭环且证据真实充分；但 B-1 闭环核验清单中「六份 XML 的用例计数与 README 表格逐一相符」一项 6 行中 2 行不符（另携 1 处 skip 注记错位），构成 B-1 同类残留申报失实——判 **fail**，唯一阻断 B-2。
- 本记录即返工输入；实施者按 B-2 返工输入三步（两行表修正＋更正提交＋重新全量验收）执行后发起 attempt 3。
- 合入决策归所有者；fail 状态下不得合入 redesign-main。

## 6. 验收者亲跑命令输出摘录（关键证据）

```
# 冻结与漂移（attempt 2 亲证）
git rev-parse HEAD                          → 3d41a15c1f6f5770989ff3af7bf13830befb636d
git ls-remote origin refs/heads/pipeline3/redesign-main → 3d41a15c…（零漂移）
git status（产品区，变异恢复后两度）        → （空＝净）
构建树指向：build/pipeline-wt3/build/CMakeCache.txt
  CMAKE_HOME_DIRECTORY = …/build/pipeline-wt3/RobWork（冻结源码）
  RWS_BUILD_INDUSTRIALROBOT:BOOL=ON

# 动作1 B-1 闭环
git ls-tree -r 3d41a15c -- …/builds/fix-t1/ → 19 件（README＋6 log＋6 json＋6 xml）
六份 XML（pwsh [xml] 解析，顶层＋testcase 级双断言）：
  modeling_test        tests=375 failures=0 errors=0 testcase_failures=0 ts=21:24:02.114
  modeling_gui_test    tests= 42 failures=0 errors=0 testcase_failures=0 ts=21:24:04.660
  requirements_test    tests=214 failures=0 errors=0 testcase_failures=0（skipped 元素 1）ts=21:24:02.558
  requirements_gui_test tests= 47 failures=0 errors=0 testcase_failures=0 ts=21:24:07.267
  ui_test              tests=267 failures=0 errors=0 testcase_failures=0（skipped 元素 0）ts=21:24:02.722
  ui_gui_test          tests= 81 failures=0 errors=0 testcase_failures=0（skipped 元素 1）ts=21:24:08.917
report JSON summary：375/375、42=41+1skip、214=213+1envUnavailable(decisive=false)、47/47、267/267、81=80+1skip
log↔XML 耗时吻合：316ms↔0.316／2429↔2.429／103↔0.103／1259↔1.259／1631↔1.631／9454↔9.454
git show 3d41a15c --stat → 19 files，全在 traceability/builds/fix-t1/（纯留痕面）

# 动作2b 亲跑（构建树 build/pipeline-wt3/build，PATH 前置 vcpkg/Miniconda3）
cmake --build … --target <7目标> → BUILD_EXIT=0（grep error/warning 0 命中）
./sdurws_ird_modeling_test.exe         → MDL_EXIT=0；375 tests ran，PASSED 375
./sdurws_ird_requirements_test.exe     → REQ_EXIT=0；214 ran，PASSED 213＋SKIPPED 1（ACC1）
./sdurws_ird_ui_test.exe               → UI_EXIT=0；267 ran，PASSED 267
./sdurws_ird_modeling_gui_test.exe     → MDLGUI_EXIT=0；42 ran，PASSED 41＋SKIPPED 1（F-620）
./sdurws_ird_requirements_gui_test.exe → REQGUI_EXIT=0；47 ran，PASSED 47
./sdurws_ird_ui_gui_test.exe           → UIGUI_EXIT=0；81 ran，PASSED 80＋SKIPPED 1（ACC1）
核心用例：ResetToSixAxis_ClearsDefaultTcp_WithChainRefs OK／
MixedBadBoundsRevoluteAndUnconfirmedContinuous_RejectsOnValueTrack OK／
AppliedReceipt*_F460 ×3 OK／KinEvaluationChannel.*_UI_T65 ×3 OK

# 动作2c 变异1（F-800 钉——指令指定）
[注释 StructureEdit.cpp:321 ws.design.defaultTcp.reset();]
cmake --build … --target sdurws_ird_modeling_test → MUT1_BUILD_EXIT=0
--gtest_filter=StructureEditTest.ResetToSixAxis_ClearsDefaultTcp_WithChainRefs
  → MUT1_RUN_EXIT=1；[  FAILED  ] 1 test
git checkout -- → status 净 → HEAD 仍 3d41a15c → 重建 → filtered PASSED；
全套件复跑 375/375（FINAL_MDL_EXIT=0）

# 动作2c 变异2（F-799 钉半区短路——指令指定形态）
[CommandHandlersTest.cpp:682 → { /* MUTATION-2: rangeFound = true; */ }]
cmake --build … → MUT2_BUILD_EXIT=0
--gtest_filter=MdlCommandPrepare.MixedBadBounds…RejuectsOnValueTrack
  → MUT2_RUN_EXIT=1；「缺少 MDL-ASSERT-RANGE-NOT-FINITE 范围诊断」FAILED
git checkout -- → status 净 → HEAD 仍 3d41a15c → 重建 → 全套件 375/375 复绿

# 动作2d 门禁与校验
cmake --build … --target ird_gates → GATES_EXIT=0
  「全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中」
  自测 fail_r1/r3/r4/r5/t1/t2/sub 按预期检出（引擎健康）
pwsh validate-docs.ps1  → PASS (20 units, 12 trace entries, 317 task files)
pwsh validate-task.ps1 -TaskFile FIX-T1.json → PASS (1 tasks)
ctest --test-dir … -L "^ird$" → No tests were found!!!（S-2 延续）

# 动作2e findings 机器验证（临时文件落盘后解析，规避管道损坏）
旧版 815 条目＝新版 815 条目（合法 JSON 双解析）
F-460 open→fixed＋fixedBy／F-799 open→fixed＋fixedBy／F-800 open→fixed＋fixedBy
F-495 open→open（无 fixedBy，owner 纯追加）
F-550/551/552/553 条目序列化严格一致 True×4
diff 全量 3 hunk＝仅上述四条目

# B-2 失实证据（库内三方对 README 两行）
xml/requirements_test.xml tests=214 ↔ README「sdurws_ird_requirements_test｜267 用例全 PASS」
xml/ui_test.xml tests=267 ↔ README「sdurws_ird_ui_test｜214 用例全 PASS（含环境条件 skip）」
（ui_test 零 skip：XML skipped 元素 0、JSON skipped=0、log 无 SKIPPED 段）
3d41a15c 正文：「requirements_test 267/ui_test 214」——同源互换
本 README 同文括注「requirements 213+1 skip／ui 267 全过」数值反而正确（转抄错误非伪造）
```
