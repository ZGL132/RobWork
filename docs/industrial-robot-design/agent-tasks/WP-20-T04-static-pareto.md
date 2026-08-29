# WP-20-T04 静态指标与 Pareto

- **Task ID / 需求 ID / ADR / 阶段：**WP-20-T04；OPT-04、OPT-07、OPT-08 静态子集（OPT-B）、AT-09；ADR-003；阶段 B / R1。契约：`module-design/optimization.md` v0.3 §5.3～§5.4、`architecture/symbol-registry.md` SYM-OPT-006/007/009/010、需求 §9.3（支配与容差）
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源同 T01
- **前置任务及必需工件：**WP-20-T03（可行集合工件——Pareto 只对可行候选）；WP-15-T01（`minJointMargin` 与 WP-15 同一公式——只读消费其结果，不复制实现）
- **允许创建/修改/删除的文件：**创建 `plugins/optimization/candidate/include/sdurws/ird/opt/StaticMetrics.hpp`、`StaticPareto.hpp`，`candidate/src/StaticMetrics.cpp`、`StaticPareto.cpp`，`test/StaticParetoTest.cpp`，`testdata/optimization/pareto/`；修改 `plugins/optimization/CMakeLists.txt`（仅追加本任务文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**WP-15 裕量公式实现（只消费结果）；T01～T03 冻结语义；requirements/CSV/architecture/module-design；禁止加权总分替代 Pareto、禁止隐式聚合
- **修改前接口：**无指标视图与 Pareto 计算
- **修改后接口：**`StaticMetrics`（指标视图：metricId、value、unit、来源 `ResultRef`）：OPT-B 可算三项（模块冻结）——`overallSizeEnvelope`（单位 m/m³ 随声明）、`structureMass`（kg，派生重算后连杆质量合计）、`minJointMargin`（dimensionless，与 WP-15 同一公式）；其余五项（节拍/器件成本/器件质量/关节侧正机械功/最小驱动裕量）属阶段 C/D，StageB 研究定义中引用即拒（复用 `IRD-OPT-STAGE-LOCKED`）。`StaticPareto`：支配关系＝对全部激活目标 A 不劣于 B 且至少一目标严格优于超过该目标 `comparisonTolerance`；全目标差异均在容差内＝互不支配（无差别并列，§9.3）；软约束只警告/次级排序（SYM-OPT-009）不参与支配；不出现加权总分；Pareto 集结论措辞固定为"当前变量域、算法与计算预算内发现"（§9.4 步骤 7）
- **实施步骤：**1) RED：写 `StaticParetoTest`（三项指标、容差支配、软约束、禁加权、措辞）；2) 建含支配/互不支配/容差边界候选的 `testdata/optimization/pareto/` 黄金集；3) 实现指标视图（OPT-B 三项，阶段 C/D 五项引用即拒）；4) 实现 Pareto 支配计算与并列语义；5) 三形式命令转绿并写证据
- **RED 测试：**`StaticParetoTest`（先写先败）：`OnlyThreeStageBMetricsComputable`（三项可算；五项 StageC/D 引用 → `IRD-OPT-STAGE-LOCKED`）、`DominanceRequiresToleranceExceededStrictWin`（严格优于必须超过该目标 `comparisonTolerance` 才计入支配）、`AllWithinToleranceMeansMutuallyNonDominated`（全目标差异在容差内＝互不支配并列）、`SoftConstraintNeverChangesFeasibility`（违反只警告/次级排序）、`NoWeightedAggregateScore`（代码与输出无加权总分字段/排序键）、`ConclusionWordingFrozen`（措辞固定）
- **最小实现：**指标视图＋支配计算转绿；缓存与确定性（T05）、结果落库与应用（T06）不在本卡
- **正常/边界/失败测试：**
  - 正常：Given 含明显支配关系的候选集，When 计算，Then 非支配集合与黄金集一致、每指标带 `ResultRef` 来源
  - 边界：Given 两候选全部目标差异恰在容差内，Then 互不支配并列（同层排序稳定）；Given 差异恰超容差，Then 计入支配
  - 失败：Given StageB 研究定义引用五项阶段 C/D 指标之一，When 校验，Then `IRD-OPT-STAGE-LOCKED` 拒绝并引导改选可算目标
- **精确验证命令**（仓库根、VS x64；三形式，仅用登记目标）：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_definition_test$'`；`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_definition_test`；`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_definition_test$"`；预期退出码 0
- **diff 和禁止项检查：**diff 仅含允许清单；`rg -ni "weight(ed)?|score|sum \*|total" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/optimization/candidate/src/StaticPareto.cpp; if ($LASTEXITCODE -gt 1) { throw '扫描命令执行失败' }` 无加权总分路径；`minJointMargin` 只从 WP-15 结果读取（无本地裕量公式复制）；软约束不进可行性与支配判定分支
- **证据工件：**`plugins/optimization/out/test-evidence/wp-20/<run-id>/`——静态 Pareto 黄金集（含支配/并列/容差边界）、指标来源对照、`comparisonTolerance` 取值评审（优化工程师签署）、测试日志
- **提交格式：** `WP-20-T04: 实现静态 Pareto 与支配排序`

  - 新增静态 Pareto 支配排序与稳定并列
  - 新增排序确定性测试
  - 新增运行证据记录
- **停止与升级条件：**目标聚合或 `comparisonTolerance` 语义无法从需求 §9.3 推导、或全局默认激活目标三项口径冲突时，停止并升级优化工程师产品评审；不得以加权评分临时替代 Pareto
