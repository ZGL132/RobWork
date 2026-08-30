# WP-19-T07 器件选型工作台界面

- **Task ID / 需求 ID / ADR / 阶段：** WP-19-T07；SEL-01～08、UX-01～08、NFR-PERF-03；阶段 C / R1。契约：`architecture/testing-contract.md` §3～§5、`architecture/evaluation-semantics.md` §5；模块详设 `module-design/device-selection.md` v0.4 §8.1～§8.7、`module-design/session-ui.md` v0.4 §8。
- **基线 commit：** 代码基线 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；文档语义源 `device-selection.md` v0.4。
- **前置任务及必需工件：** WP-19-T05（`ComponentSelectionResult`）、WP-19-T06（目录版本）、WP-10-T06（工作台外壳）、WP-11-T04（目录安全导入）、WP-01-T03（测试入口）。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/selection/`）创建/修改 `gui/` 下产品目录、轴需求、筛选、可行方案、淘汰方案、详情、曲线、比较和整机草案面板；创建 `test/SelectionGuiTest.cpp`；修改本插件 `CMakeLists.txt` 仅登记 GUI 文件与 `sdurws_ird_selection_gui_test`；创建 `out/test-evidence/wp-19/<run-id>/`。禁止删除计算核心文件。
- **禁止修改的文件和公共接口：** `src/`、目录和结果公共头、WP-11 导入、WP-18 传动映射、WP-10 外壳、架构、Schema、正式目录数据；禁止 GUI 改写筛选结论或就地刷新历史目录版本。
- **修改前接口：** 目录、筛选和结果接口可用，但没有 §8 规定的目录/轴需求、筛选、可行与淘汰方案、曲线、比较和整机草案界面。
- **修改后接口：** GUI 经只读端口呈现目录版本、轴需求、筛选条件、可行与淘汰原因、曲线/工作点、2～4 方案比较和整机草案；采用动作仍走 WP-19-T05 的候选应用语义。
- **实施步骤：** 1) 写表列、筛选、比较和按钮状态 RED；2) 实现目录与轴需求面板；3) 实现可行/淘汰/详情/曲线面板；4) 实现比较与整机草案；5) 接入空态、目录不可用和部分结果；6) 登记并运行 GUI 目标。
- **RED 测试：** `CatalogAndAxisRequirementColumnsMatchSpecification`、`RejectedRowsExposeReasonAndMargin`、`CompareAcceptsTwoToFourCandidates`、`CatalogVersionIsAlwaysVisible`、`UnavailableCatalogBlocksRunWithoutChangingHistory`。
- **最小实现：** §8 的选型浏览、筛选、比较和状态界面；不实现目录导入、筛选算法、传动映射或候选应用逻辑。
- **正常/边界/失败测试：**
  - 正常：Given 有效轴需求和目录版本，When 筛选完成并选择候选，Then 可行表、详情、曲线工作点和整机草案一致。
  - 边界：Given 2～4 个候选和 150% 缩放，When 比较，Then 差异列可读、主操作可见、长型号通过省略和提示展示。
  - 失败：Given 目录缺失、版本不可用或无可行项，When 运行，Then 显示明确原因和处理入口，历史结果不变且不产生半成品草案。
- **精确验证命令：**（仓库根、Visual Studio x64 环境；GUI 平台固定为 windows，单次只运行本目标）
  - `$env:QT_QPA_PLATFORM='windows'; powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_selection_gui_test$'`
  - 回退构建：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_selection_gui_test`
  - 回退执行：`$env:QT_QPA_PLATFORM='windows'; $testExe=(Resolve-Path '.\out\build\industrial-robot\bin\Debug\sdurws_ird_selection_gui_test.exe').Path; & $testExe`
- **diff 和禁止项检查：** diff 仅含本插件 `gui/`、测试、CMake 和证据；`rg -n "import|refreshInPlace|selectComponents\(|DriveTrainMapping" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/selection/gui; if ($LASTEXITCODE -eq 0) { throw '检测到 GUI 越权实现' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中；边界脚本零违规。
- **证据工件：** `out/test-evidence/wp-19/<run-id>/selection-ui.md`，包含表列、筛选、比较和按钮矩阵、目录错误态、三档缩放截图及命令退出码。
- **提交格式：** `WP-19-T07: 新增器件选型工作台界面`

  - 新增 目录、筛选、候选与比较面板
  - 新增 目录失败和无可行项 GUI 测试
  - 证据 记录缩放和历史结果保护结果
- **停止与升级条件：** 目录或结果契约不能唯一提供 §8 字段、需要 GUI 改写筛选语义或历史版本时停止并升级对应所有者；实现者不得担任独立验证者。
