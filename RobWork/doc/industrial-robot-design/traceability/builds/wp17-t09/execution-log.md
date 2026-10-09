# WP-17-T09 执行留痕——实现 dynamics 插件界面（工作流页＋曲线视图）

- 执行日期：2026-10-09
- 任务分支：`wp17-t09`（工作树 `build/pipeline-wt`）
- 执行者：实施会话（WP-17-T09）

## 1. 执行的命令与结果（全部真实执行）

| 步骤 | 命令 | 结果 |
| --- | --- | --- |
| 集成配置 | `cmake -S <wt>/RobWork -B <wt>/build -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON` | Configuring done / Generating done |
| 插件目标 | `cmake --build <wt>/build --config Release --target sdurws_ird_dynamics_plugin` | 零错误 |
| 测试目标 | `cmake --build <wt>/build --config Release --target sdurws_ird_dynamics_test` | 零错误 |
| 契约测试目标 | `cmake --build <wt>/build --config Release --target sdurws_ird_dynamics_contract_test` | 零错误 |
| harness 目标 | `cmake --build <wt>/build --config Release --target sdurws_ird_dynamics_app` | 零错误（构建通过；**未启动运行**——见 §3） |
| 计算库目标 | `cmake --build <wt>/build --config Release --target sdurws_ird_dynamics` | 零错误 |
| 单元测试执行 | `cmake --build <wt>/build --config Release --target sdurws_ird_dynamics_test_report` | **96 用例：95 PASSED ＋ 1 SKIPPED（envUnavailable 如实登记）＋ 0 FAILED**；gtest XML＝`traceability/gtest-reports/wp-17-t09/sdurws_ird_dynamics_test.xml`（tests="96" failures="0" errors="0"） |
| 契约测试执行 | `cmake --build <wt>/build --config Release --target sdurws_ird_dynamics_contract_test_report` | **22 用例全 PASSED**；XML＝`traceability/gtest-reports/wp-17-t09/sdurws_ird_dynamics_contract_test.xml`（tests="22" failures="0"） |
| 门禁 | `cmake --build <wt>/build --config Release --target ird_gates` | **全部检查通过：R-1/R-2/R-3/R-4/R-5/T-1/T-2/SUB/GRAPH/LIB/SA02 零命中**（含引擎自测 9 项按预期检出） |
| 独立冒烟 | `cmake -S <wt>/RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <wt>/build-smoke -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/vcpkg.cmake -DCMAKE_PREFIX_PATH=D:/software/Qt/6.11.1/msvc2022_64` ＋ `cmake --build <wt>/build-smoke --config Release` | 配置通过；全量构建零错误（见提交信息核对行） |

契约 verify 命令逐条核对：
- `cmake --build build --config Release --target sdurws_ird_dynamics_plugin` ✅ 已执行，零错误。
- `cmake --build build --config Release --target sdurws_ird_dynamics_test` ✅ 已执行，零错误（含本批新增 DynPluginPanelTest.cpp 编入）。

## 2. 落地面与契约口径（acceptance 对照）

1. **工作流页＋曲线视图界面链路**：`plugin/DynamicsPanelWidget.{hpp,cpp}`（两页合一 Tab 主面板）＋`plugin/DynCurveChartView.{hpp,cpp}`（自绘单通道折线＋游标联动＋峰值标记）＋模型层流 `plugin/DynPanelModel.{hpp,cpp}`（L-D1~L-D6 六流）。界面链路用例＝`test/DynPluginPanelTest.cpp` 六组 14 用例（模型层——QCoreApplication 级以下，零 Qt 类型消费）＋契约面 3 用例（`contract_test/DynamicsPluginAssemblyContractTest.cpp` 新增：登记面值断言＋白名单词表对账）。
2. **经 IPluginUiRegistrar 白名单挂位（契约编译期缺口——诚实登记）**：本单元依赖白名单（units/dynamics.md §3.2 五条登记边）与 ird_gates 机器面（ird_gates_whitelist.cmake IRD_ALLOWED_UNIT_EDGES）均无 `dynamics->ui` 边，且该白名单文件不在本契约 allowedFiles 内不可增登——ui 单元公共类型（IPluginUiRegistrar/IPluginUiModule/PluginUiDescriptor/DomainReadinessItem）本批**零消费**；装配面以**字段同构的自持描述符**承载（assembly/DynamicsPluginAssembly.hpp：pluginId/titleKey/stageToken/readinessDomainKey/commands/panels 与 ui::PluginUiDescriptor 逐一对应），真实注册端口消费归宿主装配批次收口（缺口登记＝单元卡 P-DYN-8）。白名单 token 对账锚以测试自持词表（ui.md §11.1 八 token）钉住。**该缺口已与编排侧裁决确认（escalate 裁决：按 B 零 ui 编译边落位），将由编排侧登记为本批 finding 转治理批次处置**。
3. **插件零计算逻辑（二分结构扫描）**：契约测试 `DynPluginAssembly.PluginFaceHasZeroComputationSymbols_WP17T02_ACC2`（14 符号全文扫描）＋`PluginFaceHasNoDirectFileOrProjectAccess_WP17T02_ACC2`（6 符号）对 plugin/＋assembly/ 全部文件扫描——本批新增 9 文件后仍零命中（实测执行）。
4. **ird_gates 零命中**：见 §1 门禁行（实测执行）。

## 3. GUI 手动验证通道（单元卡 §11.5）——未启动，如实登记

- `sdurws_ird_dynamics_app` harness **已落位并构建通过**（§1）；无人值守门禁**不做 GUI 运行验证**（既有环境事实）——本批未启动任何 GUI 可执行文件、未产生截图。
- §11.5 手动点验流程（①构建→②启动 harness→③工作流页可见→④发起评估→⑤曲线联动/峰值定位→⑥时刻回放零修订→⑦截图留痕本目录）**待人工执行**；模型层对应链路已由 `test/DynPluginPanelTest.cpp` 用例承载（峰值定位→游标跳转黄金、回放查表黄金、零修订断言）。
- 测试内 envUnavailable 用例（`DynPanelGui.WidgetPresentationDeferredToHarnessEnvUnavailable`）GTEST_SKIP 登记——**未执行的测试不标注通过**（AGENTS §4.2）。

## 4. 本批文件清单

新增（插件面 plugin/）：DynPanelTypes.hpp、DynPanelModel.hpp/.cpp、DynPanelCommandCatalog.hpp/.cpp、DynCurveChartView.hpp/.cpp、DynamicsPanelWidget.hpp/.cpp、DynPanelModule.hpp；
升级：assembly/sdurws/ird/dynamics/DynamicsPluginAssembly.hpp、plugin/DynamicsPluginAssembly.cpp；
新增（harness）：app/HarnessMain.cpp；
新增（测试）：test/DynPluginPanelTest.cpp；扩展：contract_test/DynamicsPluginAssemblyContractTest.cpp、contract_test/BuildGraphContractTest.cpp（allowed 集增登本单元 _app 目标）；
CMake：dynamics/CMakeLists.txt（插件源列表/AUTOMOC 口径/_test 链接面/app 目标）；
留痕：本目录＋traceability/gtest-reports/wp-17-t09/；
文档：units/dynamics.md（§1.2/§9.5/§11.5/§12/§15.2/§15.4 增量修订）。
