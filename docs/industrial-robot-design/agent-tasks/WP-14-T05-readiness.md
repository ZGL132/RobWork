# WP-14-T05 就绪状态机

- **Task ID / 需求 ID / ADR / 阶段：**WP-14-T05；REQ-06（两级状态机）、AT-02、需求 §5.4；阶段 B / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/requirements-definition.md` v0.3 §3/§4
- **前置任务及必需工件：**WP-14-T01（校验链）、WP-14-T03（区域/采样预算）、WP-14-T04（负载数据通道）工件合入；WP-10-T01（`StageStatusModel` 八值契约——聚合状态映射目标，代码前置）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/requirements/include/sdurws/ird/requirements/ReadinessChecker.hpp`；`requirements/src/ReadinessChecker.cpp`；`requirements/test/ReadinessTest.cpp`；`requirements/testdata/requirements/ready-matrix/`；`requirements/evidence/WP-14/T05/`；`requirements/CMakeLists.txt`（仅追加本任务文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**`StageStatusModel`（WP-10 所有，只做映射消费）；`EngineeringStatus` 等公共枚举（WP-03）；T01～T04 冻结字段；`architecture/`、`module-design/`；禁止执行调度（归 WP-08）
- **修改前接口：**无（就绪判定不存在；旧链路无"预览 vs 正式"区分）
- **修改后接口：**`ReadinessChecker::entryLevel(条目)->{Valid, Invalid(行/列定位)}`；`ReadinessChecker::aggregate(全部条目)->{输入未完成, 可计算}` 并映射 `StageStatusModel` 八值；`ReadinessChecker::previewScope(条目集)->Valid 子集`（部分预览只取 Valid、定位全部错误行）
- **实施步骤：**1) 条目级判定（复用 T01 校验链＋行/列定位）；2) 聚合规则：任一启用 Must 非法→"输入未完成"（阻止正式运行，`IRD-REQ-NOT-READY`）；仅 Should 非法→"可计算"＋警告可见；3) 预览作用域（不产生正式 Pass/Verified/报告证据）；4) 聚合→八值映射表；5) ready-matrix 夹具全矩阵
- **RED 测试：**Given 存在非法 Must 条目，When 请求正式运行，Then 聚合＝"输入未完成"、`IRD-REQ-NOT-READY`（Input/Error）阻止调度并返回逐项诊断（`ReadinessTest` 先行）
- **最小实现：**两级判定＋映射表＋预览作用域；不接调度器
- **正常/边界/失败测试：**
  - 正常：Given 全部条目 Valid，When 聚合，Then "可计算"，映射 `StageStatusModel` 对应值
  - 边界：Given 仅 Should 非法，When 聚合，Then "可计算"＋警告逐条可见；预览取 Valid 子集同时定位全部错误行，且预览输出不进入正式证据（AT-02）
  - 失败：Given 非法 Must，When 正式运行请求，Then 阻止＋逐项诊断（对象/实际值/要求值/建议动作）；聚合不得显示"可计算"伪造可算状态
- **精确验证命令**（仓库根）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_requirements_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_requirements_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_requirements_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`grep -rn "submit\|scheduler\|run(" requirements/src/ReadinessChecker.cpp` 零命中（不调度）；映射表覆盖 `StageStatusModel` 全部八值（无缺省分支）
- **证据工件：**`requirements/evidence/WP-14/T05/`——就绪状态机全矩阵（非法 Must/仅 Should/预览/正式×条目组合）、八值映射对照、预览不产正式证据的断言输出
- **提交格式：**`WP-14-T05: implement readiness state`
- **停止与升级条件：**状态组合未被 `StageStatusModel`（WP-10）或 REQ-06 两级语义覆盖时暂停并升级联合评审；不得新增第三层状态或私有枚举绕过映射
