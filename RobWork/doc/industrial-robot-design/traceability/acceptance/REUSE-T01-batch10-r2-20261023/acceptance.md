# REUSE-T01 B10-P2-01 返工独立复验

冻结 SHA：`a3cb564810b3eae8515ba11bfed9c37756d808f4`。原失败冻结：`e0bee2994eee0e8f3299ea5ccf200c354f4454be`。

结论：**PASS，仅关闭 B10-P2-01**。20 个宏的真实 `ird_add_gtest(...)` 调用与 functions、test-build-inventory 两处消费者集合全部一致；10 个单元的13个误列目标已移除，无漏项。B11新功能及全T01退出不在本次验收范围。

## 复验依据

- 新建独立 TEMP clone（非父构建目录），只检出上述 SHA；clone/checkout 均exit0，初末checkout干净。未读取父树未提交实现。
- 从每个产品单元的真实CMake源码逐处提取行首宏调用，核20组、47次真实消费，inventory/functions两处均20/20集合匹配且无重复调用；宏source绑定两处20/20一致。详见 `macro-target-checks.json`。
- 移除项恰为原P2列明的13项：dynamics_app、execution_worker、kinematics_app、demo6r、modeling_app、project_openbench_worker、requirements_app、selection_app、testdata_lint、testkit_prochelper、trajectory_app、ui_app、studio（均sdurws_ird_前缀）。涉及10个单元，逐单元差集见 `removed-targets.json`；未通过给这些程序新增宏调用来迎合登记。policy消费者只调整排序，集合不变。
- 本批58条关系（原36main、20宏、2监听器）关联auditEvidence全部290项摘要新鲜；全部58处inventory引用均匹配新摘要 `6c993a7ff3da836ccd305377cb0a4978c0f0634406cd50b297e819084ae647c4`。另核58项functionInput、20项macroInput和40项两处macroSource摘要，总466项、0失配，见 `hash-checks.json`。
- main库存保持不变，58条B10关系仍pending。相对原失败SHA，产品源码、框架、vcpkg及permissions.json无变更；没有新增产品权限或迁移完成宣称。F-957/F-958仍open。

## 历史证据保真

以原TEMP文件为基准，逐一比对冻结归档，原根文件无缺漏：

| 归档 | 文件数 | 字节一致 | 仅CRLF/LF不同 | 内容失配 |
| --- | --- | --- | --- | --- |
| REUSE-T01-batch10-r1-20261023 | 44 | 36 | 8 | 0 |
| REUSE-T01-gtest-listener-20261023 | 27 | 24 | 3 | 0 |

详细逐文件记录见 `archive-checks.json`。原B10 acceptance仍为FAIL且保留B10-P2-01；本报告不回写原失败。GTest独立验收仍仅对公开Append局部集合接管heap监听器给出PASS，1 passed、0 failed/skipped/notRun，wholeT01Pass=false；未变成产品修复或所有退出崩溃重现的声明。

## 本次真实验证

| 命令 | 结果 |
| --- | --- |
| validate-task.ps1 -TaskFile tasks/REUSE-T01.json | PASS，exit0，task.log |
| validate-reuse.ps1 -Head a3cb564… -TaskFile tasks/REUSE-T01.json | PASS治理结构，exit0，reuse.log |
| validate-docs.ps1 | PASS，exit0，docs.log |
| check-rework.ps1 | PASS，exit0，check.log |

三门禁计数：passed=3、failed=0、skipped=0。20宏语义消费者复验：passed=20、failed=0；摘要466 passed、0 failed。门禁报告全库328关系、closedFiles=0，其全库计数包含B11，仅作机器门禁观察，不作为B11独立语义验收。

C++构建、GUI、既有25项API与新增GTest探测重跑：**NOT RUN**，本次返工仅消费者登记和摘要；按委派范围核已保存独立GTest PASS。同SHA探测CMake有B11的PlanningSuitabilityTest/pathplanning增量，不属于本次验收，未把该增量视为已通过或重验GTest的依据。全库DeepEvidence、全T01退出：NOT RUN。

本次未实施、未修改任何仓库、未提交/推送/合入、未派代理。报告与日志在本独立TEMP根；归档复制根文件并排除 `repo/`。本PASS不能授予全T01 PASS、T02/T05实施准入或生产权限。
