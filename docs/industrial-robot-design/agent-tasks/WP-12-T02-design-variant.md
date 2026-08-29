# WP-12-T02 新机型与改型内容

- **Task ID / 需求 ID / ADR / 阶段：**WP-12-T02；需求 NFR-COR-04、EVI-01、REQ-06、OPT-04；ADR-004；阶段 A / R1。
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；文档：requirements v0.8（§5.2/§5.3 报告要求）、检查点 `IRD-D2-20260829`、module-design/reporting.md v0.3 §3。
- **前置任务及必需工件：**WP-12-T01（`ReviewReport/ReviewReportBuilder` 与 `reports/` 追加）；WP-05-T05（`EvidenceGap` 与谓词输出）；WP-01-T03（测试入口）。
- **允许创建/修改/删除的文件**（模块根同 WP-12-T01）：创建 `test/DesignVariantTest.cpp`、`testdata/{new-design,variant}/`、`out/test-evidence/wp-12/<run-id>/`；修改 `ReviewReport.hpp`（设计摘要/基线差异字段段）、`ReviewReportBuilder.hpp/.cpp`、`src/ReviewReport.cpp`、`CMakeLists.txt`；删除：无。
- **禁止修改的文件和公共接口：**T01 冻结的构建入口与追加协议；requirements.md 与 architecture/、module-design/ 文档；WP-03/05 谓词与缺口结构；以单一加权分数替代 Pareto 取舍；其他 WP 公共头。
- **修改前接口：**T01 的 `ReviewReport` 含设计摘要占位字段，未区分新机型/改型结构。
- **修改后接口：**新机型内容段（需求、设计参数、证据、候选、限制）；改型逐指标差异段 `{基线值,候选值,绝对变化,相对变化,来源}`；Pareto 候选与取舍理由结构（保留多目标，不合成单一分数）。
- **实施步骤：**1) 先写缺基线/缺指标来源失败测试；2) 实现新机型报告段装配；3) 实现改型逐指标差异计算（绝对/相对变化，来源指向快照/结果 ID）；4) 实现 Pareto 候选与取舍理由装配；5) 断言两模式下字段完整性与来源可追溯。
- **RED 测试：**改型报告缺基线值或某指标缺来源 → `IRD-RPT-INPUT-INCOMPLETE` 定位到指标；候选集出现单一加权总分字段 → 编译/测试失败（结构不允许）。
- **最小实现：**内容装配与差异计算；渲染与数据包归 T03/T04。
- **正常/边界/失败测试：**
  - 失败：Given 基线修订缺失或候选结果缺失，When build 改型报告，Then Input 诊断且不产出报告。
  - 正常：Given 新机型项目，When build，Then 列出需求、设计参数、证据、候选和限制；Given 改型项目，Then 每项指标给出基线值、候选值、绝对变化、相对变化和来源。
  - 边界：相对变化分母为零（基线值为 0）不产生非法数，按固定规则标注；Pareto 候选取舍理由逐条呈现，不以加权分数替代。
- **精确验证命令**（无 GUI 测试）：
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_reporting_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_reporting_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_reporting_test$"`
  - 预期：目标全部用例通过（退出码 0）；脚本未交付时以原生形式执行，不复制临时脚本
- **diff 和禁止项检查：**diff 仅命中允许清单；无加权总分字段；全部数值有限（SI）；来源引用均为内容 ID/快照 ID，不用显示名作身份。
- **证据工件：**`out/test-evidence/wp-12/<run-id>/`：新机型/改型样例报告 JSON、逐指标差异表、来源追溯清单、测试输出。
- **提交格式：**`WP-12-T02: 新增新机型与改型报告内容`

  - 新增 新机型内容段、逐指标差异段与 Pareto 取舍结构实现
  - 新增 缺基线/缺来源失败测试及目标登记
  - 新增 样例报告 JSON 与逐指标差异表证据记录
- **停止与升级条件：**差异指标集合或相对变化规则无法从 requirements §5.2/§5.3 推导时停止并报告；新增报告字段须先改 reporting.md §3 再实现。
