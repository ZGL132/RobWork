# ASM-PANEL 三新域面板挂位收口批——构建/门禁/测试执行留痕

- 日期：2026-10-10
- 分支：`asm-panel`（宿主 `redesign-main` 任务分支；基点＝b26a20b3 RUL-TOK
  返工第 1 轮合入后——六域注册全 Ok 形态在案）
- 范围：三新域面板挂位收口（六域面板全部可见）——ExtraDomainAssembly
  三域面板取件 API＋UiPlugin 宿主挂位段（IRD_UI_PLUGIN_EXTRA_DOMAINS
  单一书写点 ui_plugin/studio 两宿主共用）＋失败隔离＋ui_contract_test
  聚合用例扩展＋布局冒烟通道同步＋两处登记（units/ui.md v1.93）。
- 三域门面与计算库源码零改动（面板工厂闭包已在三域 assembly 门面——
  本批只补宿主消费面）；白名单零新边（IRD_TARGET_LEVEL_EDGES/
  IRD_ALLOWED_UNIT_EDGES/IRD_TEST_TARGET_EDGES 均未动）；CMakeLists
  零改动（新 API 与挂位段随既有 TU 编入面）。

## 1. 集成模式构建（真实执行）

配置命令（增量，吸收源码改动）：

```
cmake -S <wt>/RobWork -B <wt>/build -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON
```

配置退出码 0。构建目标（Release，`grep -cE ": error|: fatal error"`
计数 0，`cmake --build` 退出码 0——日志文件＋`$?` 直读严格口径）：

- 批次 1：sdurws_ird_ui / _test / _contract_test / _gui_test / _app /
  **sdurws_ird_ui_plugin**（挂位段宿主目标一）/ **sdurws_ird_studio**
  （挂位段宿主目标二——exe 全新链接零错误）
- 批次 2：sdurws_ird_workflow_test / _contract_test、
  sdurws_ird_dynamics_test / _contract_test、sdurws_ird_selection_test /
  _contract_test、sdurws_ird_optimization_test / _contract_test

## 2. 独立冒烟模式构建（真实执行）

```
cmake -S <wt>/RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <wt>/build-smoke \
  -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=<repo>/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64
cmake --build <wt>/build-smoke --config Release
```

配置与全树构建退出码均 0，error 计数 0。冒烟模式无框架目标——
ui_plugin/studio 不注册（既有登记口径；本批挂位段属集成树专属）。

## 3. ird_gates 门禁（真实执行，零命中）

```
cmake --build <wt>/build --config Release --target ird_gates
```

退出码 0；尾行「全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/
LIB/SA02 零命中」＋引擎自测按预期检出（fail_* 系）。本批白名单与
CMakeLists 零改动——命中集与基线一致零新增。

## 4. 单元测试／契约测试（真实执行，gtest XML 为准）

集成树（直接运行 exe；`--gtest_output=xml:`＋`--ird_report=` 双 report
口径〔DTB §5.5〕；PATH 前置 D:/software/Miniconda3——python313.dll
fresh 前置；XML/JSON/日志拷贝于 `traceability/gtest-reports/asm-panel/`）：

| 目标 | 用例 | 失败 | skip | 说明 |
| --- | --- | --- | --- | --- |
| sdurws_ird_ui_test | 257 | 0 | 0 | 零用例增删（本批测试面在 contract_test） |
| sdurws_ird_ui_contract_test | 35 | 0 | 0 | 基线 34＋本批新增 1（ExtraDomainPanelPickup_FailureIsolationAbsentReturnsNull_ASM_PANEL_ACC1）；SixDomain 聚合用例增⑩段断言（用例数不变） |
| sdurws_ird_workflow_test | 193 | 0 | 0 | 回归基线保持 |
| sdurws_ird_workflow_contract_test | 77 | 0 | 0 | 回归基线保持 |
| sdurws_ird_dynamics_test | 105 | 0 | 3 | skip＝既有 GUI envUnavailable 登记（DynPluginPanelTest GuiPresentation 系） |
| sdurws_ird_dynamics_contract_test | 30 | 0 | 0 | 基线保持 |
| sdurws_ird_selection_test | 277 | 0 | 3 | skip＝既有 GUI envUnavailable 登记 |
| sdurws_ird_selection_contract_test | 58 | 0 | 0 | 基线保持 |
| sdurws_ird_optimization_test | 164 | 0 | 3 | skip＝既有 GUI envUnavailable 登记（OptPluginPanelTest） |
| sdurws_ird_optimization_contract_test | 49 | 0 | 0 | 基线保持 |

冒烟树（直接运行，`.smoke.xml` 后缀同目录留痕）：

| 目标 | 用例 | 失败 | 说明 |
| --- | --- | --- | --- |
| sdurws_ird_ui_test | 240 | 0 | 与 asm-studio 冒烟基线一致（零回归） |
| sdurws_ird_ui_contract_test | 22 | 0 | 集成专属增列不在冒烟（既有口径）——本批新用例属集成增列 |

新用例过滤直跑（集成树，权威轮后复核）：
`--gtest_filter="*ASMSTUDIO*:*ASM_PANEL*"` → 2/2 PASSED。

## 5. 做红验证（双变异实证——测试真实失败能力）

对新增断言面做产品侧变异（plugin/ExtraDomainAssembly.cpp，临时改动
即改即恢复；重编译后过滤直跑）：

- M1＝dynamicsPanelWidget 缺席分支误返非空哨兵指针 → 重编译后
  `--gtest_filter="*ASM_PANEL*"` 真实失败（FAILED 1，退出码 1）——
  取件缺席语义断言具备真实失败能力；
- M2＝selection 登记状态强制 `status.ok=false`（伪造登记失败）→
  重编译后 `--gtest_filter="*ASMSTUDIO*"` 真实失败（FAILED 1，退出码 1）
  ——六域全 Ok 聚合断言（含本批⑩段供体面前置）具备真实失败能力；
- 恢复两处变异后重编译＋全量重跑十套件全绿（§4 表即恢复后权威轮
  产物）；`grep -rn "做红变异|redSentinel"` 产品源零残留。

## 6. 未执行事项（如实登记）

- **ui_gui_test 未运行**：GUI 契约测试不在无人值守门禁（ui.md §12.2
  串行通道）；gui_test 目标构建通过（编译面零回归）。本批布局冒烟
  通道断言面已随域面板同步（default-hidden 补三域＋aux-toggles 计数
  4→7＋layout2 三 still-hidden 断言）——该通道为实机 GUI 环境
  （IRD_UI_PLUGIN_SMOKE=layout），归所有者实机点验执行。
- **RobWorkStudio 实机装载与六域面板可见性人工点验未执行**：任务依据
  明示"本批交付后所有者启动实机点验为最终确认"（GUI 通道如实登记，
  不伪造）。宿主挂位段（UiPlugin buildDockBody/reassertEmbedded
  Presentation）的端到端呈现验证＝§12.2 实机装载通道；无人值守钉扎
  ＝工厂在册供体面断言＋取件缺席语义用例（§4/§5）。
- 三新域面板 widget 实体在无人值守通道零实例化（QCoreApplication 级
  结构性限制——QWidget 构造须 QApplication，units/ui.md §13 ASM-PANEL
  行如实登记；ASM-UI 先例口径，不伪造 GUI 断言）。
- ctest 注册树列表为 0 的既有缺陷未触碰——一律以直接运行 exe 口径
  为准（DTB §5.5；本批未走 _report 目标而以同口径直接运行 exe＋双
  report 参数，XML/JSON 落 traceability/gtest-reports/asm-panel/）。
