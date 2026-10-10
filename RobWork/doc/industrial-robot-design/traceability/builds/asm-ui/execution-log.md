# ASM-UI 收口批——构建/门禁/测试执行留痕

- 日期：2026-10-10
- 分支：`asm-ui`（宿主 `redesign-main` 任务分支）
- 范围：ui 宿主面收口——ui_plugin 三新域承接（dynamics/selection/
  optimization 预登记边转消费）＋WorkflowHostAdapters 两缺陷修复＋
  11 必填桥参数化＋两卡收口登记（units/ui.md v1.90＋units/workflow.md
  §10.2 v1.6）。

## 1. 集成模式构建（真实执行）

配置命令（增量，吸收 CMake 改动）：

```
cmake -S <wt>/RobWork -B <wt>/build -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON
```

构建目标（Release，`grep -iE ": error|: fatal"` 零命中）：

- sdurws_ird_workflow / _test / _contract_test
- sdurws_ird_ui / _test / _contract_test / _gui_test / _app
- **sdurws_ird_ui_plugin**（本批核心——链接段增链三域 plugin，
  IRD_TARGET_LEVEL_EDGES 三条预登记边转消费）
- sdurws_ird_studio（同源编入 DomainAssembly.cpp——无
  IRD_UI_PLUGIN_EXTRA_DOMAINS 定义，维持三域装配零变化，构建通过）

## 2. ird_gates 门禁（真实执行，零命中）

```
cmake --build <wt>/build --config Release --target ird_gates
```

输出尾行：`全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02
零命中`；引擎自测 9 例按预期检出（pass_clean/pass_r4comment 干净通过＋
fail_r1/fail_t1/fail_r5/fail_sub/fail_r3/fail_r4/fail_t2 按预期检出）。
本批零新边（白名单未触碰——消费 asm-plug 预登记三条 ui_plugin 目标级
边），dependency-graph.json 未改（GRAPH 一致性随零改动成立）。

## 3. 独立冒烟模式构建（真实执行）

```
cmake -S <wt>/RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <wt>/build-smoke \
  -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=<repo>/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64
cmake --build <wt>/build-smoke --config Release
```

全树构建退出码 0，error 计数 0（`grep -iE ": error|: fatal"` 零命中）。
冒烟模式无框架目标（`if(TARGET sdurws)` 不成立）——ui_plugin/studio 不
注册（既有登记口径，本批三域承接面属集成树专属，不在冒烟验证范围）。

## 4. 单元测试／契约测试（真实执行，gtest XML 为准）

集成树（`<target>_report` 目标构建执行＋直接运行复核；ui 两套运行带
DTB §5.1 PATH 前置 `D:/software/Miniconda3`——python313.dll fresh 前置；
XML 拷贝于 `traceability/gtest-reports/asm-ui/`）：

| 目标 | 用例数 | 失败 | 说明 |
| --- | --- | --- | --- |
| sdurws_ird_workflow_test | 193 | 0 | 基线 191＋ASM-UI 新增 2（修复③④钉扎；11 桥参数化为既有用例改造不增数） |
| sdurws_ird_workflow_contract_test | 77 | 0 | 基线保持（零回归） |
| sdurws_ird_ui_test | 255 | 0 | 基线保持（零回归） |
| sdurws_ird_ui_contract_test | 33 | 0 | 基线 32＋ASM-UI 新增 1（HostRegistrarAssemblySequence_Snapshot_ASMUI_ACC2） |

冒烟树（直接运行，`--gtest_output=xml:` 生成，同目录 `.smoke.xml` 后缀）：

| 目标 | 用例数 | 失败 | 说明 |
| --- | --- | --- | --- |
| sdurws_ird_workflow_test | 193 | 0 | 与集成同计数 |
| sdurws_ird_workflow_contract_test | 77 | 0 | 与集成同计数 |
| sdurws_ird_ui_test | 240 | 0 | 差 15＝PresentationBridgeContractTest（集成专属增列 if(TARGET sdurw_kinematics)——既有登记口径） |
| sdurws_ird_ui_contract_test | 22 | 0 | 差 11＝UI-T20/T23 集成专属增列（含本批 HostIntegration 用例——集成树专属面） |

## 5. 做红验证（双变异实证——测试真实失败能力）

临时回退两处修复核心行（分流段预填登记＋候选身份 store 取值）后重编译：
两枚新增钉扎用例均**真实失败**（collectImportCalls==2≠1／激活身份==旧
项目≠候选）——`FAILED 2 tests`；恢复修复后重编译全量重跑 193/193 全绿。

## 6. 未执行事项（如实登记）

- **ui_gui_test 未运行**：GUI 契约测试不在无人值守门禁（ui.md §12.2
  串行通道）；本批零 GUI 呈现面改动（命令条/对话框/Dock 零触碰），
  gui_test 目标构建通过（编译面零回归）。
- **ui_plugin 六域装配承接的运行期验证未执行**：承接 TU 编入集成树
  MODULE（CMake 禁止测试目标链接 MODULE）——运行验证通道＝开发期插件
  实机装载（Plugins→Load plugin）与 GUI 冒烟，归验收会话/所有者实机
  流程；装配序列语义的测试面钉扎分布（宿主面用例三旧域聚合快照＋三域
  contract_test 真实端口激活）登记 units/ui.md §13 ASM-UI 行。
- ctest 注册树列表为 0 的既有缺陷未触碰——一律以 `_report` 目标为准
  （DTB §5.5 口径）。
