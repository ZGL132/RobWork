# GOV-GATE 治理批次——门禁工具修复执行留痕

- 日期：2026-10-10
- 分支：`gov-gate`（宿主 `redesign-main` 任务分支）
- 范围：所有者授权治理批次一（无 canonical 契约 JSON，权威依据＝各批次
  验收登记的治理义务）——①ird_gates R-4 注释剥离正则悬崖（F-570 悬崖，
  wp22-t08／workflow.md §14 登记段同源）修复＋自测回归；②gate-all.ps1
  头注释运行时长标注（wp22-t13 S2）；③workflow LifeCycleMainlineContract
  死 include 移除（wp22-t13 S1）；④ui/modeling 剪贴板类 GUI 用例重试半区
  （wp17-t08 建议①——skip 半区已由 F-617/F-620 修复批落地，本批补齐）。

## 0. 改动文件清单（allowedFiles 之内）

| 文件 | 改动 |
| --- | --- |
| `industrialrobot/cmake/ird_gates.cmake` | R-4 块注释剥离正则 `/\*([^*]|\*[^/])*\*/` 替换为语义逐字符等价的确定性线性扫描 `ird_strip_block_comments`（无回溯无深递归）＋RobWork 快路径守卫＋自测新增 pass_r4cliff/fail_r4cliff 两例（9→11 例） |
| `scripts/industrialrobot/gate-all.ps1` | 头注释增补"运行时长与构成"段（wp22-t13 S2——名似静态门禁实含冒烟全量构建约 10+ 分钟）——纯注释，零行为变化 |
| `industrialrobot/workflow/contract_test/LifeCycleMainlineContractTest.cpp` | 移除死 include `<QString>`（wp22-t13 S1——全文件零 QString 使用，grep 复核零命中） |
| `industrialrobot/ui/gui_test/ParamTablePanelGuiTest.cpp` | `clipboardUsable()` 探针补 bounded 重试窗口（首轮＋2 次重试，wp17-t08 建议①重试半区；skip 半区 F-617/F-620 既有） |
| `industrialrobot/modeling/gui_test/ModelingPanelGuiTest.cpp` | 同款重试半区（与 ui 侧同形复制——既有 harness 惯例） |
| `doc/industrial-robot-design/traceability/gtest-reports/gov-gate/` | workflow 两套件 gtest XML 留痕 |
| `doc/industrial-robot-design/traceability/builds/gov-gate/` | 本日志＋悬崖前后/red-green 原始日志＋等价性探针脚本与输出 |

## 1. F-570 引擎修复——等价性验证（检测语义零变化）

方法：修复前管线（行注释正则＋`string(REGEX REPLACE "/\\*([^*]|\\*[^/])*\\*/" " ")`）
与修复后管线（同款行注释正则＋`ird_strip_block_comments` 线性 DFA）逐例
比对【剥离输出字节串】与【R-4 判定面 MATCHALL 结果】。探针脚本随留痕存档
（`proto.cmake`），输出（`equivalence-probe-output.txt`）：

- 刁钻语料 32 例（奇偶星串收尾 `/*a***/` vs `/*a**/`、未闭合块、贪心吞并、
  引号内 `/*`、`//` 与块注释交叠、`/* /* */` 嵌套形态等）：不一致 0 例；
- 320 星行长注释（含 RobWork 注释性提及）：剥离输出一致；
- 未闭合 320 星串块：一致；
- 真实产品面全集 541 文件（industrialrobot 20 单元 include/**＋src/**）：
  剥离输出不一致 0 文件、判定面不一致 0 文件。剥离后仍含 RobWork 引号
  字面量的 6 文件（探针逐一定位）＝runtime 5 文件（R-4 整单元例外域）＋
  `project/src/ProjectStoreImpl.cpp` 1 文件（IRD_R4_EXCEPTION_FILES 豁免
  册登记，WP-22-T02 回填）——两者豁免面均在本修复未触碰的扫描前置逻辑，
  新旧判定逐文件一致，与全树零命中自洽。

全树级判定面复核：修复前引擎（`git show HEAD:...ird_gates.cmake` 提取，
`real-tree-gate-OLD.log`）与修复后引擎（`real-tree-gate.log`）各跑真实树
一遍，均 exit=0、`全部检查通过：…零命中`——R-4 不扩大不缩小在全树口径成立。

## 2. F-570 悬崖——修复前后对照（同形态文件实测）

触发形态＝wp22-t08 事故同款：单一连续块注释约 300 星行（Lifecycle.hpp
头注释当时被拆为 1-128/129-301 两块规避；本验证将其合并回单一连续块复刻
事故形态，297 星行、178KB 文件）＋合成夹具（320 星行×127 字节注释体
40.6KB）。实测（`cmake -DIRD_ROOT=<夹具> -DIRD_GATE_CHILD=ON -DIRD_ENABLE_
GIT=OFF -DIRD_GRAPH_FILE= -P <引擎>`，日志随留痕存档）：

| 夹具 | 修复前引擎 | 修复后引擎 |
| --- | --- | --- |
| 合并真实形态（lifefixture，297 星行/43KB 注释体） | **exit=127（栈溢出进程死亡）** `old-engine-life.log` | exit=1（正常走完扫描报夹具缺公共头 R2 命中）`new-engine-life.log` |
| 合成 320 星行×127B（scale320，40.6KB） | **exit=127** `old-engine-scale.log` | exit=0 零命中 `new-engine-scale.log` |
| 合成 320 星行短行（11.5KB 注释体） | exit=0（低于本机栈阈值——悬崖阈值的下界数据点）`old-engine-cliff.log` | —（同上已覆盖） |

退出码 127 与任务依据"栈溢出进程退出 127"一致；修复前引擎进程死于扫描
中途（R-2 命中打印于 R-4 剥离之前、第 8 步汇总未达），修复后引擎全流程
正常完成。

## 3. 引擎自测悬崖回归用例（9→11 例）＋红灯锚定

新增两例（夹具由 `string(REPEAT)` 生成 320 星行、注释体约 44/50KB——
高于本机实测崩溃阈值 40KB，保证对修复前形态的复现能力）：

- `pass_r4cliff`：≥300 星行连续块注释且注释内含 RobWork 字样 → 不崩且
  必须通过（防误报半区）；
- `fail_r4cliff`：同形态长块注释＋代码面 `"RobWork"` 字面量 → 不崩且
  必须命中 IRD-GATE-R4（检出能力不因悬崖形态劣化）。

红灯锚定（修复前引擎跑新自测夹具，白名单已并置 `old-ird_gates.cmake`）：
`pass_r4cliff` exit=**127**、`fail_r4cliff` exit=**127**（`red-pass-case.log`/
`red-fail-case.log`）——两夹具对修复前引擎确实致命；修复后引擎在同一对
夹具上 pass 例 rc=0、fail 例按预期检出 IRD-GATE-R4（见第 4 节自测 11 例）。

## 4. ird_gates 门禁（真实执行，零命中）

```
cmake --build <wt>/build --config Release --target ird_gates     # exit=0
```

输出尾行：`[ird_gates] 全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/
GRAPH/LIB/SA02 零命中`；引擎自测 **11/11**：pass_clean/pass_r4comment/
pass_r4cliff 干净通过，fail_r4cliff/fail_r1/fail_t1/fail_r5/fail_sub/
fail_r3/fail_r4/fail_t2 按预期检出（真实树直跑同口径，`real-tree-gate.log`）。

## 5. 集成模式构建与测试（真实执行）

配置（增量）：`cmake -S <wt>/RobWork -B <wt>/build -G "Visual Studio 17
2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON`（缓存确认
`RWS_BUILD_INDUSTRIALROBOT:BOOL=ON` 在位）。

- `sdurws_ird_workflow_test_report`：构建＋执行 exit=0，gtest XML
  **193/193 全绿**（0 failure/0 error/0 skip；
  `gtest-reports/gov-gate/sdurws_ird_workflow_test.xml`）；
- `sdurws_ird_workflow_contract_test_report`：构建＋执行 exit=0，gtest XML
  **77/77 全绿**（0 failure/0 error/0 skip；含 LifeCycleMainlineContract
  1/1——死 include 移除后的文件真实编译执行通过；
  `gtest-reports/gov-gate/sdurws_ird_workflow_contract_test.xml`）；
- `sdurws_ird_ui_gui_test`：构建 exit=0（剪贴板重试半区编译通过）；
- `sdurws_ird_modeling_gui_test`：构建 exit=0（同上）。

## 6. 诚实边界（未执行项）

- **独立冒烟模式构建未执行**：本批三个 C++ 改动文件（LifeCycle 契约测试
  ＋两 GUI 测试）均已在集成模式真实编译并（workflow 两套件）真实执行；
  验收口径（acceptance ⑥）未含冒烟项，工时预算内未重配全新冒烟树。
- **GUI 测试未运行**：`ui_gui_test`/`modeling_gui_test` 仅构建通过——
  GUI 实机运行不在无人值守范围（asm-plug 批同口径先例）；剪贴板重试
  半区的运行时行为（瞬态占用重试）无本机实测，其正确性论证见代码注释
  （bounded 窗口＋耗尽仍 skip 归因，不吞真失败）。
- gate-all.ps1 改动为纯头注释，未重跑该脚本全量验证（其行为零变化）。

## 7. 主仓库污染自查

`git -C "D:/10_Source_Repos/21_robot/RobWork" status --porcelain` 在本任务
开始前后均只有 ui-t74 批次遗留 smoke-tour 留痕与非跟踪目录（.acc-pipeline/
等）——本任务零新增条目（交付前复核见提交记录）。
