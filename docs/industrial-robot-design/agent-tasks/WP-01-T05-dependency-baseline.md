# WP-01-T05 依赖与 API 基线

- **Task ID / 需求 ID / ADR / 阶段：** WP-01-T05；NFR-DEP-05（主：冻结 RobWork 家族/Qt/编译器/碰撞后端/构建选项/API 与许可证基线）、NFR-SEC-05（依赖清单含组件、版本、许可证、哈希）；当前无直接关联 ADR，若发现未批准依赖则本任务产出新 ADR 并登记；阶段 A 前提 / R1。契约：`architecture/public-interfaces.md`。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-01-T04；工件：`RobWork/scripts/industrial-robot/check-boundaries.ps1`（可扩展）、`out\build\industrial-robot` 配置日志（含生成器/架构/选项证据）与 `gitlab-ci/industrial-robot-windows.yml`。实施与验证所用 worktree 按 WP-01 计划 §5.6 环境准备模板准备。
- **允许创建/修改/删除的文件：**
  - 创建：`RobWork/dependencies/industrial-robot-baseline.json`
  - 修改：`RobWork/scripts/industrial-robot/check-boundaries.ps1`（追加依赖审计规则，不改既有规则与 `-ScanRoot` 签名）
  - 条件创建：`docs/industrial-robot-design/architecture/adr/ADR-006-unapproved-dependency-<slug>.md`（仅当存在 approved=false 且必须引入的组件）
  - 写运行日志：`out/logs/industrial-robot/<timestamp>/`（原始日志）；证据工件登记于 `out/test-evidence/wp-01/<run-id>/`（两级口径见 WP-01 计划 §5.4）
- **禁止修改的文件和公共接口：** `industrialrobot/` 内 CMake 与源文件（CMake 引用基线组件在后续 WP 进行）；旧插件；`requirements.md`、CSV、文档门禁脚本；不得让缺来源/许可证/哈希或 approved=false 的组件进入 CMake。
- **修改前接口：** `check-boundaries.ps1` 为 T01 版本（四类构建违规＋脚本规则，无依赖审计）；`dependencies/industrial-robot-baseline.json` 不存在。
- **修改后接口：** `industrial-robot-baseline.json` 顶层固定字段 `schemaVersion、projectCommit、components、compiler、qt、buildOptions`；每个组件含 `name、version、source、license、sha256、usage、approved、approvalRef`；`check-boundaries.ps1` 新增规则：依赖缺版本/来源/许可证/哈希/审批即非零并指出组件与缺失字段。
- **实施步骤：**
  1. 先执行"RED 测试"断言：用仅含 `{"schemaVersion":1}` 的临时 JSON 触发审计，确认非零与字段诊断（RED）。
  2. 采集基线：RobWork/RobWorkStudio/RobWorkSim 锁定 commit（94fb910e8d4b1e2bb84d569cbca4aa623cbd2844 及仓库实际状态）、Qt 版本、MSVC 与 Windows SDK 版本、碰撞检测后端及版本、第三方组件与许可证；一并记录操作员 `CMAKE_PREFIX_PATH` 的组件与版本组成（WP-01 计划 §5.5，仅入环境版本记录，`industrial-robot-baseline.json` 结构不变、不写机器绝对路径）。
  3. 生成 `industrial-robot-baseline.json`，逐组件填齐 8 字段（哈希用 `Get-FileHash`/锁定 commit 记录）。
  4. 在 `check-boundaries.ps1` 实现依赖审计规则与"组件＋缺失字段"诊断。
  5. 对未批准组件编写阻断断言；确需引入的，按 ADR → 安全/许可证评审 → 更新 JSON → CMake 引用（后续 WP）→ 扫描与 CI 流程产出 ADR 草案。
  6. 独立评审者复核 CMake 引用与基线一致性，写证据。
- **RED 测试：** 先写的失败断言：`t05-missing-fields`：临时最小 JSON（缺 components/compiler/qt/buildOptions 或组件缺 8 字段之一）→ `check-boundaries.ps1` 必须非零并点名组件与缺失字段；`t05-unapproved`：组件 `approved=false` → 必须非零且该组件不得被任何 `sdurws_ird_*` 目标引用。
- **最小实现：** JSON 基线＋审计规则两条断言转绿；不为未批准组件添加任何 CMake 引用；ADR 仅在确有未批准必需组件时产出。
- **正常/边界/失败测试：**
  - 正常：Given 完整基线 JSON，When 运行 `check-boundaries.ps1`，Then 退出码 0，依赖审计无诊断。
  - 边界：Given 组件字段齐全但 `approvalRef` 为空串，When 审计，Then 非零并指出 approvalRef 缺失（空串视同缺失）。
  - 失败：Given 缺 sha256/license/source 或 approved=false，When 审计或构建引用，Then 非零并指出组件，不进入 CMake。
- **精确验证命令：**（仓库根目录、VS x64 环境；脚本＋原生双形式）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1`；预期退出码 0（含依赖审计通过）。
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_.*_test$'`；预期退出码 0（基线不破坏既有目标）。
  - 原生回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_core` 与 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_core_test$"`；预期构建与测试通过。
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `industrial-robot-baseline.json`、`check-boundaries.ps1` 与（如产出）新 ADR 文件；`industrialrobot/` 内 CMake 零变化（本任务不引用组件）；既有边界规则与签名零变化。
- **证据工件：** `industrial-robot-baseline.json`（提交物）、`out/test-evidence/wp-01/<run-id>/dependency-audit.log`（两条 RED 断言前后结果，原始日志自脚本契约目录复制入根）、版本采集记录（MSVC/SDK/Qt/碰撞后端命令输出与 `CMAKE_PREFIX_PATH` 组件组成）、独立评审签署（评审者非采集者）。
- **提交格式：** `WP-01-T05: 依赖与 API 基线`
- **停止与升级条件：** 组件版本/哈希无法在锁定 commit 上复现、许可证无法判定，或存在无法批准又无法移除的必需依赖时，停止并升级给安全/许可证评审与工作包所有者；采集者不得自行批准自己登记的组件。
