# WP-14-T05 验证留痕（实施段，2026-09-26）

- 任务：WP-14-T05（实现就绪校验 R0~R9 与预览/正式分离及需求命令族）；单元 requirements
- 分支：wp14-t05（base 5df21a81af1859b1b3bf9ee2233f6da50a80eede）
- 提交：170d3d0d（Readiness 面）→ b6a3e3ed（命令处理器族）；留痕面提交随本目录（文档/留痕）

## 留痕清单

| 文件 | 内容 | 来源（真实执行） |
| --- | --- | --- |
| sdurws_ird_requirements_test.xml | 模型测试 gtest XML：tests=108 failures=0 | 集成树 bin/Release 可执行真实运行（--gtest_output=xml） |
| sdurws_ird_requirements_contract_test.xml | 契约测试 gtest XML：tests=5 failures=0 | 同上 |
| ird-test-report-requirements.json | 用例级明细 total=108 passed=108 failed=0 | 同上（--ird_report） |
| ird-test-report-contract.json | 用例级明细 total=5 passed=5 failed=0 | 同上 |
| build-smoke-t05.log | 独立冒烟模式构建日志（零错误，exit=0） | cmake --build build_smoke_t05（vcpkg toolchain＋Qt 前缀） |
| ird_gates_base_engine_raw.txt / ird_gates_head_engine_raw.txt | 门禁引擎直跑原始输出（base/head 两侧，exit=1＝存量例外既有状态） | cmake -DIRD_ROOT=... -P ird_gates.cmake |
| ird_gates_base_hits_normalized.txt / ird_gates_head_hits_normalized.txt | 归一化命中集（各 127 行，路径占位替换后排序） | 同上 |
| ird_gates_base_head_comparison.md | base..head 归一化比对结论：命中集 IDENTICAL（零新增命中） | 逐行 diff |

## 契约 verify 命令执行记录（tasks/foundation/WP-14-T05.json verify 数组）

1. `cmake --build build --config Release --target sdurws_ird_requirements` —— 执行，零错误；
2. `ctest --test-dir build/RobWorkStudio/src/rwslibs/industrialrobot/requirements -C Release -L "^ird$"` —— 执行，2/2 通过（100%）；
3. `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1` —— 执行，PASS（20 units, 12 trace entries, 189 task files）。

补充（非 verify 数组，DoD 口径）：

- 集成模式测试目标构建（_test/_contract_test）零错误；
- 独立冒烟模式：本单元产品目标＋两测试目标构建零错误（build_smoke_t05.log）；
- `validate-task.ps1 -TaskFile tasks/foundation/WP-14-T05.json` PASS；
- ird_gates：引擎直跑 base..head 归一化比对零新增命中（存量例外既有状态未清零，DTB §4.5）。
