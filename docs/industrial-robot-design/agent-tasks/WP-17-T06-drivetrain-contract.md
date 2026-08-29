# WP-17-T06 动力学到传动映射

- **Task ID / 需求 ID / ADR / 阶段：**WP-17-T06；DYN-04（关节侧结果与候选传动无关）、AT-07；ADR-004（共享语义单一权威）；阶段 C / R1。契约：`architecture/domain-model.md` §4（类型化广义力）、`architecture/public-interfaces.md` §7（`DynamicResult` 字段）、`architecture/symbol-registry.md` SYM-DYN-001/SYM-EVL-001、`module-design/dynamics.md` §3/§6（WP-17-T06 注记）、`module-design/drivetrain.md` §6（WP-18 侧断言权威）、requirements §8.5、§15.1 传动映射黄金数据
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源同 WP-17-T01
- **前置任务及必需工件：**WP-17-T03（关节侧 `DynamicResult` 完整工件）；对外与 WP-18-T01（映射语义冻结）、WP-18-T02（旋转传动映射）侧联——契约断言物理位于 WP-18 侧 `evaluation/drivetrain/test/DrivetrainContractTest.cpp`（目标 `sdurws_ird_drivetrain_contract_test`，由 WP-18 登记）
- **允许创建/修改/删除的文件**（模块根同 WP-17-T01）：
  - 创建：`test/DynamicResultContractTest.cpp`（公共头稳定化用例）、`out/test-evidence/wp-17/<run-id>/`
  - 修改：`include/sdurws/ird/dynamics/DynamicResult.hpp`（仅稳定化：只读视图字段与语义冻结，供 WP-18 契约引用、不链接实现）、`plugins/dynamics/CMakeLists.txt`（登记 `sdurws_ird_dynamics_contract_test`）。禁止删除任何文件；契约测试本体不落本 WP
- **禁止修改的文件和公共接口：**在本 WP 实现或复制传动映射（`DriveTrainMappingEvaluator` 唯一实现归 WP-18）；修改 WP-18 侧 `DrivetrainContractTest.cpp` 与 `sdurws_ird_drivetrain*` 目标；把映射依赖引入 `sdurws_ird_dynamics` 链接；T01～T05 冻结语义与 `DynamicsEvaluator` 接口；requirements/CSV/architecture
- **修改前接口：**`DynamicResult.hpp` 已随 T02/T04 提供类型化视图与仿真状态字段，但未经公共头稳定化评审、无 `_contract_test` 目标
- **修改后接口：**`DynamicResult`（规范名，symbol-registry §4.6；`DynamicsResult` 为禁止名称）公共头稳定化：关节侧字段（类型化广义力/转角/位移/转速/加速度/机械功率/能量/峰值与 RMS 包络/仿真状态）只读视图＋payload 经 `ResultEnvelope.payloadId` 引用，新增字段走契约变更；`sdurws_ird_dynamics_contract_test` 登记（公共头稳定化用例）；候选无关性断言（改变 `DriveTrainDesign` 不改变关节侧 `DynamicResult`、多速比黄金数据可复算 §15.1）由 WP-18 侧 `DrivetrainContractTest` 执行，本卡联合验收
- **实施步骤：**1) RED：写公共头稳定化用例（字段冻结/禁名/payload 引用）；2) 冻结 `DynamicResult.hpp` 只读视图；3) 登记 `_contract_test` 目标并转绿；4) 与 WP-18-T01/T02 对侧联调 `DrivetrainContractTest`；5) 经 WP-18 运行入口验证通过后写证据与双评审签署
- **RED 测试：**`DynamicResultContractTest`（先写先败）：`ReadOnlyViewFieldsFrozen`（字段集与语义冻结）、`ForbiddenNameAbsent`（`DynamicsResult` 零命中）、`PayloadViaEnvelopeRef`（payload 经 payloadId 引用、无内嵌副本）、`DynamicsLibHasNoDrivetrainDep`（`sdurws_ird_dynamics` 不链接 WP-18）
- **最小实现：**公共头稳定化＋本侧契约用例转绿；映射实现与多速比黄金数据（WP-18-T02/T05）不在本卡
- **正常/边界/失败测试：**
  - 正常：Given 完整循环 `DynamicResult`，When WP-18 侧凭公共头＋`IResultRepository` 消费，Then 映射可复算且与 §15.1 黄金数据一致
  - 边界：Given 仅改变 `DriveTrainDesign`（如 `axes[i].ratio`）重算，Then 关节侧 `DynamicResult` 逐位不变（候选无关性）
  - 失败：Given 映射依赖渗入 dynamics 构建或 WP-18 复算不一致，When 契约测试运行，Then 记录证据并停止，不得测试侧归一化
- **精确验证命令**（仓库根、VS x64；三形式）：
  - 本侧（仅用登记目标）：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_dynamics_contract_test$'`；`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_dynamics_contract_test`；`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_dynamics_contract_test$"`
  - 契约断言运行入口（WP-18 §9 命令，目标归 WP-18）：`powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_drivetrain(_contract)?_test$'`；`cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_drivetrain_contract_test`；`ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_drivetrain(_contract)?_test$"`
  - 预期两者退出码均 0
- **diff 和禁止项检查：**diff 仅含允许清单；`efficiency|efficiency|\bratio\b" plugins/dynamics/` 零命中（映射归 WP-18）；`DynamicsResult` 禁名零命中；未改动 WP-18 目录任何文件
- **证据工件：**`out/test-evidence/wp-17/<run-id>/`——候选无关性联合验收记录（动力学/驱动工程师＋独立测试双评审签署）、映射消费接口评审纪要、公共头稳定化记录、两侧测试日志（commit/配置）
- **提交格式：** `WP-17-T06: 固化传动映射动力学契约测试`

  - 新增传动映射契约测试目标
  - 新增关节侧只读视图契约用例
  - 新增运行证据记录
- **停止与升级条件：**WP-18 侧联目标未就绪或复算不一致时，停止并双升 WP-17/WP-18 所有者；`DynamicResult` 需改字段时先走 contract-registry 契约变更，不得在测试侧归一化掩盖差异
