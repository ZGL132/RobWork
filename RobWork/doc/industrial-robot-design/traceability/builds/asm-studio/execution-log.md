# ASM-STUDIO studio 承接收口批——构建/门禁/测试执行留痕

- 日期：2026-10-10
- 分支：`asm-studio`（宿主 `redesign-main` 任务分支）
- 范围：studio 承接收口——白名单 studio→dynamics/selection/optimization
  三条 unit 级边增登（两表均登）＋studio 段三新域装配承接（六域在正式
  主程序装配齐备）＋分布钉扎消账（IRD_TEST_TARGET_EDGES 三行＋六域聚合
  注册断言用例）＋WP-16-T08~T13 六契约状态修正＋两处登记（units/ui.md
  v1.91＋DTB §2.27 studio 承接批消账）。

## 1. 集成模式构建（真实执行）

配置命令（增量，吸收 ui/CMakeLists.txt 改动）：

```
cmake -S <wt>/RobWork -B <wt>/build -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON
```

构建目标（Release，构建日志 `grep -cE ": error|: fatal error"` 计数 0，
cmake --build 退出码 0——注：早期一轮曾以 `grep|head` 管道尾退出码误读
构建结果，实施中已改为日志文件＋`$?` 直读的严格口径，下述结论均按该
口径复核）：

- sdurws_ird_ui / _test / _contract_test / _gui_test / _app
- **sdurws_ird_studio**（本批核心——编入 ExtraDomainAssembly.cpp＋
  IRD_UI_PLUGIN_EXTRA_DOMAINS 定义＋链接三新域 plugin，exe 全新产物
  15:06 链接零错误）
- **sdurws_ird_ui_plugin**（ASM-UI 既有承接面零回归——同源 TU 编译
  零变化行为）
- sdurws_ird_ui_contract_test（同源直编 DomainAssembly.cpp＋
  ExtraDomainAssembly.cpp＋链接三新域 plugin——新用例编译链接通过）
- sdurws_ird_workflow_test / _contract_test（回归基线）

## 2. ird_gates 门禁（真实执行，零命中）

```
cmake --build <wt>/build --config Release --target ird_gates
```

退出码 0；尾行「全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/
LIB/SA02 零命中」；引擎自测 9 例按预期检出（pass_clean/pass_r4comment/
pass_r4cliff 干净通过＋fail_r1/fail_t1/fail_r5/fail_sub/fail_r3/fail_r4/
fail_r4cliff/fail_t2 按预期检出）。本批白名单增登三边（unit 级表＋
EXTRA_EDGE_REFS 出处登记）＋测试面三边（IRD_TEST_TARGET_EDGES）——
studio 非单元节点不入 dependency-graph.json（白名单 :312 既有口径），
本批零依赖图改动，GRAPH 一致性随零改动成立。

## 3. 独立冒烟模式构建（真实执行）

```
cmake -S <wt>/RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <wt>/build-smoke \
  -G "Visual Studio 17 2022" -A x64 \
  -DCMAKE_TOOLCHAIN_FILE=<repo>/vcpkg/scripts/buildsystems/vcpkg.cmake \
  -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64
cmake --build <wt>/build-smoke --config Release
```

全树构建退出码 0，error 计数 0。冒烟模式无框架目标（`if(TARGET sdurws)`
不成立）——ui_plugin/studio 不注册（既有登记口径，本批 studio 承接面属
集成树专属，不在冒烟验证范围；测试目标增列为同段门控，冒烟计数见 §4）。

## 4. 单元测试／契约测试（真实执行，gtest XML 为准）

集成树（直接运行 exe；四套均带 DTB §5.1 PATH 前置 `D:/software/Miniconda3`
——python313.dll fresh 前置；`--gtest_output=xml:`＋`--ird_report=` 双
report 口径〔DTB §5.5〕；XML/JSON 拷贝于 `traceability/gtest-reports/
asm-studio/`）：

| 目标 | 用例数 | 失败 | 说明 |
| --- | --- | --- | --- |
| sdurws_ird_workflow_test | 193 | 0 | 基线保持（零回归——回归基线口径） |
| sdurws_ird_workflow_contract_test | 77 | 0 | 基线保持（零回归） |
| sdurws_ird_ui_test | 255 | 0 | 基线保持（本批零用例增删——ui_test 零消费三新域，不虚构测试面） |
| sdurws_ird_ui_contract_test | 34 | 0 | 基线 33＋本批新增 1（SixDomainHostAssemblySequence_Snapshot_ASMSTUDIO_ACC1——XML 内 status=run result=completed 已核对） |

冒烟树（直接运行，同目录 `.smoke.xml`/`.smoke.json` 后缀）：

| 目标 | 用例数 | 失败 | 说明 |
| --- | --- | --- | --- |
| sdurws_ird_workflow_test | 193 | 0 | 与集成同计数 |
| sdurws_ird_workflow_contract_test | 77 | 0 | 与集成同计数 |
| sdurws_ird_ui_test | 240 | 0 | 差 15＝PresentationBridgeContractTest（集成专属增列 if(TARGET sdurw_kinematics)——既有登记口径） |
| sdurws_ird_ui_contract_test | 22 | 0 | 差 12＝UI-T20/T23/ASM-STUDIO 集成专属增列（含本批六域聚合用例——集成树专属面；冒烟基线 22 不变） |

## 5. 做红验证（双变异实证——测试真实失败能力）

对新增用例 SixDomainHostAssemblySequence_Snapshot_ASMSTUDIO_ACC1 的两条
核心断言面做产品侧变异（ExtraDomainAssembly.cpp，临时改动即改即恢复）：

- M1＝dynamics 登记状态强制 `status.ok=false`（伪造登记失败）→ 重编译
  后 `--gtest_filter=*ASMSTUDIO*` 真实失败（FAILED 1，退出码 1）；
- M2＝selection 登记状态强制 `status.ok=true`（伪造登记成功——诚实
  拒绝被本地绕过）→ 重编译后同过滤真实失败（FAILED 1，退出码 1）；
- 恢复两处变异后重编译＋全量重跑集成四套 193/77/255/34 全绿（§4 表即
  恢复后权威轮产物）；`grep 做红变异` 产品源零残留（git diff 仅本批
  文件头注释登记更新）。

## 6. 未执行事项（如实登记）

- **ui_gui_test 未运行**：GUI 契约测试不在无人值守门禁（ui.md §12.2
  串行通道）；本批零 GUI 呈现面改动（Widget/对话框/Dock 零触碰），
  gui_test 目标构建通过（编译面零回归）。
- **RobWorkStudio 真实装载与界面人工点验未执行**：正式主程序六域装配
  承接的端到端运行（含 selection 注册拒绝在共享树/状态栏的隔离呈现）
  归所有者人工流程（本批任务依据明示口径，如实登记不伪造）；装配序列
  语义的无人值守钉扎＝新用例六域聚合注册断言（§4/§5）。
- ctest 注册树列表为 0 的既有缺陷未触碰——一律以直接运行 exe＋
  `_report` 目标口径为准（DTB §5.5）。
- ui_plugin（MODULE）本身的 UiPlugin::initialize 装配全链不经测试目标
  直测（CMake 禁止 executable 链接 MODULE 的结构性限制仍在；assemble
  DomainPlugins 需 QDockWidget——QCoreApplication 级测试不实例化），
  维持 asm-ui 登记形态（units/ui.md §13 ASM-UI/ASM-STUDIO 行）。
