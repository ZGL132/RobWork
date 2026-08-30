# WP-22-T04 诊断与报告入口

- **Task ID / 需求 ID / ADR / 阶段：** WP-22-T04；UX-01～03、AT-12；ADR-004、ADR-005；阶段 E / R1＋R2。契约：`architecture/public-interfaces.md` §1、§4～§6、`architecture/evaluation-semantics.md` §4～§5、`architecture/execution-model.md` §1；模块详设 `module-design/workflow-integration.md` v0.4 §5、§7、§10.4、§10.6～§10.7。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档语义源 `workflow-integration.md` v0.4。
- **前置任务及必需工件：** WP-22-T01（转移表与驾驶舱骨架）、WP-22-T02（`NextStepAdvisor` 与状态聚合——`IRD-WF-EVIDENCE-MISSING` 由建议求值路径发出）；外部：WP-04-T02（`IProjectCommandService` 命令端口）、WP-09-T02/T03（`IDiagnosticCatalog` 目录与错误码登记）、WP-12-T01（`ReviewReportBuilder` 报告入口）；工件：T01～T02 用例通过、WP-09 目录含 `IRD-WF-*` 三码提名条目。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/ui/workflow/`）创建 `include/sdurws/ird/ui/workflow/CommandPalette.hpp`、`src/CommandPalette.cpp`；修改 `test/WorkflowModelTest.cpp`（追加命令守卫用例）、`testdata/`（命令守卫样本）；写 `evidence/`。不删除文件。
- **禁止修改的文件和公共接口：** WP-04/WP-09/WP-12 公共头（命令服务、诊断目录、报告构建器只绑定不复制）；WP-10 源文件；T01～T02 已冻结语义（转移表、建议规则表）；业务插件私有头与 Widget；`requirements.md`、CSV；不新增 CMake 目标。
- **修改前接口：** 无 `CommandPalette`；驾驶舱未绑定命令、诊断与报告入口；`WorkflowModelTest` 无命令守卫用例。
- **修改后接口：** `CommandPalette` 按 §5 冻结命令绑定既有端口：保存、撤销/重做、运行/快速检查、取消、切换阶段、应用方案、比较方案、生成评审报告、打开工程策略和项目命令。URDF 新建项目入口归 WP-22-T06，不作为导入命令。不可用原因使用简短工程用语；诊断可跳转对象、字段和对应 Dock；报告对话框显示范围、状态、问题摘要和生成动作，不显示内部身份值。
- **实施步骤：**
  1. 写 RED 测试（九组命令守卫参数化、撤销/重做空态与失效命令、Quick 用途提示、报告缺口列表、IRD-WF-* 触发）。
  2. 实现 `CommandPalette` 数据结构：命令→端口绑定与守卫谓词（只调用既有端口）。
  3. 实现守卫失败不可用原因文案（UX-02 工程用语）与诊断触发（经 `IDiagnosticCatalog`）。
  4. 绑定报告入口：输入不完整时列 `IRD-RPT-INPUT-INCOMPLETE` 缺口并指向补数据入口。
  5. CMake 编入既有模型测试目标，执行验证命令，写证据。
- **RED 测试：** 实现前 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_workflow_model_test$"` 无 CommandPalette 用例；落地后全部通过。
- **最小实现：** 命令面板、守卫、诊断跳转和报告对话框绑定；不实现项目新建入口（T06）、比较视图（T03）或 GUI 回归（T05）。
- **正常/边界/失败测试：**
  - 正常：Given 候选满足 `isFormallyFeasible`，When 请求"应用为当前方案"，Then 命令经 WP-04 端口发出（方案分支＋恰好一个新修订，基线不覆盖）。
  - 边界：Given 无可撤销操作，When 请求撤销，Then 显示 `IRD-PROJ-NOTHING-TO-UNDO` 文案；Given 阶段完成证据缺失，When 求下一步建议，Then `IRD-WF-EVIDENCE-MISSING`（Input/Warning）按 `gaps` 列缺口；Given 仅 Quick 结果，Then 提示"不作正式证据"（evaluation-semantics §5）。
  - 失败：Given 未通过正式可行判定的候选请求应用，Then `IRD-WF-APPLY-BLOCKED`（Engineering/Error）列 gaps、保持当前修订；Given 比较集缺同名指标，Then `IRD-WF-NOT-COMPAREABLE`（Input/Error）拒绝整组比较；Given 报告输入不完整，Then 列 `IRD-RPT-INPUT-INCOMPLETE` 缺口。守卫文案不含哈希/Schema/内部插件名（UX-02）。
- **精确验证命令：**（仓库根、VS x64 环境；`QCoreApplication` 模型测试）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_workflow_model_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_workflow_model_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_workflow_model_test$"`
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；无新增命令接口或第二套诊断目录；命令不直写 revision、不经旁路执行业务计算；守卫文案无哈希/Schema/内部插件名。
- **证据工件：** `ui/workflow/evidence/t04-diagnostic-guidance.log`：九组命令守卫矩阵（前置满足/不满足两分支）、诊断与报告入口样例（含 IRD-WF-*/IRD-RPT-*/IRD-PROJ-NOTHING-TO-* 触发记录）、命令原文与 commit。
- **提交格式：** `WP-22-T04: 诊断与报告入口`
- **停止与升级条件：** 模块详设 §5 冻结命令集与 WP-04/09/12 端口签名无法对接、或 IRD-WF-* 三码未在 WP-09 目录登记时，停止并升级工作包所有者；实现者不得担任本卡独立验证者。
