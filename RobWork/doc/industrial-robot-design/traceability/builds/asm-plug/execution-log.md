# ASM-PLUG 收口批——构建/门禁/测试执行留痕

- 日期：2026-10-10
- 分支：`asm-plug`（宿主 `redesign-main` 任务分支）
- 范围：三插件真实挂位收口（P-DYN-8／P-SEL-10／P-OPT-10 消账）——
  dynamics/selection/optimization 三单元装配面翻译注册＋白名单/依赖图
  三边增登＋三卡收口登记。

## 1. 集成模式构建（真实执行）

配置命令（增量，吸收 CMake 改动）：

```
cmake -S <wt>/RobWork -B <wt>/build -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON
```

构建目标（Release，退出码 0，error 计数 0——输出经 `grep -iE ": error|: fatal"` 零命中）：

- sdurws_ird_dynamics_plugin / _test / _contract_test / _app
- sdurws_ird_selection_plugin / _test / _contract_test / _app
- sdurws_ird_optimization_plugin / _test / _contract_test

## 2. ird_gates 门禁（真实执行，零命中）

```
cmake --build <wt>/build --config Release --target ird_gates
```

输出尾行：`[ird_gates] 全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中`；
引擎自测 9 例全部按预期检出（pass_clean/pass_r4comment 干净通过＋
fail_r1/fail_t1/fail_r5/fail_sub/fail_r3/fail_r4/fail_t2 按预期检出）。
本批新登记边（IRD_ALLOWED_UNIT_EDGES 三单元边＋IRD_TARGET_LEVEL_EDGES
三条 ui_plugin 消费行＋IRD_TEST_TARGET_EDGES 三条 contract_test→ui 行）
与 dependency-graph.json（68 条边）GRAPH 一致性核对通过。

## 3. 独立冒烟模式构建（真实执行）

```
cmake -S <wt>/RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <wt>/build-smoke \
  -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=<repo>/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64
cmake --build <wt>/build-smoke --config Release
```

全树构建退出码 0，error 计数 0（`grep -iE ": error|: fatal"` 零命中；
尾行 sdurws_ird_workflow_test 正常产出）。

## 4. 单元测试／契约测试（真实执行，gtest XML 为准）

六个 `<target>_report` 目标逐一构建执行（构建目标＝自动运行测试 exe 并
写 gtest XML）；XML 拷贝于本仓库
`doc/industrial-robot-design/traceability/gtest-reports/asm-plug/`：

| 目标 | 用例数 | 失败 | 跳过 | 说明 |
| --- | --- | --- | --- | --- |
| sdurws_ird_dynamics_test | 105 | 0 | 1 | SKIP＝DynPanelGui envUnavailable（T09 既有登记，非通过） |
| sdurws_ird_dynamics_contract_test | 30 | 0 | 0 | 新增 4：DynPluginRegistration 三用例＋词表具名豁免半区用例 |
| sdurws_ird_selection_test | 277 | 0 | 1 | SKIP＝SelPanelGui envUnavailable（T10 既有登记，非通过） |
| sdurws_ird_selection_contract_test | 58 | 0 | 0 | 新增 3：SelPluginRegistration 三用例 |
| sdurws_ird_optimization_test | 164 | 0 | 1 | SKIP＝OptPanelGui envUnavailable（T10 既有登记，非通过） |
| sdurws_ird_optimization_contract_test | 49 | 0 | 0 | 新增 3：OptPluginRegistration 三用例 |

三单元 `_test` 目标本批零源码改动（基线用例数原值保持——零回归）；
`_contract_test` 增量仅 ASM-PLUG 用例与具名豁免登记（既有用例零回归）。

## 5. 未执行事项（如实登记）

- **GUI 实机点验未执行**：三单元 `_app` harness（§11.5/§15.3 手动点验
  通道）本批只交付构建产物，未启动运行——无人值守门禁不做 GUI 运行
  验证；`*_test` 中的 GUI 呈现用例以 envUnavailable GTEST_SKIP 如实
  登记（非通过）。手动点验流程归验收会话/所有者执行。
- ctest 注册树列表为 0 的既有缺陷未触碰——一律以 `_report` 目标为准
  （DTB §5.5 口径）。
