# WP-13-T03 URDF 导入

- **Task ID / 需求 ID / ADR / 阶段：**WP-13-T03；MDL-03、MDL-11、MDL-12、AT-15/AT-17、需求 §8.1.2；阶段 B / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；语义源 `module-design/robot-modeling.md` v0.3、`architecture/canonical-kinematics.md` §5/§8
- **前置任务及必需工件：**WP-13-T02（`RobotDesignDraft`/命令构建工件）；WP-11-T05（`ResourceImportService` 安全读取端口——预算、禁 DOCTYPE/外部实体/网络已在 IO 层承担）
- **允许创建/修改/删除的文件：**创建 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins/modeling/include/sdurws/ird/modeling/UrdfImportAdapter.hpp`；`modeling/src/UrdfImportAdapter.cpp`；`modeling/test/UrdfImportTest.cpp`；`modeling/testdata/modeling/urdf/`（含缺失轴/零轴/continuous/planar/多分支样本）；`modeling/evidence/WP-13/T03/`；`modeling/CMakeLists.txt`（仅追加本任务文件；WP-11 端口契约三例入 `sdurws_ird_modeling_contract_test`）。禁止删除任何文件
- **禁止修改的文件和公共接口：**WP-11-T05 已合入接口（本模块不做第二套 XML/URDF 解析）、WP-06 名称解析实现、`RobotDesign` 权威类型；`schemas/`、`architecture/`、`module-design/`
- **修改前接口：**无（旧导入器缺失 `<axis>` 时用局部 +Z——§13.3 待 Rewrite 行为）
- **修改后接口：**`UrdfImportAdapter::import(SafeResource)->expected<RobotDesignDraft,UrdfImportReport>`；`UrdfImportReport`（模块私有）：Error/Warning/Info 条目、字段路径、源值/采用值、原因、建议动作、被排除分支清单；确认记录持久化前禁止 Verified
- **实施步骤：**1) 经 `ResourceImportService` 取安全字节，只做语义映射（`<origin>`＝父 Link→零位 Joint Frame 变换、`<axis>` 在 Joint Frame 表达、SI 单位）；2) 轴规则按 §8.1.2（缺失→+X＋`Defaulted` 待确认草稿；零/非法轴仅报告并阻止新修订；非单位规范化并报告）；3) `continuous` 保留语义、prismatic/fixed 类型不降级；4) 分支拓扑：多可动分支须用户显式选链，被排除分支完整报告；planar/floating/Mimic/闭环在目标链阻止成模（不转 FixedFrame）；5) 报告确定性（同输入条目序一致）
- **RED 测试：**Given 缺失 `<axis>` 的 revolute 关节，When import，Then 采用局部 +X 并生成 `Defaulted` 警告的待确认草稿（明确断言不是 +Z）（`UrdfImportTest` 先行）
- **最小实现：**单链 revolute/continuous/prismatic/fixed 映射＋报告生成；URDF→DH 不做（归 T04 可表达性验证）
- **正常/边界/失败测试：**
  - 正常：Given 合法单链 4～7 轴 URDF（任意有限非零轴），When import，Then 形成 `ExplicitJoint` 权威草稿、报告逐项可观察（AT-15/AT-17）
  - 边界：Given 零轴/非法数值样本，When import，Then 完整导入报告生成、`IRD-MDL-IMPORT-BLOCKED`（Input/Error）、零新修订；`continuous` 保留语义并要求用户确认工作范围
  - 失败：Given 目标链含 planar/floating/Mimic/闭环，When import，Then 阻止形成可计算 `RobotDesign`、被排除分支逐项报告、旧修订不变
- **精确验证命令**（仓库根；含消费 WP-11 端口的契约测试目标）：
  ```powershell
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_modeling(_contract)?_test$'
  cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_modeling_test sdurws_ird_modeling_contract_test
  ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_modeling(_contract)?_test$"
  ```
- **diff 和禁止项检查：**diff 仅含允许清单；`grep -rn "QFile\|ifstream\|ifstream.*urdf" modeling/src/UrdfImportAdapter.cpp` 零命中（一律经 WP-11 端口）；`grep -rn "DOCTYPE\|QXmlStreamEntityResolver" modeling/src/` 零命中（IO 层职责）；`grep -rni "+Z\|0,0,1" modeling/src/UrdfImportAdapter.cpp` 零命中（无 +Z 默认残留）
- **证据工件：**`modeling/evidence/WP-13/T03/`——URDF 样本哈希、映射报告（逐条目）、被排除分支清单、确认记录占位、AT-15/AT-17 断言输出
- **提交格式：**`WP-13-T03: add urdf adapter`
- **停止与升级条件：**轴/分支语义与需求 §8.1.2 冲突、或 RobWork 轴约定无法由 canonical-kinematics §5/§7 证明时暂停并提交 ADR；样本出现 §8.1.2 未覆盖的关节语义时升级需求评审，不得自行猜测降级
