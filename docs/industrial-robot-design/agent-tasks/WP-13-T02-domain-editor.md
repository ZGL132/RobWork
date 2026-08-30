# WP-13-T02 领域编辑器

- **Task ID / 需求 ID / ADR / 阶段：**WP-13-T02；MDL-01、MDL-08、MDL-13、需求 §7.1/§8.1.1；阶段 B / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/robot-modeling.md` v0.3、`architecture/public-interfaces.md` §1
- **前置任务及必需工件：**WP-13-T01（七类夹具＋失败断言合入）；WP-10-T01（`EditDraft` 公共头——代码前置）；WP-04-T02（`DomainCommand`/`IProjectCommandService` 公共头——端口契约经公共头合法可用，非代码前置）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/include/sdurws/ird/modeling/RobotDesignEditor.hpp`、`ModelingCommands.hpp`、`ModelingDiagnostics.hpp`；`modeling/src/RobotDesignEditor.cpp`、`ModelingCommands.cpp`；`modeling/test/DomainEditorTest.cpp`；`modeling/CMakeLists.txt`（仅追加本任务文件）。禁止删除任何文件
- **禁止修改的文件和公共接口：**`RobotDesign`/`JointDefinition` 权威类型定义（WP-03 公共头）、`DomainCommand` 基类签名（public-interfaces §1）、WP-04 持久化实现、WP-10 接口；`schemas/`、`architecture/`、`module-design/`；禁止插件内平行命令基类
- **修改前接口：**无（编辑器与命令不存在；旧链路 `JointTransformSpec`（RPY+Pos+隐式 Z 轴）待 Rewrite）
- **修改后接口：**`RobotDesignEditor::fromDraft(EditDraft)->expected<RobotDesignDraft,Diagnostics>`（base revision＋编辑操作序列＋逐项诊断）；`RobotDesignDraft::buildCommand()->ApplyRobotDesignEdit`（`DomainCommand` 子类：`validate/buildMutations` 纯函数）；`ModelingDiagnostics.hpp` 首批诊断映射（对接 WP-09 `Diagnostic`）；MDL-09 规则：`ExplicitJoint` 下 Axis 一等可编辑、`StandardDH` 下只读派生
- **实施步骤：**1) 草稿校验（唯一 `robotId`、目标主链 4～7 可动关节、关节限制、来源保留）；2) 非法草稿零命令零失效分支；3) 命令 DTO 构建（幂等 `commandId`）；4) T01 失败断言转绿并补 DomainEditorTest；5) 诊断映射首批条目
- **RED 测试：**Given 重复 `robotId` 或可动关节数不足 4 的草稿，When `buildCommand`，Then 拒绝（Input 诊断定位字段）、修订号不变、下游零失效（`DomainEditorTest` 先行）
- **最小实现：**草稿校验＋命令构建两条路径；不做 URDF 导入、DH 转换与编译（后续任务）
- **正常/边界/失败测试：**
  - 正常：Given 合法草稿，When 应用（命令服务替身），Then `validate/buildMutations` 通过、恰好一个新修订
  - 边界：Given `StandardDH` 权威草稿，When 尝试编辑 Axis，Then 拒绝并提示"只读派生字段"（MDL-09）；`ExplicitJoint` 下 Axis 编辑成功且保留原始输入轴与 `ValueProvenance`
  - 失败：Given 非法草稿（限制缺失/引用悬空），When 应用，Then 零修订、诊断逐项可定位、旧修订保持
- **精确验证命令**（仓库根）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_modeling_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_modeling_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_modeling_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`rg -n "class.*DomainCommand" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/include/` 命中处仅继承声明（无平行基类）；`rg -n "JointTransformSpec" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/; if ($LASTEXITCODE -eq 0) { throw '检测到禁止实现' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中（§13.3 消除项）；`rg -n "robwork|rw::" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/src/RobotDesignEditor.cpp; if ($LASTEXITCODE -eq 0) { throw '检测到禁止实现' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中（无 RobWork 头入计算核心）
- **证据工件：**`modeling/out/test-evidence/wp-13/<run-id>/`——草稿状态矩阵（合法/非法逐项）、命令日志、诊断样本、T01 失败断言转绿记录
- **提交格式：**`WP-13-T02: 新增领域编辑器`

  - 新增 RobotDesignEditor 草稿校验与 ApplyRobotDesignEdit 命令实现
  - 新增 非法草稿零命令测试及目标登记
  - 新增 草稿状态矩阵与诊断样本证据记录
- **停止与升级条件：**公共字段与 WP-03 权威类型或 public-interfaces §1 不一致时暂停并升级架构评审；需要新增跨模块符号时先登记 symbol-registry，不得直接进公共头
