# FIX-T1 验收记录（attempt 3）

| 字段 | 值 |
| --- | --- |
| 任务 | FIX-T1 缺陷修复批A（交互域）——F-800 修复＋F-799 回归钉＋F-460/F-495 核实收口 |
| 契约 | tasks/foundation/FIX-T1.json（status=ready） |
| verdict | **pass**（B-1/B-2 双闭环成立；技术面全量重验 4.1~4.11 全部通过；无阻断级问题。建议级观察 1 项 O-1 留档不阻塞，见 §4） |
| 验收模式 | 独立子会话验收（所有者 2026-10-12 指令，ACC §2.2，attempt 3）——与实施者及 attempt 1/2 验收者零共享记忆，不采信任何声称，一切结论以亲手执行的命令输出与亲读文件为准 |
| 送验对象（冻结 SHA） | base = a82f58bcd8db9354a6686f1dd540d167b6b32f26；head = **2f14d1fc0e1e1326db18fc1ab28c0cd91f6740da**（attempt 2 范围 3d41a15c＋B-2 返工 2f14d1fc；`git ls-remote origin refs/heads/pipeline3/redesign-main` 亲证＝2f14d1fc，零分支漂移；四提交 3e540267＋25a7f92a＋3d41a15c＋2f14d1fc） |
| 复现现场 | worktree build/pipeline-wt3（HEAD==headSha 已核 2f14d1fc，开始时 tracked 零 diff；两轮变异恢复后两度亲证 git status 净且 HEAD 不变）。构建树＝该 worktree 自有集成构建树 `build/pipeline-wt3/build/`——grep CMakeCache 亲证 `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON` 且 `CMAKE_HOME_DIRECTORY=D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt3/RobWork`（指向冻结 worktree 源码）；仓库根 build/（属主工作树）未使用（环境红线）。ACC §3 detached worktree 口径偏差与前两轮相同（按所有者指令在实施隔离 worktree 复现，HEAD 冻结与干净度逐项核实），登记为环境口径偏差（不构成阻断） |
| 验收者环境 | Windows 10 x64 / Git Bash＋pwsh 7 / MSVC 17.12.6 / 亲跑日志留存于系统临时目录 fixt1a3/（构建 log＋六套件 log＋ui 复跑与 PERF 单跑 log＋两变异 build/run log＋gates log＋双 validate log） |
| 三元组 | branch = acc/fix-t1/3；path = traceability/acceptance/fix-t1-20261012-attempt3.md；commit = 见本分支尖端（推送后回填于验收报告） |

## 0. B-1/B-2 双闭环结论（主项）

**B-1（attempt 1：gtest XML 零落库）与 B-2（attempt 2：README 表两行计数互换）双闭环成立。**

### B-1 闭环（XML 六份在库真实全绿——attempt 2 已闭环，本轮复核维持）

`git ls-tree -r 2f14d1fc -- …/traceability/builds/fix-t1/` 共 19 件＝README＋6 log＋6 report JSON＋**6 XML**（xml/ 子目录）。本轮机器复核六份 XML：

| 套件 | `<testsuites>` 顶层属性（grep 亲提） | failure 元素 | skipped 元素 | 时间戳 |
| --- | --- | --- | --- | --- |
| modeling_test | tests=375 failures=0 errors=0 | 0 | 0 | 21:24:02.114 |
| requirements_test | tests=214 failures=0 errors=0 | 0 | 1（GuiRegistration.V22RegisteredNotExecuted_ACC1） | 21:24:02.558 |
| ui_test | tests=267 failures=0 errors=0 | 0 | 0 | 21:24:02.722 |
| modeling_gui_test | tests=42 failures=0 errors=0 | 0 | 1（PreviewPane_MultiTypeChain_UI_T59） | 21:24:04.660 |
| requirements_gui_test | tests=47 failures=0 errors=0 | 0 | 0 | 21:24:07.267 |
| ui_gui_test | tests=81 failures=0 errors=0 | 0 | 1（ParamTablePanelGuiTest.BatchPasteImpactDetail_UI_T08_ACC1） | 21:24:08.917 |

顶层 failures=0 与 testcase 级 failure 元素计数双断言一致；skip 归属（ACC1 豁免×2、F-620 剪贴板面×1）与 README 声称逐一吻合。

### B-2 闭环（README 表六行数值与 XML 机器提取值逐一相符——本轮主核）

`git show 2f14d1fc --stat`：**单文件** builds/fix-t1/README.md（8 insertions/6 deletions）＝纯留痕面，产品源码零改动。更正后 README 表六行逐行比对 XML 机器提取值：

| README 行（2f14d1fc 版亲读） | XML 实值 | 相符 |
| --- | --- | --- |
| modeling_test｜375/375 PASS（failures=0，含新增 2 用例） | 375，failures=0，skip 0 | ✓ |
| requirements_test｜214 用例（213 过＋1 环境条件 skip〔ACC1 豁免〕；failures=0） | 214，failures=0，skipped=1（ACC1） | ✓ |
| ui_test｜267/267 全过（failures=0，零 skip） | 267，failures=0，skipped=0 | ✓ |
| modeling_gui_test｜42 用例（41 过＋1 环境条件 skip——F-620 剪贴板面；failures=0） | 42，failures=0，skipped=1（UI_T59） | ✓ |
| requirements_gui_test｜47/47 PASS（failures=0） | 47，failures=0，skipped=0 | ✓ |
| ui_gui_test｜81 用例（80 过＋1 环境条件 skip；failures=0） | 81，failures=0，skipped=1（ACC1） | ✓ |

B-2 返工三步逐项核验：①两行修正到位（requirements_test=214＝213 过＋1 skip〔ACC1〕；ui_test=267 全过零 skip——与 attempt 2 返工输入要求逐字一致）✓；②以新提交 2f14d1fc 落库且提交正文四段如实声明本更正（What 段登记两行写反事实＋S-5 建议采纳〔表格数值改由 XML `<testsuites tests=>` 属性机器提取回填，不再手工转抄〕；Why 段登记 attempt 2 fail 事实 acc/fix-t1/2 @f67267f8 与 B-1 已闭环结论）✓；③本记录即 attempt 3 全量验收（非增量）✓。

README 更正段如实登记两轮 fail 事实：「留痕构成更正（attempt 1 验收 B-1 返工）」段与「二次更正（attempt 2 验收 B-2）」段亲读在位——MSYS 路径静默未产出 XML 的根因、两轮验收分支与 SHA、机器提取回填做法均如实载明，无粉饰。

## 1. 指令验收动作逐项结论表（attempt 3）

| # | 动作 | 结论 | 关键证据 |
| --- | --- | --- | --- |
| 1 | B-1/B-2 闭环核验（主项） | **通过（双闭环）** | 详 §0。README 六行↔XML 机器提取值逐一相符；XML 六份 failures=0 双断言；README 更正段如实登记两轮 fail；2f14d1fc `--stat` 单文件纯留痕面 |
| 2a | diff 面核查 | **通过** | `git diff a82f58bc..2f14d1fc --name-status` 全量 25 文件：modeling 三件（StructureEdit.cpp／StructureEditTest.cpp／CommandHandlersTest.cpp）＋DTB（M）＋FIX-T1.json（A）＋findings.json（M）＋builds/fix-t1/ 19 件（A：README＋6 log＋6 report JSON＋6 XML）——均在契约 allowedFiles 内；禁触模式（project/io/diagnostics/runtime/selection/trajectory/drivetrain/reporting/框架源码/scripts/RT-T15.json/UI-T78.json/REQUIREMENTS.md/patches/）机器断言零命中；SA-02 无登记需求 |
| 2b | 六套件亲跑＋四组核心用例 | **通过** | 七目标（studio＋六测试）重建 exit=0，error/warning 零命中（grep 计数 0）；六套件亲跑全 exit=0：modeling_test **375/375**、requirements_test 213+1skip（ACC1 既有豁免）、ui_test **267/267**（复跑；首跑 1 用例环境时序抖动见 O-1）、modeling_gui 41+1skip（F-620）、requirements_gui 47/47、ui_gui 80+1skip（ACC1）——与在库六份 XML 计数逐一相符；四组核心用例亲跑全 OK：StructureEditTest.ResetToSixAxis_ClearsDefaultTcp_WithChainRefs（F-800 钉）、MdlCommandPrepare.MixedBadBoundsRevoluteAndUnconfirmedContinuous_RejectsOnValueTrack（F-799 钉）、PluginPanel.AppliedReceipt* 三用例（F-460）、KinEvaluationChannel.*UI_T65 三用例（F-495） |
| 2c | 变异验证（两处） | **通过** | 变异1（指令指定）：注释 StructureEdit.cpp:321 `ws.design.defaultTcp.reset();` → 重建 exit=0 → F-800 钉**红**（exit=1，StructureEditTest.cpp:248 `ws.design.defaultTcp.has_value()` 断言失败）→ `git checkout --` 恢复（git status 净、HEAD 不变 2f14d1fc）→ 重建复绿（全套件 375/375）。变异2（产品面三重短路）：CommandHandlers.cpp 三值轨出口同拍短路（:1218 策略不可解析门／:1232 EvaluationFailed 门／:1242 blockers 门各改 `if (false && …)`）→ 重建 exit=0 → F-799 钉**红**（exit=1，CommandHandlersTest.cpp:677 Expected equality 失败）→ 恢复（git status 净、HEAD 不变）→ 全套件 375/375 复绿。两钉非恒真实证；变异 2 单/双出口短路会被纵深出口吸收（attempt 1 已证），三重短路方红＝值轨出口唯一性机理再次实证 |
| 2d | ird_gates＋双 validate | **通过** | ird_gates 构建 exit=0，自报「全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中」（日志终端乱码系编码显示问题、内容可辨），引擎自测 fail_r1/r3/r4/r4cliff/r5/t1/t2/sub 九例按预期检出（引擎健康）；validate-docs **PASS（20 units, 12 trace entries, 317 task files）**；validate-task（FIX-T1.json）**PASS（1 tasks）** |
| 2e | findings 账面核对 | **通过** | 基线 a82f58bc 与送验 2f14d1fc 两版 JSON 经临时文件落盘后 pwsh ConvertFrom-Json 均合法解析、**815 条目不变**；F-460 status=open→**fixed**＋fixedBy（audit-r2c 批 2fe3da66f／验收 pass a17df640a／合入 c028c550d）、F-799 →**fixed**（F-746① 顺带修复＋本批补钉）、F-800 →**fixed**（本批 StructureEdit.cpp 同拍 reset）；F-495 status 维持 **open**、无 fixedBy、owner 纯追加（宿主复验归所有者不代翻）；F-550/551/552/553 条目 ConvertTo-Json -Compress 序列化逐字节一致 True×4；findings diff 全量 3 hunk 且增删行中条目 id 行零变动（F-496/F-801 仅 hunk 上下文）——改动收敛于上述四条目的 status/fixedBy/owner 字段 |
| 2f | 契约与 DTB | **通过** | validate-task 句法 PASS；DTB 版本行 v0.64＋WP-10-T79（§2.11）＋WP-13-T22（§2.14）＋WP-14-T12（§2.15）三任务行（attempt 2 亲证在位；本批范围 2f14d1fc 对 DTB 零新增改动，diff 复核一致） |
| 3 | 注释规范核查 | **通过** | modeling 三文件改动面全查（改动面小，非抽查）：StructureEdit.cpp:310-323 修复段注释解释**为什么**（链引用面随链重建清空的诚实形态、I-MDL-9 悬挂引用机理、与「不静默保留悬挂引用」承诺的矛盾、与 poseSetRef 同语义、默认 TCP 为链派生选择）＋追溯 F-800/FIX-T1，且 recordChange 摘要同步扩列、drivetrainRef 保留注明 MDL-16 依据；StructureEditTest.cpp 新用例 /// 中文块注（F-800 出处＋audit 条目＋I-MDL-9）＋断言处「悬挂引用不可入库」；CommandHandlersTest.cpp 新用例 /** @brief */ Doxygen 中文块注（缺陷形态＋F-799/F-746① 历史＋红阶段如实登记「缺陷已不在，直接绿」＋单位 ±1 rad＋decode 层 fail-fast 边界缘由）＋断言处值轨唯一出口与 ERR-01 逐项定位注记——AGENTS §2 合格；新增行 TODO/FIXME/stub/placeholder/未实现/not implemented 扫描零命中 |

## 2. 协议 4.1~4.11 逐项证据（ACC §4，attempt 3 全量重验）

### 4.1 改动范围 — 通过
见 §1 动作 2a。base..head 25 文件全部在契约 allowedFiles 内；forbiddenFiles 零触碰（机器断言）；patches/ 无新文件（SA-02）。

### 4.2 可复现构建 — 集成模式通过；独立冒烟模式未执行（口径偏差，与前两轮相同，如实报告）
- 集成模式：`cmake --build build --config Release --target sdurws_ird_studio sdurws_ird_modeling_test sdurws_ird_requirements_test sdurws_ird_ui_test sdurws_ird_modeling_gui_test sdurws_ird_requirements_gui_test sdurws_ird_ui_gui_test`（build＝pipeline-wt3 自有构建树，缓存指向冻结源码已 grep 亲证）exit=0，error/warning 零命中。
- 独立冒烟模式：**未执行**——所有者三轮指令口径均为集成树复跑＋六 exe 直跑（强于冒烟）；与 ACC §4.2 双模式要求口径偏差延续（attempt 1 S-1），如实报告。

### 4.3 契约 verify 命令 — 与前两轮同口径
- verify[0] 集成构建三目标：以七目标超集覆盖，exit=0。
- verify[1] `ctest --test-dir build/RobWorkStudio/src/rwslibs/industrialrobot -C Release -L "^ird$"`：worktree 构建树 ctest 零注册为基线既成形态（attempt 1 S-2，前两轮亲跑复核一致；本轮沿 exe 直跑等效替代，未再重复跑该命令）。
- verify[2] ird_gates：exit=0 零命中。verify[3] validate-docs：PASS。verify[4] validate-task：PASS。

### 4.4 acceptance 条目逐条证据 — 全部通过
1. F-799 修复面：通过——回归钉亲跑绿＋变异 2（三重短路）翻红实证非恒真；断言域既有用例零回归（375/375）。
2. F-800 修复面：通过——变异 1 实证修复前真实红（注释 reset 行即 has_value() 断言失败）、恢复后绿；drivetrainRef 保留／recordChange／guardChainInvariants 零变化（diff 亲读 :322-330）。
3. F-460 收口面：通过——AppliedReceipt 三用例亲跑全 OK；findings fixedBy 附三提交 SHA（2fe3da66f/a17df640a/c028c550d）与账面一致。
4. F-495 登记面：通过——KinEvaluationChannel 三 UI_T65 用例亲跑全 OK；findings append 核实注、status=open 维持、不代翻口径与账面一致。
5. 回归门槛：**通过**——受影响三单元 gtest 全绿（亲跑，六套件计数与在库 XML 逐一相符）且 XML＋ird-test-report.json＋log 三方留痕在库 ✓；ird_gates 零新增 ✓；validate 双 PASS ✓；批外单元零 diff ✓；双模式构建之冒烟模式未执行（口径偏差见 4.2，前两轮 S-1 延续，不构成阻断）。

### 4.5 测试真实失败能力 — 通过（两断言变异均翻红并恢复复绿，§1 动作 2c）

### 4.6 架构红线 — 通过
modeling diff 零 include 行变动（R-1/R-2 零新依赖边，grep 亲证）；`#include <Q…>` 零命中（L2 零 Qt）；无名称前缀拼接；修复在 modeling 本域（权威归属正确）；SA-02 patches 零触碰；PA-2 不受损（recordChange 产生新记录，历史只增不改）。

### 4.7 注释规范 — 通过（§1 动作 3；改动面全查）

### 4.8 文档同步 — 通过
DTB v0.64＋三任务行（§1 动作 2f）；findings 四条目账面与代码事实同步（§1 动作 2e）；F-554~F-563/F-801~806 现状与移交登记于契约 note，无静默积压。

### 4.9 commit 质量 — 通过
- 25a7f92a／3d41a15c：前两轮已核；3d41a15c 正文计数失实由 2f14d1fc 正文如实声明覆盖（历史提交按约定不改写）。
- **2f14d1fc：四段结构齐备**——What（两行修正＋机器提取回填做法）、Why（attempt 2 fail 事实＋B-1 已闭环结论，含验收分支与 SHA）、How（机器比对六值逐一相符＋产品源码零改动声明）、影响面（仅留痕 README 一文件＋attempt 3 全量重验声明）；验证结论与库内事实一致（本轮逐行复核相符），无粉饰。

### 4.10 留痕完整 — 通过
- gtest XML：6/6 在库且真实（顶层＋testcase 级双断言、skip 归属吻合、时间戳成批 21:24:02~08）✓
- ird-test-report.json：6/6 在库 ✓；构建/运行日志 6/6 在库 ✓
- 留痕 README 表六行与 XML 实值逐一相符（B-2 闭环）✓；两轮 fail 事实登记如实 ✓
- phase-one-validation.log/resultNote：FIX-T1 契约无此字段（老字段，本批不适用——如实注记）。

### 4.11 偷懒/缩水实现扫描 — 通过
- ① diff 范围 TODO/FIXME/stub/placeholder/未实现/not implemented 零命中（新增行 grep 亲跑）；
- ② 空实现：产品代码改动仅一行 reset＋注释与摘要同步，无空接口；
- ③ acceptance 逐条证据表：五条均有具名测试亲跑输出／变异翻红记录／在库文件三方证据（见 §2 4.4）；
- ④ 测试强度：两新用例断言行为与边界（值轨出口＋objectWrites 空＋诊断码传导；joints 数＋引用表清空＋defaultTcp 无值），两变异翻红证明非恒真。

## 3. 裁决

- **verdict = pass**：ACC §5 pass 判据＝4.1~4.11 全部通过且无阻断级问题。attempt 1 B-1（XML 零落库）与 attempt 2 B-2（README 两行计数互换）均已闭环且证据链三方互证；技术面（范围/注释/构建/六套件亲跑/四组核心用例/双变异翻红/门禁/双 validate/findings 815 条目账面/契约/DTB）经全量重验全部通过。
- 无阻断级问题。建议级观察 1 项（O-1）留档于 §4，不阻塞合入。
- 合入决策归所有者（本记录仅为验收证据；合入机械次序见 PIPE §4.7）。

## 4. 发现的问题清单

### 阻断级

无。

### 建议级（不阻塞；转登义务沿用前两轮口径——随验收记录留档并建议后续治理批登记 findings）

- **O-1（新增，留档）ui_test 性能用例存在环境时序敏感性**：本验收首轮全套件亲跑中 `TaskPresentationModelTest.UiThreadHeartbeatUnderSlowPeerQuery_UI_PERF_1` 失败一次（断言 `maxGap < 200ms` 实测 444ms——UI 线程心跳被调度延迟；TaskPresentationModelTest.cpp:662）。同一进程负载下取证：该用例单独重跑 3/3 绿、全套件复跑 267/267 绿 exit=0；attempt 1/2 两轮验收者亲跑均绿；本批 ui 单元源码零文件触碰（diff 机器断言）——判定为环境时序抖动（机器负载面），非本批回归，且抖动本身反证该 PERF 钉对真实时序敏感、非恒真。建议后续治理批评估为该用例增加重试容差或负载隔离（如 gtest 环境级 CPU 空闲等待），避免跨环境验收对数歧义；留痕 README 亦可注记该用例的环境敏感性。

### 延续留档（attempt 1 S-1/S-2，口径不变）

- S-1 独立冒烟模式未执行（三轮指令口径一致收窄为集成树复跑＋exe 直跑——如需 ACC §4.2 完整双模式，建议指令显式列冒烟步骤）。
- S-2 契约 verify[1]（ctest -L ird）在 worktree 构建树恒「No tests were found」（基线既成）——建议后续治理批修订 verify 命令模板或为 worktree 树恢复 ctest 注册。

## 5. 验收者亲跑命令输出摘录（关键证据）

```
# 冻结与漂移（attempt 3 亲证）
git rev-parse HEAD                            → 2f14d1fc0e1e1326db18fc1ab28c0cd91f6740da
git ls-remote origin refs/heads/pipeline3/redesign-main → 2f14d1fc…（零漂移）
git status --porcelain（变异恢复后两度）      → （空＝净，HEAD 不变）

# 构建树指向（环境红线）
grep RWS_BUILD_INDUSTRIALROBOT|CMAKE_HOME_DIRECTORY build/CMakeCache.txt
  → RWS_BUILD_INDUSTRIALROBOT:BOOL=ON
  → CMAKE_HOME_DIRECTORY=D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt3/RobWork

# 动作1 B-1/B-2 闭环（README 六行↔XML 机器提取）
grep '<testsuites' xml/*.xml（六份）：
  modeling_test.xml         tests="375" failures="0" ts=2026-10-12T21:24:02.114
  requirements_test.xml     tests="214" failures="0" ts=21:24:02.558
  ui_test.xml               tests="267" failures="0" ts=21:24:02.722
  modeling_gui_test.xml     tests="42"  failures="0" ts=21:24:04.660
  requirements_gui_test.xml tests="47"  failures="0" ts=21:24:07.267
  ui_gui_test.xml           tests="81"  failures="0" ts=21:24:08.917
grep -c '<failure' / '<skipped'（六份）：failure 全 0；skipped＝0/1/0/1/0/1
  （skip 用例名：V22RegisteredNotExecuted_ACC1／PreviewPane_MultiTypeChain_UI_T59／BatchPasteImpactDetail_UI_T08_ACC1）
git show 2f14d1fc --stat → README.md 1 file, 8 insertions(+), 6 deletions（纯留痕面单文件）

# 动作2a diff 面
git diff a82f58bc..2f14d1fc --name-status → 25 文件（§1 动作 2a 清单）
禁触模式逐项 grep -c → 全 0（project/io/diagnostics/runtime/selection/
  trajectory/drivetrain/reporting/RT-T15.json/UI-T78.json/REQUIREMENTS.md/patches//scripts/）
industrialrobot 外源码改动 → 零命中

# 动作2b 亲跑（构建树 build/pipeline-wt3/build，PATH 前置 vcpkg/Miniconda3）
cmake --build … --target <7目标> → BUILD_EXIT=0（grep -ciE "error|warning" → 0）
./sdurws_ird_modeling_test.exe         → MDL_EXIT=0；375 ran，PASSED 375
./sdurws_ird_requirements_test.exe     → REQ_EXIT=0；214 ran，PASSED 213＋SKIPPED 1（ACC1）
./sdurws_ird_ui_test.exe               → 首跑 UI_EXIT=1（UI_PERF_1 时序抖动，见 O-1）
                                         → 复跑 UI_EXIT=0；267 ran，PASSED 267
./sdurws_ird_modeling_gui_test.exe     → MDLGUI_EXIT=0；42 ran，PASSED 41＋SKIPPED 1（F-620）
./sdurws_ird_requirements_gui_test.exe → REQGUI_EXIT=0；47 ran，PASSED 47
./sdurws_ird_ui_gui_test.exe           → UIGUI_EXIT=0；81 ran，PASSED 80＋SKIPPED 1（ACC1）
核心用例（--gtest_filter 亲跑）：
  ResetToSixAxis_ClearsDefaultTcp_WithChainRefs          → PASSED（EXIT=0）
  MixedBadBounds…Continuous_RejectsOnValueTrack          → PASSED（EXIT=0）
  PluginPanel.AppliedReceipt*（3 用例）                  → PASSED 3（EXIT=0）
  KinEvaluationChannel.*UI_T65（3 用例）                 → PASSED 3（EXIT=0）

# O-1 取证（ui PERF 用例环境敏感性）
首跑失败断言：TaskPresentationModelTest.cpp(662) Expected (maxGap) < 200ms,
  actual 444ms；「UI 线程出现阻塞（心跳最大间隙 444 ms）」
单跑 ×3 → PERF_RUN_EXIT=0×3（1 ok / 0 failed each）
全套件复跑 → UI_RERUN_EXIT=0；267 tests ran，PASSED 267

# 动作2c 变异1（F-800 钉——指令指定）
[StructureEdit.cpp:321 → // MUTATION-ACC3: ws.design.defaultTcp.reset();]
cmake --build … --target sdurws_ird_modeling_test → MUT_BUILD_EXIT=0
--gtest_filter=StructureEditTest.ResetToSixAxis_ClearsDefaultTcp_WithChainRefs
  → MUT_RUN_EXIT=1；StructureEditTest.cpp(248): Value of: ws.design.defaultTcp.has_value()
    [FAILED] 1 test
git checkout -- → git status 净 → HEAD 2f14d1fc 不变 → 重建 → 全套件 375/375 复绿
  （FINAL_MDL_EXIT=0）

# 动作2c 变异2（F-799 值轨出口三重短路——产品面）
[CommandHandlers.cpp :1218 策略门／:1232 EvaluationFailed 门／:1242 blockers 门
 → if (false && …)  // MUTATION-ACC3-2 三处]
cmake --build … → MUT2_BUILD_EXIT=0
--gtest_filter=MdlCommandPrepare.MixedBadBounds…RejectsOnValueTrack
  → MUT2_RUN_EXIT=1；CommandHandlersTest.cpp(677): Expected equality of these values
    [FAILED] 1 test
git checkout -- → git status 净 → HEAD 2f14d1fc 不变 → 重建 → 全套件 375/375 复绿
  （FINAL_MDL2_EXIT=0）

# 动作2d 门禁与校验
cmake --build … --target ird_gates → GATES_EXIT=0
  自测 fail_r1/r4cliff/r3/r4/r5/t1/t2/sub 按预期检出（pass_clean/pass_r4comment/
  pass_r4cliff 通过）；末行「全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02
  零命中」（终端 GBK 乱码、内容可辨）
pwsh validate-docs.ps1  → VDOCS_EXIT=0；PASS (20 units, 12 trace entries, 317 task files)
pwsh validate-task.ps1 -TaskFile FIX-T1.json → VTASK_EXIT=0；PASS (1 tasks)

# 动作2e findings 机器验证（Windows 临时目录落盘后 pwsh 解析）
ConvertFrom-Json 双版合法：OLD_COUNT=815 NEW_COUNT=815
F-460 status=fixed fixedBy=audit-r2c 批 2fe3da66f（…验收 pass a17df640a；合入 c028c550d）
F-799 status=fixed fixedBy=F-746①（audit-r2d1 批 0e7cb3111…）＋FIX-T1 补钉
F-800 status=fixed fixedBy=缺陷修复批A FIX-T1（StructureEdit.cpp 同拍 reset）
F-495 status=open fixedBy=(no field)
F-550/551/552/553 ConvertTo-Json -Compress 严格比对 identical=True×4
findings diff hunk=3；增删行中条目 id 行零变动（F-496/F-801 仅上下文）

# 注释/红线扫描
modeling diff 新增行 TODO|FIXME|stub|placeholder|未实现|not implemented → 零命中
modeling diff include 行变动 → 零命中（R-1/R-2）；#include <Q…> → 零命中（L2）
```
