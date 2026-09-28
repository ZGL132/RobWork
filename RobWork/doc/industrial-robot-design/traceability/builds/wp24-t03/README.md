# traceability/builds/wp24-t03/ — WP-24-T03 返工（fix cycle 1）验证留痕

对应验收返工：attempt 2 fail（阻断 B-1 唯一项＝findings F-421，记录
`traceability/acceptance/WP-24-T03-20260928.md` @ `acc/WP-24-T03/2`，
记录提交 5786af8fbf3f0d94a156545187353f93d780162a）。本目录为返工段
（分支 wp24-t03，tip 9802e83b 之上续接）全程亲手执行留痕，attempt 3。

## 现场说明

- 集成树＝独立 worktree 冷启（`wt_wp24t03_fix`，任务完成后删除）：vcpkg
  toolchain＋`CMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64`＋
  `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`（CMakeCache grep 确认）＋F-420 在册
  口径排除三个与 industrialrobot 零依赖的框架模拟插件目标（attempt 2 验收同款）。
- 冒烟树＝`build_smoke_wp24t03fix`（vcpkg toolchain＋Qt CMAKE_PREFIX_PATH，
  AGENTS §4.1 口径）。

## 契约 verify 四命令（逐条亲跑）

| # | 命令 | 结果 | 留痕 |
| --- | --- | --- | --- |
| 1 | `cmake --build build --config Release` | **0（零 error）** | verify1_build.log（build_release_fix.log 为全量冷启首建同值） |
| 2 | `cmake --build build --config Release --target ird_gates` | **exit 1＝存量红**：引擎自计 72 处命中——F-019 在册口径，与 base 两端同值零新增（见下门禁比对） | verify2_gates.log |
| 3 | `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1` | 分支树失败＝**base 继承的 WP-24-T08.json note JSON 转义缺陷**（note line 63 position 903——attempt 2 验收 4.3-③ 同一款；主线 fecdf104 已修、合并即消解）。主检出 redesign-main@fecdf104 亲跑 **PASS (20 units, 12 trace entries, 202 task files)** 佐证（不落本目录——主检出簿记现场，结论见提交信息与验收请求） | verify3_validate_docs_branchtree.log |
| 4 | `cmake --build build --config Release --target sdurws_ird_ui_test sdurws_ird_modeling_test` | **0** | verify4_targets.log |

validate-task 自查：目标契约 `WP-24-T03.json` **PASS (1 tasks)**；同目录扫描
对 WP-24-T08.json 的 ConvertFrom-Json 报错同上（base 继承、合并即消解，
未私改他任务契约）。

## 阻断 B-1 修复与具名测试对照

| 返工清单 | 修复落位 | 具名证据 |
| --- | --- | --- |
| ①字符集放行 `-`（＋注释与实现对齐） | ui/src/PluginUiRegistrar.cpp `commandIdWellFormed`：段内字符集 [a-z0-9-]＋'.' 分段、必含点（词形权威＝ui.md §7.1 v0.8 登记口径）；注释同步修正 | `PluginRegistrarT03B.CommandIdSyntaxRejectsUppercaseAcceptsHyphen_WP24_T03B`（大写拒＋失败不入列＋连字符放行）＋破坏实验 break/restore 四件 |
| ②真链路 gtest | modeling/test/PluginModuleT03BTest.cpp 增 PluginRegistrarT03B 族 | `PluginRegistrarT03B.RealChainModelingDescriptorOkTenCommands_WP24_T03B`（真实 descriptor→Ok＋报告 ok=1/panels=1/commands=10） |
| ③关于框真实清单 GUI 帧复录 | — | **未执行（编排者禁启 Studio——GUI 冒烟禁跑）**：机制级自证由②承载，真机呈现面随全量验收段裁量 |
| 顺带面：注释任务号拼写 | ui/src/WorkbenchContent.cpp WP-24_T03B→WP-24-T03B（验收 4.7 登记小疵） | 纯注释零行为 |

## 测试执行（gtest XML＋console 全量在册）

- 集成：modeling 272/272（attempt 2 基线 270＋返工新增 2）、ui 158/158、
  ui_gui 40/40、ui_contract 22/22、modeling_contract 17/17——均 exit=0。
- 冒烟：modeling 215/215、ui 158/158、ui_contract 22/22、modeling_contract
  8/8——均 exit=0，与 attempt 2 冒烟基线逐目标同值。
- `ird-test-report.json`＝结构化汇总（schemaVersion ird-test-report/1）。

## 门禁比对（ird_gates——base..head 归一化，F-398/F-402 家族纪律）

两端均以引擎直跑（`cmake -P cmake/ird_gates.cmake`，base 树＝detached
worktree @ 3f767d9c、head 树＝返工后 wp24-t03）：

| 计数口径 | base（3f767d9c） | head（返工后） | 一致性 |
| --- | --- | --- | --- |
| ① IRD-GATE-* token 原始出现数 | 151 | 151 | 一致 |
| ② token 种类 sort -u | 8 | 8 | 一致 |
| ③ 含 IRD-GATE 行 sort -u（cmake -P 直跑无双份回显） | 151 | 151 | 一致 |
| ④ 引擎自计（"72 处命中"） | 72 | 72 | 一致 |

组成关系注明：直跑口径无 MSBuild 回显双份（与 attempt 2 验收者的构建目标
口径 151/8/53/72 粒度不同、同值等价——其 ③=53 为 MSBuild 并行输出切分＋
路径归一化合并后的唯一命中行）；本目录 ③ 未经路径归一化前含各自树绝对
路径前缀，归一化（树路径→WORKTREE）后行级去重集 diff=**空**。
附件：ird-gates-{base,head}-cmakep.log、ird-gates-lineuniq-{base,head}-norm.txt。

## 破坏实验（真实失败能力自证）——全链留痕无删改

1. break_build.log/break_run.log：临时禁用连字符放行（等价缺陷回潮）→
   `--gtest_filter=PluginRegistrarT03B.*` 实测 2 FAILED（exit=1）。
2. restore_build.log/restore_run.log：还原环节**误用 git checkout 将未提交
   修复一并回滚至 HEAD 缺陷版**（教训在案：未提交修复不得用 git checkout
   做破坏还原）——restore_run 的 FAILED 即该缺陷版产物，反向再次实证测试
   判别力。
3. reapply_build.log＋最终态 XML：重新施加修复→modeling_test 全量 272/272
   复绿；冒烟树增量重建零重编＝产物与最终修复源态一致。

## 未执行项（如实登记）

- 返工清单③ GUI 帧（见上表）。
- 主检出 validate-docs PASS 佐证未落本目录文件（主检出为编排者簿记现场，
  不落杂物；结论载于提交信息与验收请求，可复跑）。
