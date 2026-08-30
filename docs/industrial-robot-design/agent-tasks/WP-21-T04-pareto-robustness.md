# WP-21-T04 Pareto 与鲁棒性

- **Task ID / 需求 ID / ADR / 阶段：** WP-21-T04；OPT-04（Pareto 非支配、无加权总分）、OPT-06（Quick 筛选＋Verified 复核）、OPT-07（激活目标与 `comparisonTolerance` 语义归 WP-20）、OPT-09（§15.3 扰动协议三模式）、AT-10～13、NFR-PERF-04～06；ADR-004（Pareto 容差支配复用 WP-20 冻结口径，不建第二实现）。阶段 D / R2。契约：`architecture/evaluation-semantics.md` §3～§4、`architecture/symbol-registry.md`（SYM-OPT-006/009/010）；模块详设 `module-design/optimization.md` v0.3 §4、§5.4～§5.5；需求 `requirements.md` §15.3（误淘汰审计与鲁棒性协议冻结值）。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-21-T02（四层判定与可行集准入可用）；WP-20-T04（静态 Pareto 容差支配冻结实现，`StaticPareto.hpp` 公共头）；工件：T01～T02 用例通过、WP-02 optimization audit/robustness 样本占位。
- **允许创建/修改/删除的文件：**（前缀 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/optimization/`）创建 `joint/include/sdurws/ird/opt/MiseliminationAudit.hpp`、`joint/src/MiseliminationAudit.cpp`、`joint/include/sdurws/ird/opt/RobustnessProtocols.hpp`、`joint/src/RobustnessProtocols.cpp`、`test/ParetoRobustnessTest.cpp`、`testdata/optimization/audit/`、`testdata/optimization/robustness/`；修改模块 CMakeLists 与 `joint/src/JointSearchOrchestrator.cpp`（挂接审计与鲁棒性执行点）；写 `out/test-evidence/wp-21/<run-id>/`。不删除文件。
- **禁止修改的文件和公共接口：** WP-20 `StaticPareto.*`（只调用其支配判定）；WP-05 谓词；WP-16～19 评估器；WP-08 execution；`requirements.md`、CSV、`schemas/`；不新增 CMake 目标。
- **修改前接口：** 无 `AuditSample` 审计流水线、无鲁棒性三模式执行器；联合 Pareto 计算不存在。
- **修改后接口：** `MiseliminationAudit` 产出 `AuditSample`（候选 ID、淘汰原因、分层键、Verified 复核结论）审计流水线；`RobustnessProtocols` 提供三模式执行器；联合 `ParetoSet`（SYM-OPT-006）支配关系调用 WP-20 冻结口径（容差支配、软约束不参与支配、全目标差异均在容差内＝互不支配并列；结论措辞"当前变量域、算法与计算预算内发现"）。
- **实施步骤：**
  1. 写 RED 测试（误淘汰判定、分层抽样、超限处置、三模式结论措辞、覆盖对象选取）。
  2. 实现审计：固定种子，按淘汰原因×目标值区间分层抽取淘汰候选的 5%、且不少于 200 个（淘汰数不足时全抽）执行 Verified 复核；"Quick 淘汰但 Verified 满足硬约束且相对已验证集合非支配"计误淘汰。
  3. 实现超限处置：误淘汰率 >1% 或 95% 置信上界 >3% → `IRD-OPT-AUDIT-THRESHOLD-EXCEEDED`（Engineering/Error）：扩大保留池、禁用对应 Quick 规则重跑、禁发正式候选报告。
  4. 实现鲁棒性三模式（需求 §15.3 冻结）：有界公差验证（P0，正式）＝签署公差表＋固定种子 64 点拉丁超立方采样，全部有效样本满足全部硬约束才判"公差域内鲁棒通过"，任一样本违反判"鲁棒性不足"，编译失败样本单独报告计入失败不计入分母替换；概率鲁棒性（P1）＝仅签署分布模型与置信口径时输出通过率置信区间，否则禁止任何概率性结论；敏感度抽查（P0，默认）＝默认扰动（结构尺寸 ±0.5%、质量/惯量 ±5%、摩擦 ±20%、传动效率向不利方向降低 2 个百分点）同款采样，输出指标敏感度排序与最差裕量，标注"敏感度参考"，禁用鲁棒措辞。
  5. 实现覆盖对象选取：最终比较的至多 10 个 Verified 候选（不足全做），必须覆盖每个激活目标的 Pareto 极值点、膝点、用户选中项与多样性代表（目标空间最大—最小距离），不得只取排序前 10。
  6. 物理一致性：扰动不得独立缩放质量与惯量，按解析估算或一致性规则重算，每个样本惯量张量执行正定性与三角不等式校验，非法样本单独报告。
  7. 执行验证命令，写证据。
- **RED 测试：** 实现前 `ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_joint_test$"` 无 ParetoRobustness 用例；落地后审计、三模式、覆盖对象、超限处置四组用例全部通过。
- **最小实现：** 审计流水线＋三模式执行器＋联合 Pareto 调用 WP-20 口径＋覆盖对象选取；不做候选应用（T05）与验收证据装配（T06）。
- **正常/边界/失败测试：**
  - 正常：Given 2,000 个 Quick 淘汰候选与固定种子，When 审计，Then 分层抽取 ≥200 个样本（5%×2000=100 <200 触发下限）、`AuditSample` 四字段齐全、同种子复现抽取集合一致。
  - 边界：Given 淘汰数不足 200，When 审计，Then 全抽；Given 已签署公差表但某样本编译失败，When 有界公差验证，Then 该样本单独报告并计入失败、不被分母替换。
  - 失败：Given 误淘汰率 1.5%，When 审计收口，Then `IRD-OPT-AUDIT-THRESHOLD-EXCEEDED`、扩大保留池并禁用对应 Quick 规则重跑、不生成正式候选报告；Given 无签署分布模型，When 请求概率结论，Then 拒绝输出（稳定诊断）。
- **精确验证命令：**（仓库根、VS x64 环境）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_joint_test$'`
  - 回退：`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_joint_test`
  - 回退：`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_joint_test$"`
- **diff 和禁止项检查：** `git diff --name-only` 仅含允许清单；无第二套支配关系实现（`joint/` 不出现支配判定公式复制，只调用 WP-20）；无加权总分；三模式结论措辞互不混用；`rg -n "鲁棒通过" out/test-evidence/wp-21/<run-id>/t04-pareto-robustness/sensitivity-output.json; if ($LASTEXITCODE -eq 0) { throw '敏感度模式错误使用鲁棒结论措辞' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中。
- **证据工件：** `out/test-evidence/wp-21/<run-id>/t04-pareto-robustness/`：误淘汰审计报告（分层键、抽样数、误淘汰率与 95% 置信上界）、三模式结果样例（64 点 LHS 样本身份）、覆盖对象清单、命令原文与 commit。
- **提交格式：** `WP-21-T04: Pareto 与鲁棒性`

  - 新增联合优化 Pareto 支配与鲁棒性审计
  - 新增误淘汰率阈值测试
  - 新增运行证据记录
- **停止与升级条件：** WP-20 Pareto 公共头无法满足联合目标维度、或 §15.3 冻结值与审计统计方法存在不可推导缺口时，停止并升级工作包所有者；实现者不得担任本卡独立验证者。
