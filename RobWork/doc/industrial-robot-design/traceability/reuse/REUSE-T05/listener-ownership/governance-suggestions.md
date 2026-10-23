# F-958 共享治理补齐片段（交协调会话唯一修改者落地——本包未直接编辑共享文件）

来源：分支 `codex/reuse-testkit-listener`，交付提交 `fbd51e76528a13cfab80987087d7b1b9a58e9182`（基线 `6fb26b31d`）。以下为最小补齐片段；catalog、permissions **不需要变更**（本包未新增能力、未改变权限面——功能条目 usageRole/capabilityId/permissionRef 全部保持原值），不为门禁转绿补造条目。

## 0. 授权与阶段核对（本子包 × REUSE-T05）

- F-958（findings.json，owner `testkit / REUSE-T05`）resolution 明文预定本修复："T05在现有安装函数内最小传递所有权并同步注释，原testkit/子进程/Debug退出回归，不新增监听器平台"——实施范围与预定一致，无扩展。
- REUSE-T05.json（父任务，status=planned）note 已载："所有者2026-10-23批准migration §12.11能力准入；父任务仍planned，具体能力须精确子契约/审计/权限/生产证据/整包独验"。migration.md §12.11："能力及其依赖完成审计/权限确认即可按精确任务实施；完整能力包独立验收后放行依赖能力"。
- 据此：本子包以 F-958 resolution 为精确范围凭证；父任务与 testkit 单元均**不标完成**；逐文件凭证（§3）不替代逐功能证据（§5 的功能条目更新仍是 pending，待独验）。

## 1. findings.json —— F-958 进展注记建议（**不标 closed**）

F-958 状态保持 `open`；`resolution` 建议追加进展注记：

> 2026-10-23 实施收口（codex/reuse-testkit-listener fbd51e765）：installTestRecordListener 已改堆对象经 Append 移交（gtest.h:1032 公开契约），新增常驻契约回归＋lsnchild 三态子进程 e2e；legacy 反例 Debug CRT 复现非法 delete 诊断（debug_heap.cpp 904/908），Release 静默。定向测试通过；ird_gates 首跑命中 lsnchild 白名单登记 2 处（待协调登记后复跑），validate-reuse 待凭证整合。独验通过后方可关闭；29 处跨单元注释订正随协调批次。

## 2. 29 个跨单元测试 main 错误注释订正建议（非本分支修改，维持 §2 v1 清单）

文件与行号清单及统一替换文本见本目录 v1 记录（git 历史）或下表摘要——同一错误声明（"gtest listeners 不接管所有权"）位于：drivetrain/contract_test:28、drivetrain/test:63、dynamics/contract_test:28、dynamics/test:30、evidence/test:26、execution/contract_test:47、execution/perf_test:25、execution/test:34、io/test:38、kinematics/test:34、modeling/test:38、optimization/contract_test:30、optimization/perf_test:27、optimization/test:31、policy/test:26、project/perf_test:27、project/test:35、reporting/contract_test:38、reporting/test:41、requirements/test:35、runtime/test:26、selection/contract_test:28、selection/test:30、trajectory/contract_test:29、trajectory/test:30、ui/perf_test/TestMainLargeDataPerf.cpp:27、ui/perf_test/TestMainPerf.cpp:37、ui/test:45、workflow/test:32（基线 6fb26b31d 行号）。统一替换文本：

```cpp
    // 监听器所有权按 gtest 公开契约移交：Append 接管并在测试程序结束时释放
    // （gtest/gtest.h TestEventListeners::Append 注释；F-958 修复后为堆对象，
    // 静态/栈对象地址交 Append 属非法 delete）。
```

必须更新：29 处注释与修复后实现直接矛盾。无需更新：kinematics/gui_test、ui/gui_test、ui/contract_test 等 main 注释仅描述报告并存无所有权声明；units/testkit.md、testkit README 无错误声明（已核）。

## 3. reuse/changes/REUSE-T05.json —— 逐文件变更凭证（新建文件，schema ird-reuse-change/1）

```json
{
  "schemaVersion": "ird-reuse-change/1",
  "taskId": "REUSE-T05",
  "taskFile": "RobWork/doc/industrial-robot-design/tasks/REUSE-T05.json",
  "files": [
    {
      "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/include/sdurws/ird/testkit/gtest/RecordListener.hpp",
      "sha256": "3aca91019593046c2650ab6bc5f70de97e4d85ae6a8fd7debe62713137cae8ca",
      "mode": "adapter",
      "capabilities": ["TEST-HARNESS"],
      "boundary": "F-958：installTestRecordListener 按已安装 gtest 1.18.0 公开契约（gtest.h:1032 Append 接管并 delete）改为堆对象移交，删除静态对象与错误所有权注释；--ird_report 摘除/缺省路径/OnTestProgramEnd 写盘时序/每进程一次契约/函数签名零变化；无新公共接口。",
      "verify": "RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T05/listener-ownership/README.md"
    },
    {
      "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/src/Report.cpp",
      "sha256": "b474b55ac9f65e2b55c1abaf936397c7c87ee334e56572a9ce61e1b911fcba54",
      "mode": "business",
      "capabilities": [],
      "boundary": "TestRecordStore::instance() 注释中'listener 泄漏式注册同型'失真引用订正；静态单例职责与实现零变化。",
      "verify": "RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T05/listener-ownership/README.md"
    },
    {
      "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/CMakeLists.txt",
      "sha256": "bdf5362e81df46b89bb4045a7244389245ae971d761a6ecc757f2b4920a3ea0d",
      "mode": "business",
      "capabilities": [],
      "boundary": "注册 ListenerOwnershipTest.cpp 入 sdurws_ird_testkit_test；新增 sdurws_ird_testkit_lsnchild 工具目标（other 形态，链 testkit＋GTest::gtest，输出目录对齐测试目标，DLL POST_BUILD 拷贝，add_dependencies 挂测试目标）；依赖红线配置期守卫（T-2/R-3/D-06）原文未动；工具边白名单登记随 §7。",
      "verify": "RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T05/listener-ownership/README.md"
    },
    {
      "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/test/ListenerOwnershipTest.cpp",
      "sha256": "9ec6ff8a5dab7cbb222ccdf806d20b4f7b61b4174af02808aaef44b7125e18bf",
      "mode": "business",
      "capabilities": [],
      "boundary": "F-958 定向用例（UT_LSN 五用例）：Append 单次析构/Release 返还所有权（进程内局部集合）＋lsnchild passed/failed/skipped 三态子进程 e2e（报告唯一且落 --ird_report 指定路径、退出码 0/非0/0）。",
      "verify": "RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T05/listener-ownership/run-testkit-release/gtest.xml"
    },
    {
      "path": "RobWork/RobWorkStudio/src/rwslibs/industrialrobot/testkit/tools/lsnchild/main.cpp",
      "sha256": "3f22ae4b21d7ff88dc1c09e263d903626f96697a72f47398d789ad1b8bda5188",
      "mode": "business",
      "capabilities": [],
      "boundary": "监听器所有权测试子进程（prochelper 先例，tools/ 不入产品安装面）：经生产安装入口跑真实 gtest 三态样本；--lsn_mode=legacy 为旧实现反例取证载体（非产品 fallback、非第二报告路径）。",
      "verify": "RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T05/listener-ownership/README.md"
    }
  ]
}
```

sha256 均为 `fbd51e765` 提交内容按 git show 管入 sha256sum 的整文件哈希（与 validate-reuse 报告的 bdf5362e… 口径一致）。

## 4. tasks/REUSE-T05.json —— note 追加建议（父任务保持 planned）

> 2026-10-23 F-958 监听器所有权子包实施于 codex/reuse-testkit-listener（fbd51e765）：定向测试通过；ird_gates 首跑命中 lsnchild 工具边白名单登记 2 处、validate-reuse 待 §3 凭证整合，均待协调落地后复跑；独立验收未开始。父任务与 testkit 单元不因此标完成。

## 5. reuse/functions/testkit.json —— 实际受影响条目（2 条，decision/status 保持 pending）

逐文件凭证不替代逐功能证据——以下仅刷新绑定与登记实施状态，功能关闭仍待独验：

### 5.1 `testkit.installTestRecordListener`（约 line 913）

- `sources[0].startLine`：120 → **125**；`endLine`：142 → **150**；`sha256`：a1934b15… → **1fca428b1dceda03e5dc7c526a5d8f6d7fb7fa00b0a7af68753db5fd17b6c5b4**（新文件行 125-150 区间哈希，提取规则已按基线行 120-142 复现验证）。
- `differences` 更新为："已按 gtest.h Append 公开契约堆对象移交、gtest 集合测试程序结束统一释放（fbd51e765）；--ird_report/缺省路径/写盘时序/每进程一次契约零变化；29 处消费者注释订正与门禁转绿随协调批次；待独立验收"。
- `inputs[0].sha256`：64fa9739… → **bdf5362e…**（testkit/CMakeLists.txt 新整文件哈希）。
- `auditEvidence` 追加：

```json
{ "path": "RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T05/listener-ownership/run-testkit-release/gtest.xml", "sha256": "3449296617c7ca835a950f5ec492f94225946d58cd51f5af375763bd71a80df5" },
{ "path": "RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T05/listener-ownership/run-testkit-release/ird-test-report.json", "sha256": "67d25337492023a828b2751a546a0fdf4d2055d49c933aa32c051ecc149f70bc" },
{ "path": "RobWork/doc/industrial-robot-design/traceability/reuse/REUSE-T05/listener-ownership/legacy-counterexample/debug/run.log", "sha256": "57e5638e5ce6d1043ff3156dfa7572154a3248f07e4764555a7154cab7f3e3eb" }
```

### 5.2 `testkit.TestRecordListener`（约 line 854）

- `sources[0]` **不变**：类定义行 44-108 与基线逐字节一致（区间哈希 6bf2fb80… 复算验证通过）。
- `inputs[0].sha256`：64fa9739… → **bdf5362e…**。
- `auditEvidence` 追加同 §5.1 三条。

### 5.3 机械性失真披露（协调者裁决，不在本包语义影响内）

functions/testkit.json 其余约 20 条目 `inputs[]` 同样 pin testkit/CMakeLists.txt 旧哈希 64fa9739…，因本包 CMake 增列而机械失真；功能语义未变。是否批量刷新由协调者按其批次纪律决定——本包不代刷新、不隐瞒该事实。functions/testkit.json 中无 Report.cpp 区间 pin，§3 的 Report.cpp 注释订正零波及（已核）。

## 6. traceability/reuse/REUSE-T01/test-build-inventory.json —— 唯一受影响 pin

- 库存无监听器条目、无 prochelper/lsnchild 收录位（结构＝36 mains＋宏组）；36 个 main 的 source 区间全部未变（本包零 main 修改）；ird_add_gtest 宏分析未变。
- 唯一失真：`mains[]` 中 testkit 条目（testkit/test/TestMain.cpp，约 line 635-638）的 `cmake.sha256`：64fa9739… → **bdf5362e…**。该条目 `source`（main 行 24-33，9f050581…）不变。

## 7. cmake/ird_gates_whitelist.cmake ＋ DTB §4.5 —— lsnchild 工具边登记（ird_gates 首跑真实命中）

首跑证据：ird-gates-run1.log（摘要 ird-gates-run1.summary.md）。命中＝本包新增子进程辅助目标的链接边未登记——testdata_lint（TK-T03）同型登记路径，非放宽：

```cmake
# IRD_TOOL_TARGET_EDGES 增列（含注释行）：
#   - sdurws_ird_testkit_lsnchild：testkit 单元监听器所有权测试子进程（F-958
#     交付——tools/lsnchild，经生产安装入口跑三态样本的隔离子进程载体；
#     testkit 同生命周期，不入产品分发面，同 testdata_lint T-1 纪律）
"sdurws_ird_testkit_lsnchild->sdurws_ird_testkit"

# IRD_T1_EXEMPT_TARGETS 增列：
set(IRD_T1_EXEMPT_TARGETS "sdurws_ird_testdata_lint"
                          "sdurws_ird_testkit_lsnchild")
```

同步 development-task-breakdown.md §4.5 人工登记册对应行（白名单与登记册两处必须一致——ird_gates 目标注释原文约定）。

## 8. 门禁与验收状态（四态区分）

| 状态 | 结论 |
| --- | --- |
| 实施完成 | ✅ fbd51e765（最小所有权修复＋定向测试＋注释订正） |
| 测试通过 | ✅ 定向范围：testkit_test Release/Debug 115/115、core_test 69/69、legacy 反例取证（Release 静默/Debug CRT 复现）——均为真实执行留痕 |
| 门禁通过 | ❌ 未通过：ird_gates 首跑红线命中 2 处（lsnchild 工具边登记缺失）；validate-reuse 段未执行（其单独首跑的首个阻断＝REUSE-UNDECLARED 凭证未整合）。均待协调登记后复跑，未放宽任何门禁 |
| 独立验收完成 | ❌ 未开始——由所有者另开独立会话执行，实施者不自签 |

## 9. 协调落地结论（2026-10-23，协调会话唯一修改者追加）

本草案已由共享治理唯一修改者逐项核实后落地为 **REUSE-T18 精确能力子任务**（`tasks/REUSE-T18.json` ready＋`reuse/changes/REUSE-T18.json` 35 文件凭证）——§3 原拟 `changes/REUSE-T05.json` 载体因校验器 TASK-STATE 规则（清单引用任务须 ready/done、父任务保持 planned）不可行，属机器面强制修正，非范围扩张。§1/§4 注记以实际收口结果落账（F-958 已转 fixed；父任务保持 planned）；§2 的 29 处注释已订正（仅注释行）；§5 的 2 条目区间按 COVERAGE 规则扩展为整函数语义段（110-150，草案 125-150 不含文档块）；§5.3/§6 机械失真 pin 已按实测全量刷新（functions/testkit.json 76 处＋inventory 第二处条目＋execution.json 26 处既有输入绑定）；§7 白名单＋DTB §4.5 已两处一致登记。本表上方三行的历史结论为草案时点状态，其中"门禁通过/独立验收"两行已被收口结果取代（README §状态四态区分 与 §协调批收口 为准）；独立验收记录＝acceptance-sub1（实施与测试层面）。ird_gates 复跑转绿（ird-gates-run2-final.log）。

**合入登记（2026-10-23）**：所有者裁决合入，`Merge 658e0feb2`（双亲 c5a22b209＋e128dd8e9，合并树＝交付树）已将本包连同随批基础平台提交并入 redesign-main；合入后主线 validate-reuse 复跑 PASS。"整体准入由所有者裁决"事项就此闭合；远程同步按既定约束另行处理。
