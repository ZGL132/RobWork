# GOV-FIND 执行留痕（findings 索引式收编＋project 注释勘误）

- 日期：2026-10-10
- 分支：`gov-find`（工作树 `D:/10_Source_Repos/21_robot/RobWork/build/pipeline-wt`）
- 批次内容：①findings.json 索引式收编——按 origin/acc/* 全量 84 个验收分支（`git ls-remote --heads origin 'refs/heads/acc/*'` 实测 84 个，与本地引用差集为空）逐任务/轮次登记索引条目 **F-630～F-713**（编号自现存最大 F-629 顺延；字段严格沿用既有 schema：id/source/severity/status/registeredAt/title/resolution/fixedAt；轮内具体建议级已按当轮转登在册者于 resolution 注明编号，不重复登记）；②project 单元 `PersistenceFormat.hpp:506` CommandRecord.commandType 注释示例勘误——带点示例 "project.create-branch" 与同条冻结语法 `^[a-z0-9-]{3,64}` 矛盾，改为合规无点示例并登记勘误缘由（acc/wp-20-t07/1 验收范围外观察消账；零行为改动）。
- 本批产品代码改动面：**仅注释**（PersistenceFormat.hpp 一处 @字段注释，4 行→10 行，无任何代码/行为变化）。findings.json 为纯新增 840 行（84 条索引条目），既有 604 条条目零改动。

## 执行记录（本会话真实执行）

| # | 步骤 | 命令 | 结论 |
| --- | --- | --- | --- |
| 1 | 集成树配置 | `cmake -S <worktree>/RobWork -B <worktree>/build -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON` | Configuring/Generating done；缓存 `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON` 事先 grep 确认 |
| 2 | 库构建 | `cmake --build build --config Release --target sdurws_ird_project` | 零 error；1 处存量 warning C4297（ProjectStoreImpl.cpp:794，既有代码，与本批注释改动无关，如实登记） |
| 3 | 测试运行① | `cmake --build build --config Release --target sdurws_ird_project_test_report` | **255 tests：254 PASSED＋1 SKIPPED（Tx11CopyConsistency.StageBPrerequisiteNotReady_SkipRegistered——TX-11 前置未就绪的如实登记跳过，非失败）＋0 FAILED**（12.726 s）；gtest XML 见 `traceability/gtest-reports/gov-find/sdurws_ird_project_test.xml`（tests="255" failures="0"） |
| 4 | 测试运行② | `cmake --build build --config Release --target sdurws_ird_project_contract_test_report` | **28 tests：28 PASSED＋0 FAILED**（7.248 s）；XML 见 `traceability/gtest-reports/gov-find/sdurws_ird_project_contract_test.xml`（tests="28" failures="0"） |
| 5 | 门禁 | `cmake --build build --config Release --target ird_gates` | **全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中**（引擎自测 fail_r3/fail_r4/fail_r5/fail_sub/fail_t2 均按预期检出） |
| 6 | 文档校验 | `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1` | **PASS (20 units, 12 trace entries, 312 task files)** |
| 7 | 冒烟配置 | `cmake -S industrialrobot -B build-smoke -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64` | Configuring/Generating done |
| 8 | 冒烟构建 | `cmake --build build-smoke --config Release`（全量→增量复核） | **exit 0**（错误模式 grep 零命中；project 单元产物 `sdurws_ird_project.lib`＋两测试 exe 在 build-smoke/project/Release/ 齐全） |

## 与验收依据的对应

- 验收标准①（findings 索引式收编）：F-630～F-713 共 84 条，id 连续性经脚本校验；每条含 taskId、acc 分支、verdict、建议级/偏差要点一句话、详情指向（origin/acc/<分支> :: 验收记录路径 @ 记录提交 40 位 SHA）；治理/ui 会话现存条目（F-558～F-628 等，如 F-570/F-571/F-452/F-462/F-182～F-185）在 resolution 中引用不重复登记。
- 验收标准②（PersistenceFormat.hpp 注释勘误）：零行为改动（纯注释）；勘误示例取同文件既有合规形态 "create-branch"，并注明 §6.5 内置命令族正式 token 待 P-PR-9 裁决（units/project.md §15.3）。同型表述 `src/CommandServiceImpl.hpp:60` 为 P-PR-9 阻断登记陈述（有意记录该矛盾本身），不在本批 allowedFiles，未改动。
- 验收标准③（回归全绿＋门禁＋validate-docs）：见上表 #3/#4/#5/#6——全部真实执行，全绿/零命中/PASS。

## 主仓库零污染自查

`git -C "D:/10_Source_Repos/21_robot/RobWork" status --porcelain` 在本会话操作前后均为会话开始快照既有内容（ui-t74 留痕三件修改＋历史未跟踪目录），本批零新增条目（交付前复核记录见提交正文）。
