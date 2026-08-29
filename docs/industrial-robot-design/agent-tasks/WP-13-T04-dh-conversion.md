# WP-13-T04 DH 转换

- **Task ID / 需求 ID / ADR / 阶段：**WP-13-T04；MDL-02、MDL-09、MDL-10、AT-16、需求 §7.3.2（五状态判定）/§8.1.1/§15.3；阶段 B / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/robot-modeling.md` v0.3、`architecture/canonical-kinematics.md` §3/§8
- **前置任务及必需工件：**WP-13-T02（`RobotDesignDraft`/命令构建工件）；WP-13-T01 的 `dh/`、`explicit/` 夹具；无外部门禁
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/include/sdurws/ird/modeling/DhConversionService.hpp`、`modeling/src/DhConversionService.cpp`、`modeling/test/DhConversionTest.cpp`；修改 `modeling/src/ModelingCommands.cpp`（仅追加 `SetKinematicAuthority` 命令）与 `modeling/include/sdurws/ird/modeling/ModelingCommands.hpp`（仅追加该命令声明）；扩充 `modeling/testdata/modeling/{dh,explicit}/`（经哈希清单登记）；`modeling/evidence/WP-13/T04/`；`modeling/CMakeLists.txt`（仅追加本任务文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**`KinematicAuthorityKind`/`StandardDH`/`ExplicitJoint`（SYM-KIN-001～003，WP-06/WP-13 共有）权威类型定义；`DomainCommand` 基类；WP-03/04 公共头；`schemas/`、`architecture/`、`module-design/`
- **修改前接口：**无（转换服务不存在；"从浮点变换反推覆盖原生 DH"旧行为已删除，回归测试固定）
- **修改后接口：**`DhConversionService::toExplicit(StandardDH)->RobotDesignDraft`（确定性生成）；`analyze(ExplicitJoint)->DhConversionVerdict`；`DhConversionVerdict`：`status ∈ {Exact, ExactNonUnique, Approximate, NotRepresentable, AnalysisFailed}`（需求 §7.3.2 五状态）＋逐项残差＋规范化方案＋算法版本；`SetKinematicAuthority` 命令：一个新修订、旧修订保留转换证据
- **实施步骤：**1) DH→显式按统一 schilling 约定 `Rz(theta)·Tz(d)·Tx(a)·Rx(alpha)` 确定性生成（§8.1.1）；DH `offset` 只参与生成 q=0 的 `OriginPose`（canonical-kinematics §3）；2) 显式→DH 可表达性分析（先分析、再预览、后切换），`ExactNonUnique` 规范化初版＝"优先取与前一关节轴重合或平行、且使 a≥0 的方案"；3) 往返等价验证（FK＋世界轴线＋附属 Frame）；4) `AnalysisFailed` 故障注入通道；5) `SetKinematicAuthority` 命令接线
- **RED 测试：**Given 不可表达模型（分支拓扑或非零 Y 偏移），When `analyze`，Then 判 `NotRepresentable`、不提供切换、权威模型不变（`DhConversionTest` 先行）
- **最小实现：**单自由度串联分解＋五状态判定＋残差报告；不承诺反算逐项恢复 DH 数值
- **正常/边界/失败测试：**
  - 正常：Given 四类黄金模型（Exact/ExactNonUnique/Approximate/NotRepresentable），When `analyze`，Then 判定、逐项残差与算法版本同输入下确定一致；`Exact/ExactNonUnique` 往返满足 §15.3 转换容差（Zero/Home/有限边界＋固定种子 100 姿态 FK、世界轴线、附属 Frame）
  - 边界：Given `Approximate` 判定，When 尝试设为权威，Then 拒绝（MDL-10，`IRD-MDL-CONVERSION-INEXACT`，Engineering/Warning）且只读显示残差；`ExactNonUnique` 未经用户确认规范化方案不得切换
  - 失败：Given 求解器/资源故障注入，When `analyze`，Then `AnalysisFailed`（System/Error，`IRD-MDL-CONVERSION-FAILED`）、不伪装成 `NotRepresentable`、修订不变
- **精确验证命令**（仓库根；夹具先过 Schema 校验）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\schemas\validate-schemas.ps1
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_modeling_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_modeling_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_modeling_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`grep -rn "acos(" modeling/src/DhConversionService.cpp` 零命中（姿态比较用测地角，§15.3）；`grep -rn "Origin.*AxisRotation\|q_zero" modeling/src/DhConversionService.cpp` 零命中（无双偏置链，canonical-kinematics §3）；`AnalysisFailed` 分支不得含 `NotRepresentable` 返回值
- **证据工件：**`modeling/evidence/WP-13/T04/`——四类黄金模型判定与残差表、固定种子 100 姿态 FK 对照、往返容差实测、故障注入日志、算法版本记录
- **提交格式：**`WP-13-T04: implement dh conversion`
- **停止与升级条件：**`ExactNonUnique` 规范化规则未冻结（ADR 未签署）或容差不满足 §15.3 时暂停；判定状态集合需扩展时升级需求 §7.3.2 变更评审，不得新增第六状态
