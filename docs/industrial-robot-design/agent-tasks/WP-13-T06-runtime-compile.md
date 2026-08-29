# WP-13-T06 运行时编译

- **Task ID / 需求 ID / ADR / 阶段：**WP-13-T06；MDL-06、MDL-14、需求 §7.3.1/§8.1；阶段 B / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/robot-modeling.md` v0.3、`architecture/public-interfaces.md` §2、`architecture/symbol-registry.md` §4 裁决 5
- **前置任务及必需工件：**WP-13-T02（草稿/命令）、WP-13-T03（URDF 草稿）、WP-13-T04（DH 转换）、WP-13-T05（物性/工具）工件全部合入；WP-06-T01（`CanonicalModelCompiler` 端口）、WP-06-T03（WorkCell/DWC 确定性双编译）、WP-06-T02（`RuntimeNameMap`）——代码前置
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/include/sdurws/ird/modeling/ModelingCompileRequest.hpp`；`modeling/src/ModelingCompileRequest.cpp`；`modeling/test/RuntimeCompileTest.cpp`；`modeling/out/test-evidence/wp-13/<run-id>/`；`modeling/CMakeLists.txt`（仅追加本任务文件；WP-06 端口契约三例入 `sdurws_ird_modeling_contract_test`）。禁止删除任何文件
- **禁止修改的文件和公共接口：**WP-06 编译端口与名称解析实现（`CanonicalModelCompiler`/`CompiledRobotArtifacts`/`IRuntimeNameResolver`）、`DomainCommand`/`IProjectCommandService`；`schemas/`、`architecture/`、`module-design/`；禁止自行拼接/剥离运行时名称、RobWork 头进入本模块计算核心
- **修改前接口：**无（编译请求适配层不存在；旧 `sdurws_robotmodelbuilder` 目标不作依赖）
- **修改后接口：**`ModelingCompileRequest{robotDesignContentIdentity, targetArtifactSet}`；`ModelingCompileRequest::submit()->expected<CompiledRobotArtifacts,ProjectError>`（调 WP-06 端口；工件＝canonical＋names＋WorkCell＋DWC＋诊断，全成全败）；应用时序衔接：预编译成功才 `IProjectCommandService.apply`
- **实施步骤：**1) 组装编译请求（`RobotDesign` 内容身份＋目标工件集）；2) 调 WP-06 端口并透传诊断；3) 实现"任一工件失败→整体为空→不 apply"时序（robot-modeling.md §4 固定时序）；4) failpoint 注入开关（测试专用）覆盖双编译两侧；5) 名称映射交叉校验清单
- **RED 测试：**Given DWC 编译 failpoint 触发，When submit 后应用，Then 全败零修订、`CompiledRobotArtifacts` 整体为空、修订号不变（`RuntimeCompileTest` 先行）
- **最小实现：**请求组装＋端口调用＋全败短路与 apply 衔接；不实现编译本身（WP-06 职责）
- **正常/边界/失败测试：**
  - 正常：Given 合法 `RobotDesign`，When submit，Then 返回完整 `CompiledRobotArtifacts`，`RuntimeNameMap` 双向一致（§15.3 名称映射口径）、apply 恰好一个新修订
  - 边界：Given 重命名后的设计，When submit，Then 名称表重编译、`objectId` 不变、sliceHash 不变；交叉校验清单（关节/Frame/限位/几何/碰撞规则/动力学引用逐项经 `objectId` 解析为同一 `RobotName.LocalName`）
  - 失败：Given 任一引用非法或 WorkCell failpoint，When submit，Then 无部分工件、`IRD-MDL-TOOL-REF-UNRESOLVED`/WP-06 诊断透传、修订不变
- **精确验证命令**（仓库根；含 WP-06 端口契约测试）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_modeling(_contract)?_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_modeling_test sdurws_ird_modeling_contract_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_modeling(_contract)?_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`rg -n "rw::|<rw/" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/include/sdurws/ird/modeling/ RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/src/ModelingCompileRequest.cpp` 零命中（RobWork 指针只存在于 WP-06 builder 内）；`rg -n "\+ *\"\.\" *|substr.*prefix|removePrefix" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/src/` 零命中（无名称拼接/剥离）；`CompiledCandidateArtifact` 不得出现于本模块（symbol-registry 裁决 5）
- **证据工件：**`modeling/out/test-evidence/wp-13/<run-id>/`——failpoint 注入日志（双侧）、名称映射双向表、交叉校验清单、编译日志与修订计数
- **提交格式：**`WP-13-T06: 新增运行时编译请求适配`

  - 新增 ModelingCompileRequest 组装与全败短路时序实现
  - 新增 failpoint 注入测试及契约目标登记
  - 新增 名称双向表与交叉校验清单证据记录
- **停止与升级条件：**WP-06 端口未冻结或签名变更、全成全败语义无法保证（如端口返回部分工件）时暂停并升级 WP-06/WP-13 联合评审
