# WP-20-T01 研究定义

- **Task ID / 需求 ID / ADR / 阶段：**WP-20-T01；OPT-01、OPT-02（OPT-B 子集，需求 §8.7.1 唯一集合）；ADR-003（OPT-B 权威范围）；阶段 B / R1。契约：`architecture/candidate-compilation.md` §1～§3、`architecture/symbol-registry.md` SYM-OPT-001/011、`schemas/optimization-study.schema.json`
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/optimization.md` v0.3（WP-20 部分）＋`work-packages/WP-20-optimization-definition.md`（D6）
- **前置任务及必需工件：**无 WP 内前置；交付前置 WP-13～15（本卡直接消费：WP-13-T01 `RobotDesign` 夹具、WP-14-T01 `EngineeringRequirements` 模型）；平台前置 WP-03-T01（core）、WP-09-T01（`Diagnostic`）、WP-01-T03（测试入口）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/optimization/definition/include/sdurws/ird/opt/StudyDefinition.hpp`、`VariableBindingRegistry.hpp`、`StudyValidation.hpp`，`definition/src/StudyDefinition.cpp`、`VariableBindingRegistry.cpp`、`StudyValidation.cpp`，`test/StudyDefinitionTest.cpp`，`testdata/optimization/studies/`，`plugins/optimization/CMakeLists.txt`（登记 `sdurws_ird_optimization_definition`、`sdurws_ird_optimization_definition_test`；`_contract_test` 由 WP-20-T08 登记，`joint/` 目标归 WP-21 不得创建）；修改 `industrialrobot/CMakeLists.txt`（仅追加子目录接入）。禁止删除任何文件
- **禁止修改的文件和公共接口：**`architecture/candidate-compilation.md` 冻结字段；WP-13/14 聚合类型；`schemas/optimization-study.schema.json`；requirements/CSV；`joint/` 目录与 `sdurws_ird_optimization_joint*` 目标
- **修改前接口：**无（旧 `sdurws_structureoptimizer*` 研究私有模型按模块详设 §7 Rewrite 后删除，不在本卡）
- **修改后接口：**`OptimizationStudyDefinition`（SYM-OPT-001）校验：字段按 candidate-compilation §1 冻结（`studyId`、`baselineRevisionRef`、`studyDefinitionVersion`、`variables[]`、`hardConstraints[]`、`softConstraints[]`、`metrics[]`、`objectives[]`、`budget`、`algorithmPolicy`）；`VariableBindingRegistry` 首批 8 行条目（`robot.section-dimension`/`robot.dh-length-offset`/`robot.joint-install`/`robot.joint-range`/`robot.base-pose`+`robot.tcp-offset`/`robot.link-material`/`drivetrain.ratio` 为 StageB，`drivetrain.reducer-key`+`drivetrain.motor-key` 为 StageD）；writeSet＝parameterKey 展开的物理字段全集、两两交集必须为空；派生路径 mass/COM/inertia 不入 writeSet；改型项目默认锁定非授权参数（OPT-02）；`StudyValidation` 顺序：绑定注册→writeSet 互斥→DAG 无环→阶段锁→预算/种子/版本
- **实施步骤：**1) RED：写 `StudyDefinitionTest` 逐项拒绝断言并登记目标；2) 实现绑定注册表与首批条目（含 valueType/unit 校验器）；3) 实现字段模型与 `StudyValidation` 固定顺序校验；4) 建合法/非法研究 JSON 样例进 `testdata/optimization/studies/`；5) 三形式命令转绿并写证据
- **RED 测试：**`StudyDefinitionTest`（先写先败）：`RejectsUnregisteredBinding`（→ `IRD-OPT-UNREGISTERED-BINDING`，不落盘、无部分状态）、`RejectsWriteSetOverlap`（→ `IRD-OPT-WRITE-CONFLICT`）、`RejectsDependencyCycle`（→ `IRD-OPT-CYCLE`）、`RejectsDomainViolation`（→ `IRD-OPT-DOMAIN-VIOLATION`）、`RejectsStageDInOptB`（→ `IRD-OPT-STAGE-LOCKED`）、`RejectsOutOfScopeReference`（`targetObjectId` 不属基线作用域→Input 诊断）、`LocksUnauthorizedParamsByDefault`（锁定变量不进 `DesignVector` 且激活条件不满足）、`ZeroDistinctFromUnset`（candidate-compilation §2 零值语义前置断言）
- **最小实现：**定义模型＋注册表＋校验器转绿；候选生成与补丁编译在 T02
- **正常/边界/失败测试：**
  - 正常：Given 合法研究定义（StageB 变量＋预算＋种子），When 校验并保存，Then 通过、非授权字段默认锁定且可追溯、JSON 样例与 Schema 一致
  - 边界：Given 变量值为 0，When 校验，Then 视为合法设置值（非未设置）；Given 空软约束/单目标，Then 合法
  - 失败：Given 未注册路径、writeSet 交集、依赖环、域越界或 StageD 绑定，When 校验，Then 对应 `IRD-OPT-*` 诊断、无部分状态、不落盘
- **精确验证命令**（仓库根、VS x64；三形式，仅用登记目标）：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_optimization_definition_test$'`；`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_optimization_definition_test`；`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_optimization_definition_test$"`；预期退出码 0
- **diff 和禁止项检查：**diff 仅含允许清单；`grep -rn "OptimizationStudy\b" plugins/optimization` 无单独 `OptimizationStudy` 类型（禁止名，SYM 裁决 #9）；无反射式字段写入（注册表外路径全部拒绝）；`joint/` 目录零新增
- **证据工件：**`plugins/optimization/out/test-evidence/wp-20/<run-id>/`——研究定义 JSON 样例（合法＋逐项非法）、校验矩阵（拒绝码×输入）、首批绑定条目表与评审签署、测试日志
- **提交格式：** `WP-20-T01: 定义优化研究定义对象`

  - 新增 OptimizationStudyDefinition 与字段冻结
  - 新增构造校验测试
  - 新增运行证据记录
- **停止与升级条件：**字段路径或锁定规则无法从 candidate-compilation §1～§3 推导、或 `links[i].section` 依赖的 `robot-design.schema.json` 增补（D5 提名）未落地时，停止并升级 WP-20 所有者；绑定条目扩充须走注册表评审，不得运行时动态注册
