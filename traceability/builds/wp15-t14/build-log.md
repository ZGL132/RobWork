# WP-15-T14 验证留痕（2026-10-13，实施段）

## 构建口径

- **独立冒烟模式**（本会话执行）：配置命令
  `cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B build/wp15-t14-smoke -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="D:/10_Source_Repos/21_robot/RobWork/vcpkg/scripts/buildsystems/vcpkg.cmake" -DCMAKE_PREFIX_PATH="D:/software/Qt/6.11.1/msvc2022_64"`（cwd＝工作树根 build/wp15-t14-wt）——配置成功。
- `cmake --build build/wp15-t14-smoke --config Release --target sdurws_ird_kinematics`：零错误。
- `cmake --build build/wp15-t14-smoke --config Release --target sdurws_ird_kinematics_test sdurws_ird_kinematics_contract_test`：零错误。
- **集成模式构建未执行——WIP**（契约 verify 第 1 条 `cmake --build build --config Release --target sdurws_ird_kinematics` 与第 2 条 ctest 未跑——本会话预算所限；集成树主 build/ 归所有者/验收段重配重跑，不虚报零错误）。

## 测试执行（真实运行，gtest XML＋ird-test-report 双份留痕于本目录）

| 目标 | 结果 | 明细 |
| --- | --- | --- |
| sdurws_ird_kinematics_test | 195 总：194 pass＋1 SKIPPED（PluginPanelLinks.KinGuiPresentationRegisteredNotExecuted_ACC1——既有 GUI 登记跳过，非本卡引入）＋0 fail | 其中新增 KinWorkspace 17/17（AT-25 追溯） |
| sdurws_ird_kinematics_contract_test | 57/57 pass＋0 fail | 零回归 |

- XML：`sdurws_ird_kinematics_test.xml`／`sdurws_ird_kinematics_contract_test.xml`（gtest_reports 口径）。
- 报告：`ird-test-report-test.json`（195/194/0）＋`ird-test-report-contract.json`（57/57/0）——分开捕获（同名文件按执行序覆盖，特此分档）。

## 门禁自查（AGENTS §6.3）

- `ird_gates`：**未执行**——引擎直跑需集成/治理批流程；本卡新增 include 面全部为白名单六边内既有边（core/evidence/runtime/sampling 同单元），零新边。如实登记，交验收段核对。
- `validate-docs.ps1`：见同目录验证（工作树根执行）。
- `validate-task.ps1 -TaskFile tasks/foundation/WP-15-T14.json`：见同目录验证。

## WIP 边界（如实登记，不虚报）

1. **集成模式构建未执行**（见上）。
2. **面板呈现接线未落位**：acceptance 3 的 L5 面板控件面（采样参数编辑/运行/取消/点云投影呈现控件）未接线——L2 数据与计算面已全（着色/投影/图例/导出值行/回写全部有用例），呈现归 ui View3D 的分工下控件接线归后续批次/T18 迁移链。
3. **PNG 文件生成不在本单元**：REQUIREMENTS 附录 A"视图截图 PNG＝RWStudioView3D.cpp"——本单元交付导出值行/投影/图例数据面（`workspacePointCloudExportCsv` 等有用例），PNG 渲染归 ui。
