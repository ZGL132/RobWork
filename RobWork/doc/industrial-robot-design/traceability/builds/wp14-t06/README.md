# WP-14-T06 验证留痕（实施段，2026-09-26）

- 任务：WP-14-T06（实现五种姿态规则解析与三维拾取/TCP 捕获域侧）；单元 requirements
- 分支：wp14-t06（base b1bdc8792be52a448a0cb7b6de4252b2a75f52a5）
- 实现（ OrientationResolution/Capture 两公共头＋实现 TU＋DiagCodes T06 批一码＋
  Readiness 浅核对辅助提升公共＋两测试文件）与单元卡同步（§3.3/§9.6/§12/§14.6 v0.6）
  随实施提交；本目录为验证留痕面。

## 留痕清单

| 文件 | 内容 | 来源（真实执行） |
| --- | --- | --- |
| sdurws_ird_requirements_test.xml | 模型测试 gtest XML：tests=123 failures=0 | 集成树 bin/Release 可执行真实运行（--gtest_output=xml） |
| sdurws_ird_requirements_contract_test.xml | 契约测试 gtest XML：tests=5 failures=0 | 同上 |
| ird-test-report-requirements.json | 用例级明细 total=123 passed=123 failed=0 | 同上（--ird_report） |
| ird-test-report-contract.json | 用例级明细 total=5 passed=5 failed=0 | 同上 |
| build-integration-full.log | 集成模式全量构建日志（Release，零错误） | cmake --build build --config Release（RWS_BUILD_INDUSTRIALROBOT=ON 已 grep 确认） |
| build-smoke-t06-configure.log | 独立冒烟模式配置日志（vcpkg toolchain 绝对路径＋Qt 前缀） | cmake -S industrialrobot -B build_smoke_t06 |
| build-smoke-t06.log | 独立冒烟模式构建日志（全目标零错误，exit=0） | cmake --build build_smoke_t06 --config Release |
| ird_gates_base_engine_raw.txt / ird_gates_head_engine_raw.txt | 门禁引擎直跑原始输出（base/head 两侧，exit=1＝存量例外既有状态） | cmake -DIRD_ROOT=... -P ird_gates.cmake（base 侧临时 worktree，比对后即删） |
| ird_gates_base_hits_normalized.txt / ird_gates_head_hits_normalized.txt | 归一化命中集（各 127 行，路径占位替换后排序） | 同上 |
| ird_gates_base_head_comparison.md | base..head 归一化比对结论：命中集 IDENTICAL（零新增命中） | 逐行 diff |

## 契约 verify 命令执行记录（tasks/foundation/WP-14-T06.json verify 数组）

1. `cmake --build build --config Release --target sdurws_ird_requirements` —— 执行，零错误；
2. `ctest --test-dir build/RobWorkStudio/src/rwslibs/industrialrobot/requirements -C Release -L "^ird$"` —— 执行，2/2 通过（100%）；
3. `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1` —— 执行，PASS（20 units, 12 trace entries, 189 task files；单元卡增量修订后复跑仍 PASS）。

补充（非 verify 数组，DoD 口径）：

- 集成模式全量构建（含 _test/_contract_test/ui 等）零错误（build-integration-full.log）；
- 独立冒烟模式：产品目标＋两测试目标全量构建零错误（build-smoke-t06.log；vcpkg toolchain 绝对路径）；
- `validate-task.ps1 -TaskFile RobWork/doc/industrial-robot-design/tasks/foundation/WP-14-T06.json` PASS；
- 模型测试 123/123（新增 ReqOrientationResolution 5 用例＋ReqCapture 8 用例；DiagCodesTest 封闭断言 12→13 随附同步）、契约测试 5/5；
- ird_gates：引擎直跑 base..head 归一化比对零新增命中（IDENTICAL_HIT_SETS，各 127 行；存量例外既有状态未清零，DTB §4.5）。
