# WP-13-T01 模型类型与失败夹具

- **Task ID / 需求 ID / ADR / 阶段：**WP-13-T01；MDL-01～04、MDL-11～12、AT-01；阶段 B / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（`industrialrobot/plugins/modeling/` 尚不存在）；语义源 `module-design/robot-modeling.md` v0.3、`architecture/canonical-kinematics.md` §5/§9
- **前置任务及必需工件：**WP-01-T02（CMake 骨架）；WP-01-T03（`run-tests.ps1` 测试入口）；WP-02-T01（数据清单与完整性/哈希规则）；WP-02-T02（数学断言库——测地角/有向轴夹角断言）；无 WP 内前置
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/test/ModelFixtureTest.cpp`；`modeling/testdata/modeling/` 下 `dh/`、`explicit/`、`urdf/`、`axes/`、`branches/`、`materials/`、`failpoints/` 七类夹具＋夹具哈希清单；`modeling/out/test-evidence/wp-13/<run-id>/`；`modeling/CMakeLists.txt`（登记 `sdurws_ird_modeling`、`sdurws_ird_modeling_test`、`sdurws_ird_modeling_contract_test` 骨架）。禁止删除任何文件
- **禁止修改的文件和公共接口：**`include/sdurws/ird/modeling/`、`src/`、`gui/`（本卡不写实现代码）；`schemas/`（夹具同构消费但不改 schema）；WP-02 断言库；`architecture/`、`module-design/`
- **修改前接口：**无（夹具与测试目标不存在）
- **修改后接口：**`ModelFixtureTest` 固定加载入口 `loadFixture(category,name)`（返回原始字节＋哈希）；夹具集覆盖：DH/显式/URDF 任意轴、缺失轴、零轴（`‖a‖<1e-12`）、非单位轴（规范化记录）、4/7 轴边界、不支持拓扑（分支/闭环/planar/floating/Mimic）与物性失败样本
- **实施步骤：**1) 按 `schemas/examples/robot-design.example.json` 同构生成七类夹具；2) 登记夹具哈希清单（WP-02-T01 规则）；3) 编写失败断言先行测试（非法轴/拓扑断言诊断，实现未合入前 RED）；4) 过 `validate-schemas.ps1`；5) 登记目标并归档证据
- **RED 测试：**Given 零轴（`‖a‖<1e-12`）或非单位轴夹具，When 断言"进入规范模型前被拒绝或规范化并记录"，Then 测试失败（加载/校验实现不存在）——失败断言先行冻结预期（`ModelFixtureTest`）
- **最小实现：**夹具文件＋加载入口＋哈希清单；不含任何校验实现（由 T02 起转绿）
- **正常/边界/失败测试：**
  - 正常：Given 合法 4/7 轴边界模型夹具，When load，Then 字段、单位（m/rad SI）与来源（`ImportOrigin`/`ValueProvenance`）完整可读
  - 边界：Given 任意方向非单位合法轴夹具，When 校验预期冻结，Then 断言"规范化进入＋保留原始输入轴与记录"（canonical-kinematics §5，`|‖a‖−1|≤1e-15`）
  - 失败：Given 零轴/缺失关键拓扑字段/不支持拓扑夹具，When 预期冻结，Then 断言拒绝且不产生修订（诊断码预期 `IRD-MDL-IMPORT-BLOCKED`，diagnostics.md 登记后启用）
- **精确验证命令**（仓库根；夹具往返先过 Schema 校验）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\schemas\validate-schemas.ps1
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_modeling_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_modeling_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_modeling_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；夹具哈希清单与文件一一对应（`git status` 无未登记新夹具）；`rg -n "RobotDesignEditor|UrdfImportAdapter" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/test/ModelFixtureTest.cpp; if ($LASTEXITCODE -eq 0) { throw '检测到禁止实现' } elseif ($LASTEXITCODE -ne 1) { throw '扫描命令执行失败' }` 零命中（夹具独立于实现）
- **证据工件：**`modeling/out/test-evidence/wp-13/<run-id>/`——夹具清单与哈希、失败断言输出、Schema 校验日志
- **提交格式：**`WP-13-T01: 新增模型类型与失败夹具`

  - 新增 七类模型夹具、固定加载入口与哈希清单
  - 新增 失败断言先行测试与目标登记
  - 新增 夹具清单与 Schema 校验证据记录
- **停止与升级条件：**黄金预期与需求 §8.1.2/§7.3.2 语义冲突、或 schema 与夹具同构校验失败时暂停并提交 ADR；夹具集不足以覆盖后续 T02～T07 断言时补夹具须回到本卡新提交，不得在其他任务内私加
