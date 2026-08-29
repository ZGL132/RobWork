# WP-14-T01 需求数据模型与单位校验

- **Task ID / 需求 ID / ADR / 阶段：**WP-14-T01；REQ-01～08（字段冻结）、需求 §7.1/§7.2/§15.3；阶段 B / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（`industrialrobot/plugins/requirements/` 尚不存在）；语义源 `module-design/requirements-definition.md` v0.3 §3
- **前置任务及必需工件：**WP-01-T02/T03（构建骨架与测试入口）；WP-03-T02（`ObjectId` 公共头）；WP-04-T02（`IProjectQuery` 查询端口公共头）；WP-09-T01（`Diagnostic` 公共头）；WP-11-T02（`CsvReader` 端口就绪，本卡不消费但模块外部门禁要求可用）；无 WP 内前置
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/requirements/include/sdurws/ird/requirements/RequirementsModel.hpp`、`TaskPoint.hpp`、`PoseRegion.hpp`、`LoadEvent.hpp`、`RequirementsDiagnostics.hpp`（字段骨架，后续任务补实现）；`requirements/src/RequirementsModel.cpp`、`src/TaskPoint.cpp`（骨架）；`requirements/test/RequirementsModelTest.cpp`；`requirements/out/test-evidence/wp-14/<run-id>/`；`requirements/CMakeLists.txt`（登记 `sdurws_ird_requirements`、`sdurws_ird_requirements_test`、`sdurws_ird_requirements_contract_test` 骨架）。禁止删除任何文件
- **禁止修改的文件和公共接口：**领域公共枚举（`EngineeringStatus` 等 WP-03 所有）、`schemas/engineering-requirements.schema.json`（D3 拥有，模型与 schema 对齐但不改）；`architecture/`、`module-design/`；不得修改 WP-03/04/09/11 公共头
- **修改前接口：**无（模型不存在；旧 `sdurws_engineeringrequirements` UI 表格作权威数据的链路待 Rewrite）
- **修改后接口：**冻结字段——`TaskPoint{taskId/localName, priority(Must/Should), frameId(解析为 objectId), tcpRef(ToolDefinition objectId 或 RobotDesign.defaultTcp), targetPose(单位四元数+位置), constrainedComponents ⊆ {X,Y,Z,Roll,Pitch,Yaw}, positionTolerance(>0,m)/orientationTolerance(rad,测地角), approach/retract}`；`PoseRegion`/`LoadCase` 字段骨架（T03/T04 补全）；`RequirementsModel::validate()->Diagnostics`（有限性→单位→容差>0→引用存在）
- **实施步骤：**1) 按模块详设 §3 表冻结三类模型头；2) 实现 `TaskPoint` 校验链（单位 SI、四元数＋显式参考系、RPY 仅 helper 不进持久化）；3) JSON 往返序列化（对齐 schema 与 `engineering-requirements.example.json`）；4) 诊断映射首批（`IRD-REQ-ROW-INVALID`/`IRD-REQ-REFERENCE-UNRESOLVED`）；5) 登记目标
- **RED 测试：**Given 非有限坐标/容差≤0/`tcpRef` 不可解析的任务点，When `validate`，Then Input 诊断逐字段定位且不生成任何执行工件（`RequirementsModelTest` 先行）
- **最小实现：**`TaskPoint` 校验＋JSON 往返；区域/负载仅头骨架
- **正常/边界/失败测试：**
  - 正常：Given 合法任务点/区域/负载数据，When 序列化往返，Then ID、单位、来源与版本稳定（§15.3 参数容差 1e-12，稳定 ID/枚举/引用完全一致）
  - 边界：Given 四元数 `|‖q‖−1| ∈ (1e-15,1e-12]`，When 载入，Then 重归一化并生成诊断（canonical-kinematics §6）；`constrainedComponents` 空集表示全自由（未列出即自由，REQ-01）
  - 失败：Given 引用不存在的 frameId/TCP，When `validate`，Then `IRD-REQ-REFERENCE-UNRESOLVED`（Input/Error）、恢复动作＝补齐被引对象
- **精确验证命令**（仓库根）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_requirements_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_requirements_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_requirements_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`roll.*pitch.*yaw" requirements/include/sdurws/ird/requirements/*.hpp|roll.*pitch.*yaw" requirements/include/sdurws/ird/requirements/*.hpp` 命中处仅 helper（不进持久化结构）；`activeFrame" requirements/src/|activeFrame" requirements/src/` 零命中（不得依赖"当前选中坐标系"）
- **证据工件：**`requirements/out/test-evidence/wp-14/<run-id>/`——字段矩阵（每字段×校验规则×结果）、JSON 往返样例、诊断样本
- **提交格式：** `WP-14-T01: 定义需求领域模型`

  - 新增需求条目与聚合领域模型
  - 新增模型构造与校验测试
  - 新增运行证据记录
- **停止与升级条件：**字段与 `schemas/engineering-requirements.schema.json` 或架构契约冲突时暂停并升级（schema 与模型以 D3 裁决为准）；`tcpRef` Schema 落位属 D5 提名未决时按详设 §3 注记执行并上报
