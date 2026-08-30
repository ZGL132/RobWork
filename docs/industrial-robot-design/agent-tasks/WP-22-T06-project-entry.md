# WP-22-T06 项目入口与新建项目向导

- **Task ID / 需求 ID / ADR / 阶段：** WP-22-T06；UX-01～05、MDL-01、MDL-11、AT-04；ADR-001；阶段 E / R1。契约：`architecture/project-command-contract.md`、`architecture/testing-contract.md` §3～§5；模块详设 `module-design/workflow-integration.md` v0.4 §10.1～§10.2、§10.7，`module-design/robot-modeling.md` v0.4 §8。
- **基线 commit：** 代码基线 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；文档语义源 `workflow-integration.md` v0.4。
- **前置任务及必需工件：** WP-04-T01～T06（项目命令和分支修订）、WP-10-T06（工作台外壳）、WP-11-T01～T05（安全读取与资源边界）、WP-13-T03（URDF 语义映射）、WP-22-T01（阶段导航）、WP-22-T04（命令与诊断跳转）；工件为已独立验证的项目命令端口、工作台空态和 URDF 映射端口。
- **允许创建/修改/删除的文件：**（基目录 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/workflow/`）创建/修改 `project-entry/` 下无项目入口、新建项目向导、URDF 来源页、摘要页及模型；创建 `test/ProjectEntryModelTest.cpp`；修改相邻 `ui/workflow/CMakeLists.txt` 仅登记源文件和模型用例到 `sdurws_ird_workflow_model_test`；创建 `out/test-evidence/wp-22/<run-id>/`。GUI 回归只追加到 WP-22-T05 所有的 `sdurws_ird_workflow_test`，本卡不改该测试文件。
- **禁止修改的文件和公共接口：** `plugins/modeling/gui/`、WP-13-T03 URDF 解析/映射、WP-04 项目命令、WP-11 读取策略、WP-10 外壳、其他业务插件、架构、Schema 和黄金数据；建模界面不得出现 URDF 导入、模型导入或独立模型检查动作。
- **修改前接口：** 工作流只在已有项目上导航，没有无项目入口和从空白模板、内置样例、URDF 新建项目的统一向导。
- **修改后接口：** 主工作台无项目时显示“新建项目/打开项目/最近项目”；新建项目向导统一收集基本信息和来源，空白、样例、URDF 都通过 WP-04 项目命令创建项目；URDF 仅作为新建项目来源，成功后进入建模阶段，无独立导入报告页面。
- **实施步骤：** 1) 写无项目入口、来源分支、取消和错误恢复 RED；2) 实现入口模型和三种来源页；3) 接入 WP-11 安全读取与 WP-13-T03 语义映射；4) 通过 WP-04 命令原子创建项目并进入建模；5) 添加建模 GUI 禁导入扫描；6) 将 GUI 场景说明交给 WP-22-T05 回归。
- **RED 测试：** `NoProjectStateOffersThreePrimaryActions`、`BlankAndSampleCreateThroughProjectCommand`、`UrdfCreatesProjectInsteadOfImportingModel`、`CancelLeavesNoProjectArtifacts`、`ParseFailureReturnsToSourcePage`、`ModelingGuiHasNoImportAction`。
- **最小实现：** 无项目入口、三种新建来源、摘要确认、取消和错误恢复；不实现 URDF 解析器、独立模型检查、模型导入或业务建模控件。
- **正常/边界/失败测试：**
  - 正常：Given 无项目和有效 URDF，When 完成向导，Then 原子创建单机械臂项目并进入建模阶段，机器人树与三维视图显示映射结果。
  - 边界：Given 150% 缩放、长路径和重复项目名，When 浏览与校验，Then 主按钮可见、路径可完整查看、冲突在提交前阻断。
  - 失败：Given URDF 不可读、解析失败或创建命令失败，When 提交，Then 留在对应步骤并显示简短原因和处理动作，不残留半项目、不修改当前项目。
- **精确验证命令：**（仓库根；模型测试使用 QCoreApplication，不需要 GUI 平台插件）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_workflow_model_test$'`
  - 回退构建：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_workflow_model_test`
  - 回退执行：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_workflow_model_test$"`
- **diff 和禁止项检查：** diff 仅含 `ui/workflow/project-entry/`、模型测试、CMake 和证据；`rg -n -i "urdf.*(import|导入)|模型导入|模型检查|import.*model" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/gui; if ($LASTEXITCODE -eq 0) { throw '建模界面仍含禁止入口' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中；`check-boundaries.ps1` 零违规。
- **证据工件：** `out/test-evidence/wp-22/<run-id>/project-entry.md`，包含三种来源、取消/错误恢复、原子性检查、建模 GUI 禁导入扫描和命令退出码；GUI 截图与三档缩放由 WP-22-T05 汇总。
- **提交格式：** `WP-22-T06: 新增项目入口与新建向导`

  - 新增 无项目入口和三种新建来源
  - 新增 URDF 新建项目原子性模型测试
  - 检查 移除建模界面的导入与独立检查入口
- **停止与升级条件：** 项目命令不能保证原子创建、URDF 映射缺少唯一语义或需要在建模插件新增导入入口时停止并升级 WP-04/WP-13/WP-22 所有者；实现者不得担任独立验证者。
