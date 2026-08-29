# WP-13-T07 单机械臂与身份回归

- **Task ID / 需求 ID / ADR / 阶段：**WP-13-T07；ARC-04、MDL-08、MDL-14、AT-18（阶段 B 子链路）、需求 §7.1（objectId 规则）；阶段 B / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/robot-modeling.md` v0.3、`architecture/symbol-registry.md`（SYM-ID-001）
- **前置任务及必需工件：**WP-13-T06（`ModelingCompileRequest`/编译衔接工件合入）；WP-13-T01 夹具框架（回归夹具经其登记）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/test/IdentityRegressionTest.cpp`；扩充 `modeling/testdata/modeling/`（复制/导入/删除/重命名/目标链切换回归夹具，登记哈希）；`modeling/out/test-evidence/wp-13/<run-id>/`；`modeling/CMakeLists.txt`（仅追加测试文件）。禁止删除任何文件；禁止修改任何 `src/`/`include/` 实现（发现缺陷回对应任务卡修复）
- **禁止修改的文件和公共接口：**`ObjectId` 生成算法与规则（WP-03-T02 所有）；WP-04/06 实现与公共头；`architecture/`、`module-design/`
- **修改前接口：**无（身份回归矩阵不存在）
- **修改后接口：**`IdentityRegressionTest` 固定矩阵：复制（跨项目→新 ID＋原 ID 记录于来源）、导入（重新导入→新 ID）、删除（ID 不复用、历史结果绑定原 ID 显示为历史证据）、重命名（`objectId` 不变、触发名称表重编译、sliceHash 不变）、目标链切换
- **实施步骤：**1) 用 T02/T04/T06 已合入入口编排五类操作序列；2) 断言矩阵（每格：操作前后的 `objectId`/`RuntimeNameMap`/sliceHash/修订数）；3) 多可动分支样本仅保留导入证据、不进入首版计算模型；4) 输出 AT-18 阶段 B 子链路报告
- **RED 测试：**Given 重命名对象，When 重新解析名称，Then 旧绑定消失、新绑定生效、`objectId` 与 sliceHash 不变——依赖 T06 未完成时本测试 RED（`IdentityRegressionTest` 先行编写）
- **最小实现：**无产品代码；仅测试编排与断言（缺陷以失败测试形式回投对应任务卡）
- **正常/边界/失败测试：**
  - 正常：Given 复制/导入序列，When 重新解析，Then 新对象生成新 `objectId`、来源信息记录原 ID、ID 规则符合需求 §7.1
  - 边界：Given 删除后新建同名对象，When 查询，Then 旧 ID 不复用、历史结果仍绑定旧 ID 并显示为历史证据；多可动分支未选链时保留证据不建模
  - 失败：Given 重命名后旧名称查询，When resolve，Then `IRD-NAME-UNRESOLVED`（不返回旧绑定）；断言失败即缺陷回投，不在本卡内修补实现
- **精确验证命令**（仓库根）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_modeling_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_modeling_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_modeling_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单（不得含 `src/`/`include/` 变更）；夹具哈希登记完整；矩阵每格断言含期望值（无占位断言）
- **证据工件：**`modeling/out/test-evidence/wp-13/<run-id>/`——身份回归矩阵（五操作×断言列）、AT-18 阶段 B 子链路报告、独立验证者签署
- **提交格式：**`WP-13-T07: 新增身份回归测试覆盖`

  - 新增 五操作身份回归矩阵与回归夹具（登记哈希）
  - 新增 IdentityRegressionTest 测试及目标登记
  - 新增 AT-18 阶段 B 子链路报告证据记录
- **停止与升级条件：**发现跨 WP 身份语义不一致（WP-03/04/06 对 `objectId` 或名称行为解释不同）时暂停并升级架构裁决；矩阵揭示实现缺陷时开缺陷回投单，不在本卡内改实现
