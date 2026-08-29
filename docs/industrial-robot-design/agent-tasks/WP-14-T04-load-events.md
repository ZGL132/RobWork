# WP-14-T04 负载与工艺事件

- **Task ID / 需求 ID / ADR / 阶段：**WP-14-T04；REQ-02、REQ-04、REQ-06（DataInsufficient 通道）、需求 §7.2；阶段 B / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/requirements-definition.md` v0.3 §3/§5
- **前置任务及必需工件：**WP-14-T01（`LoadCase` 字段骨架与校验链工件）；WP-13-T05（交付前置：`ToolDefinition`/TCP 与负载引用语义已冻结——无业务插件代码依赖，经修订查询消费）
- **允许创建/修改/删除的文件：**修改 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/requirements/include/sdurws/ird/requirements/LoadEvent.hpp`（补全实现）；创建 `requirements/src/LoadEvent.cpp`；`requirements/test/LoadEventTest.cpp`；`requirements/testdata/requirements/loads/`；`requirements/evidence/WP-14/T04/`；`requirements/CMakeLists.txt`（仅追加本任务文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**T01 冻结字段；WP-13-T05 的 `ToolDefinition` 权威类型（引用消费，不复制工具几何）；`EngineeringStatus`/`PayloadCompleteness` 枚举（WP-03）；`schemas/`、`architecture/`、`module-design/`；禁止动力学求值（轨迹/动力字段只保存并校验）
- **修改前接口：**`LoadCase` 仅为字段骨架（T01）
- **修改后接口：**`LoadCase{loadCaseId, payloadMass, payloadCenterOfMass(显式 frameId), externalWrench(force N/torque N·m, 显式 frameId), dwellTime, events[](grip/release，挂接 taskId 与序列)}`；`LoadCase::buildTimeline()->expected<TaskTimeline,Diagnostics>`（顺序、持续时间、引用稳定）；缺失关键负载数据→就绪语义 DataInsufficient（归 T05 消费）
- **实施步骤：**1) 补全事件字段校验（taskId 存在、序列单调、`dwellTime>0`、wrench 有限）；2) 接近/撤离沿工具轴或参考轴表达方向与距离（REQ-02）；3) 夹取/释放/驻留构成完整任务循环的口径断言（REQ-04）；4) 时间线构建与循环口径冻结；5) 缺失数据诊断（不静默默认）
- **RED 测试：**Given 缺 `payloadMass` 或 COM 参考系的负载数据，When 构建时间线/供就绪检查，Then 返回 DataInsufficient 语义诊断（不补零、不静默默认）（`LoadEventTest` 先行）
- **最小实现：**事件校验＋时间线构建＋缺失数据诊断；不做任何力求值
- **正常/边界/失败测试：**
  - 正常：Given 完整事件序列（接近→grip→作业→release→撤离＋驻留），When `buildTimeline`，Then 顺序、持续时间与引用稳定（同输入序列化一致）
  - 边界：Given 沿工具轴的接近/撤离（方向＋距离表达），When 校验，Then 方向为单位向量、距离有限非负；同一 taskId 挂接多事件按序列有序
  - 失败：Given grip 挂接不存在的 `taskId` 或序列冲突，When 校验，Then `IRD-REQ-REFERENCE-UNRESOLVED`/`IRD-REQ-ROW-INVALID`（Input/Error）、定位到事件序号、零修订
- **精确验证命令**（仓库根）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_requirements_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_requirements_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_requirements_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`grep -rn "torque(\|solve\|integrate\|simulate" requirements/src/LoadEvent.cpp` 零命中（无求值）；`grep -rn "0.0 *;.*// *default\|default.*mass" requirements/src/LoadEvent.cpp` 零命中（无静默默认值）
- **证据工件：**`requirements/evidence/WP-14/T04/`——事件时间线样例、缺失数据报告、循环口径说明（驻留＋工况切换）、诊断样本
- **提交格式：**`WP-14-T04: model load events`
- **停止与升级条件：**驻留与循环口径不一致、或与 WP-13-T05 交付的工具/负载引用语义冲突时暂停并升级联合评审；缺数据是否阻断正式运行的最终判定在 T05，本卡只保证诊断可见且不默认
