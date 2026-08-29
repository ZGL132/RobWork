# WP-03-T03 全局评估语义

- **Task ID / 需求 ID / ADR / 阶段：**WP-03-T03；需求 ARC-01、ARC-03～05、NFR-COR-03；ADR-005；阶段 A / R1。
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；文档：requirements v0.8、检查点 `IRD-D2-20260829`、architecture/evaluation-semantics.md（CTR-DOM-004）§1～§4 为唯一权威、core-domain v0.3。
- **前置任务及必需工件：**WP-03-T01（`Diagnostic` 错误通道与值类型）；WP-02-T02（参数化测试框架）；WP-01-T03（测试入口）。
- **允许创建/修改/删除的文件**（模块根同 WP-03-T01）：创建 `include/sdurws/ird/core/EvaluationSemantics.hpp`、`test/SemanticsTest.cpp`、`out/test-evidence/wp-03/<run-id>/`；修改 `CMakeLists.txt`、`DomainValidation.hpp/.cpp`（组合校验段）；删除：无。
- **禁止修改的文件和公共接口：**T01/T02 冻结签名；requirements.md 与 architecture/、module-design/ 文档；其他 WP 公共头。业务插件不得另增状态枚举；不得改变正式可行谓词语义；不得把 Warning 当 Pass；本模块只落代码，不复述契约表。
- **修改前接口：**T01/T02 交付的值类型与身份（无评估枚举、无组合谓词、无 `isFormallyFeasible`）。
- **修改后接口：**`EvaluationMode/EvidenceLevel/ExecutionOutcome/EngineeringStatus/PayloadCompleteness`（SYM-STA-001～005，值域冻结于 evaluation-semantics §1）；`RequiredEvidenceProfile`（SYM-EVI-007：usageId、profileVersion、requiredEvaluators[]{evaluatorId,minEvidenceLevel,requiredResourceFidelity,allowedWarningCategories[]}、description）；`FeasibilityGap{evaluatorId,failedCondition,detail}`、`FeasibilityVerdict{formallyFeasible,gaps[]}` 与 `isFormallyFeasible()`；非法组合码 `IRD-CORE-COMBINATION-ILLEGAL`。
- **实施步骤：**1) 参数化 60 组合 RED 测试（仅 §2 两类合法）；2) 实现枚举与合法组合谓词；3) 按 §4 伪代码逐条实现 `isFormallyFeasible()` 与 gaps；4) 断言两个锚点用例落位；5) 断言 profile 同 usageId 全局唯一。
- **RED 测试：**60 组合中的非法项（如 `Completed+NotEvaluated`、`Completed+Pass+Partial`、`Canceled+Pass`）在构造边界拒绝并返回 `IRD-CORE-COMBINATION-ILLEGAL`，不产生半包络。
- **最小实现：**枚举 + 合法组合谓词 + 谓词与缺口列举；不实现硬约束逻辑（评估器以 `Infeasible` 表达，谓词不重复）。
- **正常/边界/失败测试：**
  - 失败：Given 非法组合，When 构造 Envelope，Then 拒绝；Given 缺必需评估器、非 Verified、未 Completed、DataInsufficient/Infeasible/NotEvaluated、payload 非 Complete 或证据等级差一档，When 调用谓词，Then false 且 gaps 逐条列出。
  - 正常：Given Completed+Pass+Complete、Must 全过且证据等级满足，When 调用谓词，Then true。
  - 边界（§4 允许警告口径）：Completed+Warning 且全部警告诊断类别在 `allowedWarningCategories` 内 → true；任一类别未允许 → false 且 gaps 列出该类别；`minEvidenceLevel` 恰好满足/差一档各一例；锚点：用户取消区域覆盖 = `Canceled+NotEvaluated+Partial/None`，预算耗尽 = `Completed+DataInsufficient+Complete`；同 usageId 第二 profile 定义被拒绝。
- **精确验证命令：**
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_core_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_core_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_core_test$"`
  - 预期：目标全部用例通过（退出码 0）；脚本未交付时以原生形式执行，不复制临时脚本
- **diff 和禁止项检查：**diff 仅命中允许清单；枚举值域与 §1 逐字一致；全仓只有一处正式可行谓词定义；`EvaluationEnvelope` 等禁止名称零命中（rg 校验）。
- **证据工件：**`out/test-evidence/wp-03/<run-id>/`：60 组合矩阵日志、谓词正反例（含警告类别边界与证据等级差一档）、profile 唯一性记录。
- **提交格式：**`WP-03-T03: 新增全局评估语义枚举与可行谓词`

  - 新增 评估枚举、合法组合谓词与 `isFormallyFeasible()` 实现
  - 新增 60 组合参数化测试与目标登记
  - 新增 组合矩阵与谓词正反例证据记录
- **停止与升级条件：**需求正文与 evaluation-semantics §2/§4 冲突时停止并报告，不自行改写权威语义；组合表或谓词变更必须先修订契约与 ADR-005。
