# ASM-STUDIO 验收记录（acc/asm-studio/1）

- 日期：2026-10-10
- 任务：ASM-STUDIO——studio 承接收口（三新域 unit 级边增登＋正式主程序装配＋分布钉扎消账）
- 验收者：独立对抗式验收会话（全新上下文，与实施者无共享历史）
- 送验对象：分支 `asm-studio`，HEAD = `3ebd2fb52114e73b890e9f9491a1b49f124f7e17`（origin/asm-studio 已核对一致，无分支漂移）
- 本任务无 canonical 契约 JSON（所有者授权 studio 承接批次任务，宿主接线最后一段）；验收标准以任务下发逐条标准（①~⑥）＋acceptance-protocol.md §4 通用清单为准
- **verdict：pass**（无阻断级问题；建议级问题见 §3）

---

## 1. 验收标准逐条核对（任务下发 ①~⑥）

### ① 白名单两表增登三行＋ird_gates 零命中 —— 满足

- `ird_gates_whitelist.cmake`（diff `pipeline/redesign-main..asm-studio`）：
  - `IRD_ALLOWED_UNIT_EDGES` 增登 `studio->dynamics` / `studio->selection` / `studio->optimization` 三行（既有 studio 五行 `studio->ui/project/modeling/kinematics/requirements` 同款形态，注内含 DTB §2.27 预登记行落地与 EXTRA_EDGE_REFS 出处登记）；
  - `IRD_EXTRA_EDGE_REFS_EDGES`＋`IRD_EXTRA_EDGE_REFS_NOTES` 同步三行三注（SA-18 D1 正式产品主程序目标、非 20 单元节点不入 dependency-graph.json、selection 诚实钉扎 P-SEL-3/P-PR-9 待裁决不本地绕过）；
  - `IRD_TEST_TARGET_EDGES` 增登 `sdurws_ird_ui_contract_test->sdurws_ird_dynamics/selection/optimization_plugin` 三行（标准③）。
- 验收标准所称"两表"按任务依据 = IRD_ALLOWED_UNIT_EDGES（:165 起）与 :251-255 所在表（实为 IRD_EXTRA_EDGE_REFS_EDGES）——两表均已增登。IRD_TARGET_LEVEL_EDGES（:544 起，目标级特权边表）既有形态中 studio 五行同款**从未登记**（studio 非单元节点走 unit 级表＋EXTRA_EDGE_REFS 出处，ui_app/ui_plugin 行不适用于 studio）——本批三行不登该表与既有形态一致，ird_gates 自跑零命中实证引擎无检出。
- **ird_gates 自跑**：`cmake --build build --config Release --target ird_gates`（构建树 = pipeline-wt 集成树，CMakeCache 确认指向 pipeline-wt 源码、RWS_BUILD_INDUSTRIALROBOT:BOOL=ON）→ EXIT=0，尾行「全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中」，引擎自测 9 例按预期检出（fail_r1/fail_t1/fail_r5/fail_sub/fail_r3/fail_r4/fail_r4cliff/fail_t2 检出＋pass 三例干净通过）。
- dependency-graph.json 零改动（diff 文件列表无该文件）——:312 口径（studio 非单元节点不入依赖图）成立。

### ② ui/CMakeLists studio 段链接三新域 plugin＋装配代码 UI-T23 同款扩展——满足

- `ui/CMakeLists.txt` diff：
  - `target_sources(sdurws_ird_studio ...)` 增编 `plugin/ExtraDomainAssembly.cpp`（与 ui_plugin 同源同 TU）；
  - `target_compile_definitions(sdurws_ird_studio PRIVATE IRD_UI_PLUGIN_EXTRA_DOMAINS=1)`（与 ui_plugin 单一书写点防漂移同款）；
  - `target_link_libraries(sdurws_ird_studio ...)` 增链 `sdurws_ird_dynamics_plugin` / `sdurws_ird_selection_plugin` / `sdurws_ird_optimization_plugin`；
  - ui_plugin 段注释同步（ExtraDomainAssembly.cpp 编入目标登记更新）。
- 装配代码：`DomainAssembly.cpp` 尾段三新域承接段由编译定义区分的机制不变（UI-T23 同机制推广）；`ExtraDomainAssembly.{hpp,cpp}` 文件头编入目标登记随代码更新（两宿主目标＋测试直编面）。
- **构建**：增量构建六目标（sdurws_ird_studio / ui_plugin / ui_contract_test / ui_test / workflow_test / workflow_contract_test）→ `cmake --build build --config Release --target ...` EXIT=0，`grep -cE ": error|: fatal error"` 计数 0。流水线已复验的全量集成构建与冒烟构建未重复执行（任务口径）。
- 六域在正式主程序装配齐备的无人值守钉扎 = 新用例六域聚合注册断言（见③）＋ui_plugin（MODULE）运行期验证维持 asm-ui 登记形态（§12.2 实机装载，归所有者人工流程——已如实登记于 execution-log §6）。

### ③ 分布钉扎消账——满足

- `IRD_TEST_TARGET_EDGES` 增登 ui_contract_test→三新域 plugin 三行（ird_gates 零命中包含该三边按登记放行）。
- 六域聚合注册断言补全：`HostIntegrationContractTest.cpp` 新增 `SixDomainHostAssemblySequence_Snapshot_ASMSTUDIO_ACC1`（+197 行）——三旧域 registerPluginUi＋三新域 assembleExtraDomains 经同一真实 IPluginUiRegistrar；断言强度逐段核对（对照 `ExtraDomainAssembly.cpp` 实现 L28-115）：
  - statuses 恰三条白名单序（dynamics/selection/optimization）＋statusOf 公共接口消费（未登记域键 = nullptr）；
  - dynamics/optimization ok=true、selection ok=false（InvalidDescriptor 诚实拒绝如实承载——P-SEL-3 待裁决，零本地绕过）；
  - applyEntries 恰两条（selection 拒绝不入表——§10.9/§11.3）；
  - 聚合快照恰五条按白名单序入列＋panels/commands 计数与描述符一致＋新域 DuplicatePlugin 拒绝不增长快照＋报告行携带 §11.3 稳定码 UI-PLUGIN-ASSEMBLY-FAILED。
- 用例带 IRD_TEST_INFO 追溯（SA-01/UX-14）＋注释引用 §10.9/§11.1/§11.3/acc/asm-ui/1 建议级 1——用例名与断言对得上验收语义。
- MODULE 不可链的结构性限制保留 asm-ui 登记形态并如实登记（CMakeLists 注释、ExtraDomainAssembly.hpp 落位边界、execution-log §6、ui.md §13——四处一致）：ui_plugin 的 UiPlugin::initialize 装配全链不经测试目标直测（assembleDomainPlugins 需 QDockWidget）；ui_test 零消费零增登（不虚构测试面——IRD_TEST_TARGET_EDGES 未增 ui_test 行，CMakeLists ui_test 段未触碰）。

### ④ WP-16-T08~T13 六契约 status ready→blocked——满足

- 六文件 diff 均 `"status": "ready" → "blocked"`；结构化核对（python 逐文件解析）六卡 note 三要素齐备（依据＝dependsOn 含 blocked 的 WP-16-T07/T08/T09/T12 传递链、出处＝acc/gov-doc/1 建议 S-2、解冻条件＝WP-16-T02 完成 P-06 数值冻结后恢复 ready）。
- 前提链实读确认：WP-16-T07 status=blocked（dependsOn 含 blocked 的 WP-16-T02；T02 status=blocked）——传递受卡语义成立（acc/gov-doc/1 建议 S-2 消账兑现）。

### ⑤ units/ui.md 收口登记＋DTB §2.27 消账——满足

- `units/ui.md`：新增版本行 v1.91（2026-10-10 ASM-STUDIO studio 承接收口登记）＋§13 增 ASM-STUDIO 行（三边增登＋studio 承接＋分布钉扎消账＋六契约状态修正）＋§16.7 变更历史行。
- `development-task-breakdown.md`：v0.57→v0.58 头部版本行＋§2.27「studio 承接批（预登记后续）」→「studio 承接批（ASM-STUDIO）」——状态从「预登记（待所有者指令排入）」改为「已实施（2026-10-10，待验收）」，范围/文件/依据列同步（含六契约状态修正）。
- **validate-docs 自跑**：`pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1`（pipeline-wt 任务分支文档）→ EXIT=0，PASS（20 units / 12 trace entries / 312 task files）。

### ⑥ 门禁＋回归基线＋测试真实运行留痕——满足

- ird_gates 零命中（见①自跑）。
- workflow 回归基线：`sdurws_ird_workflow_test` 193 / `sdurws_ird_workflow_contract_test` 77——与 asm-ui 批后基线一致（asm-wf 批留痕 191＋asm-ui 批 HostAdapterTest 实增 2＝193，DTB 勘误包登记口径吻合）。
- **验收自跑四套**（集成树 `build/RobWorkStudio/bin/Release/` 直接运行 exe，PATH 前置 D:/software/Miniconda3（DTB §5.1 python313.dll 前置），`--gtest_output=xml:`＋`--ird_report=` 双 report 口径）：
  - sdurws_ird_workflow_test：EXIT=0，**193 tests, 0 failures**；
  - sdurws_ird_workflow_contract_test：EXIT=0，**77 tests, 0 failures**；
  - sdurws_ird_ui_test：EXIT=0，**255 tests, 0 failures**；
  - sdurws_ird_ui_contract_test：EXIT=0，**34 tests, 0 failures**（= 基线 33＋本批新增 1；自跑 XML 内 `HostIntegrationContractTest::SixDomainHostAssemblySequence_Snapshot_ASMSTUDIO_ACC1` PASSED 实证）。
  - 与实施者留痕（traceability/gtest-reports/asm-studio/ 八 XML＋八 JSON，git 已提交）逐套一致（XML 顶层 testsuites 属性解析：集成 34/255/77/193＋冒烟 22/240/77/193，failures 全 0）。
- ui_gui_test 不在无人值守门禁、RobWorkStudio 真实装载与界面人工点验归所有者人工流程——execution-log §6 如实登记（本批零 GUI 呈现面改动，gui_test 目标构建通过）。

## 2. 验收清单（ask 清单 1~8）逐项证据

1. **改动范围**：`git diff pipeline/redesign-main..asm-studio --stat`＝32 文件（+39807/−61）：白名单 1＋ui/CMakeLists 1＋contract_test 1＋plugin 4＋DTB 1＋六契约 6＋留痕 17（execution-log＋gtest-reports 八 XML 八 JSON）＋ui.md 1——全部落在任务依据所列 scope（白名单两表、ui/CMakeLists、分布钉扎测试面、六契约、ui.md＋DTB、留痕）内，无越界、无 forbidden 面、无框架源码改动（SA-02：patches/ 无新文件）。装配源码 4 文件＝标准②③装配扩展与注释随代码更新的义务文件，在 scope 内。
2. **红线**：diff 新增行扫描——零 Qt include 命中（本批仅改 ui 单元文件，计算库零触碰）；新增 include 仅 2 行且均为 ui 单元内部装配面私有头（`ExtraDomainAssembly.hpp`/`plugin/ExtraDomainAssembly.hpp`——同单元装配面，非跨单元私有头）；三域消费面仅公共 assembly/ 门面头（既有行，本批未新增）；RobWork 名称前缀拼接/剥离模式（strip/replace/startsWith("RobWork")/字符串拼接等）零命中；业务域互链零（三新域→ui 边为 ASM-PLUG 批既有登记，本批零新增域间边）；ird_gates 自跑 R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中。
3. **注释规范**（抽查 4 处，合格）：①文件头/落位边界（ExtraDomainAssembly.{hpp,cpp}——编入目标登记＋第三消费面＋MODULE 不可链边界，登记 units/ui.md §13 出处）；②错误语义归类（测试注释「selection 预期 InvalidDescriptor 诚实拒绝——P-SEL-3/P-PR-9 待所有者裁决面，零 token 改写零本地绕过」＋「成功态不携带失败原因（错误语义归失败态）」＋稳定码 UI-PLUGIN-ASSEMBLY-FAILED）；③所有权/生命周期（DomainAssembly.hpp extraAssemblies 注——「shared_ptr<void> 的删除器在构造点绑定实型——析构安全；存活期随 bundle（宿主持有至壳拆除——§10.9 所有权行）」）；④CMakeLists 关键步骤中文注释块（IRD_UI_PLUGIN_EXTRA_DOMAINS 单一书写点防漂移段）。测试用例逐步注释（①~⑨ 段）＋需求/AT 追溯字段。
4. **偷懒扫描**：diff 新增行 TODO|FIXME|stub|placeholder——源码（.cpp/.hpp/.txt/.cmake）零命中；留痕 JSON/XML 中的 "Placeholder" 命中均为既有测试用例名（"诚实占位"语义测试，非偷懒标记）。无空实现（assembleExtraDomains 全实现，L28-155 实读）。
5. **测试真实性与强度**：四套自跑计数与实施者留痕逐套一致（见⑥）；做红双变异复现（见 §4）——测试非恒真，selection 诚实拒绝与 dynamics 登记状态两条核心断言真实具备失败能力；用例带追溯字段、断言校验行为与边界（快照序/计数/拒绝分支/隔离语义）。
6. **验收标准逐条核对**：见 §1（本任务无 canonical 契约，无 knownPitfalls/note 豁免核对项；任务依据中"流水线 notCovered 项"即本批消账对象，已随 §2.27 落地）。
7. **提交质量**：单提交 `3ebd2fb5`，标题 `[ASM-STUDIO] 动词开头中文摘要`；正文四段（改动内容/改动原因/验证/影响面）齐备，验证段与留痕一致（计数、门禁、做红、未执行事项均如实——含「ui_gui_test 未运行」「真实装载归所有者人工流程」的如实登记，无粉饰）；`git log origin/asm-studio -1` ＝ 3ebd2fb5（已推送）；gtest XML＋JSON 留痕在 `traceability/gtest-reports/asm-studio/` 且随提交入库；pipeline-wt 工作树 `git status --porcelain` 干净（做红复现后复核亦干净——零残留）。
8. **主仓库零污染**：`git -C RobWork diff --name-only`＋`status --porcelain`——仅 ui-t74 三个旧留痕文件（console-tour.log/driver-verdict.log/tour-5-final-state.png，属 ui-t74 批非本任务）与既有未跟踪目录（.acc-pipeline/.zcode/audit 等）；units/ui.md、DTB、六契约等 allowedFiles 路径在主仓库**零未提交修改**——文档登记全部随任务分支交付（WP-18-T03 教训未重演）。

## 3. 问题清单

**阻断级：无。**

**建议级（不阻塞合入；按协议 §5 转登 findings.json）：**

- S-1（文档口径微瑕）：任务依据将白名单 `:251-255` 称为「目标级表」，该行域实为 `IRD_EXTRA_EDGE_REFS_EDGES`（出处登记表）；真正的目标级表 `IRD_TARGET_LEVEL_EDGES`（:544 起）本批未登记 studio 行——该形态与既有 studio 五行完全一致（studio 非单元节点不经目标级表）且 ird_gates 零命中实证无检出，故不构成偏差，仅建议后续批次引用行号/表名时对齐白名单实际表名，避免误读为「studio 应登 IRD_TARGET_LEVEL_EDGES 而未登」。
- S-2（既有缺陷，非本批引入）：ctest 注册树列表为 0 的既有缺陷仍未触碰（execution-log §6 已如实登记，测试一律以直接运行 exe＋`--ird_report` 双 report 口径为准）——保持 open，归属原登记责任方。

## 4. 测试真实失败能力（协议 §4.5/4.11④ 自做红复现）

验收者独立构造双变异（对照实施者 M1/M2 同面，即改即恢复）：

- **M1**：`ExtraDomainAssembly.cpp` L53 dynamics `status.ok = (outcome == RegistrationOutcome::Ok)` → 强制 `false` → 重建 sdurws_ird_ui_contract_test → `--gtest_filter=*ASMSTUDIO*` 运行 → **FAILED 1**（EXIT=1）——`EXPECT_TRUE(dynamicsStatus->ok)` 等断言真实生效；
- **M2**：同文件 L94 selection `status.ok = ...` → 强制 `true`（伪造诚实拒绝登记成功）→ 重建 → 同过滤运行 → **FAILED 1**（EXIT=1）——selection ok=false 断言与 applyEntries==2 断言真实生效；
- 恢复后：`git status --porcelain`＋`git diff` 零残留；全量重跑 `sdurws_ird_ui_contract_test` → **34/34 PASSED**（EXIT=0）。

## 5. 复现命令（验收者实跑）

```
# 送验对象（pipeline-wt 工作树，分支 asm-studio @ 3ebd2fb52114e73b890e9f9491a1b49f124f7e17）
git -C <pipeline-wt> rev-parse HEAD
git -C <pipeline-wt> diff pipeline/redesign-main..asm-studio --stat
git -C <pipeline-wt> log origin/asm-studio -1

# 增量构建（零错误）
cmake --build <pipeline-wt>/build --config Release --target sdurws_ird_studio sdurws_ird_ui_plugin sdurws_ird_ui_contract_test sdurws_ird_ui_test sdurws_ird_workflow_test sdurws_ird_workflow_contract_test

# 门禁（零命中）
cmake --build <pipeline-wt>/build --config Release --target ird_gates

# 四套测试（PATH 前置 D:/software/Miniconda3；双 report 口径）
cd <pipeline-wt>/build/RobWorkStudio/bin/Release
./sdurws_ird_workflow_test.exe --gtest_output=xml:<acc>/acc_workflow_test.xml --ird_report=<acc>/acc_workflow_test.json
./sdurws_ird_workflow_contract_test.exe --gtest_output=xml:<acc>/acc_workflow_contract_test.xml --ird_report=<acc>/acc_workflow_contract_test.json
./sdurws_ird_ui_test.exe --gtest_output=xml:<acc>/acc_ui_test.xml --ird_report=<acc>/acc_ui_test.json
./sdurws_ird_ui_contract_test.exe --gtest_output=xml:<acc>/acc_ui_contract_test.xml --ird_report=<acc>/acc_ui_contract_test.json

# 文档验证
pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1
```

自跑结果：构建 EXIT=0（error 计数 0）；ird_gates EXIT=0（R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中＋引擎自测 9 例按预期检出）；四套 193/77/255/34 全零失败；validate-docs PASS。

## 6. 未执行事项（如实登记）

- 全量集成构建与独立冒烟构建未重复执行（任务口径：流水线已用确定性命令复验通过；验收者以增量构建零错误＋四套自跑替代复现——增量构建覆盖本批全部六相关目标）。
- ui_gui_test 未运行（不在无人值守门禁——ui.md §12.2 串行通道；本批零 GUI 呈现面改动）。
- RobWorkStudio 真实装载与界面人工点验未执行（归所有者人工流程——execution-log §6 同口径如实登记）。

## 7. 验收者环境说明

- 复现现场：D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt（分支 asm-studio @ 3ebd2fb5，验收全程工作树零改动——做红变异即改即恢复，git 证零残留）。
- 构建树：pipeline-wt/build（CMakeCache：CMAKE_HOME_DIRECTORY=pipeline-wt/RobWork、RWS_BUILD_INDUSTRIALROBOT:BOOL=ON）。
- 本记录按协议 §3 v1.4 落于独立记录分支 acc/asm-studio/1（基于 origin/redesign-main＝b71dbaee1），推送后即清理记录工作树。
