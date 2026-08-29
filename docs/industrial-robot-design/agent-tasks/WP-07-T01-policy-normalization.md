# WP-07-T01 策略规范化与冲突

- **Task ID / 需求 ID / ADR / 阶段：**WP-07-T01；ARC-05、CON-06、KIN-05、TRJ-04、UX-08、NFR-COR-05；ADR-004（策略为共享语义唯一权威）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（无 industrialrobot/policy 目录）；字段权威 requirements §6.7.2；契约 `architecture/public-interfaces.md` §6/§7；方案 `module-design/policy-collision.md` v0.3 §4
- **前置任务及必需工件：**WP-03-T03（全局评估语义枚举 EvaluationMode/EvidenceProfile）、WP-04-T02（revision 查询与 ProjectRevisionRef）、WP-01-T02（`sdurws_ird_policy` 目标骨架）、WP-01-T03（测试入口）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/` 下 `include/sdurws/ird/policy/{EngineeringPolicySet.hpp,CollisionPolicy.hpp,PolicyDiagnostics.hpp}`、`src/{PolicyNormalizer.cpp,PolicyJson.cpp}`、`test/PolicyNormalizationTest.cpp`、`testdata/policy/{pairs,profiles}/`
- **禁止修改的文件和公共接口：**requirements §6.7.2 字段定义、WP-03 枚举、WP-06 名称规则、业务插件默认值、手工 CSV、`schemas/engineering-policy.schema.json`、其他 WP 私有头
- **修改前接口：**基线策略散落为各插件 `collisionSetup.enabled`/`proximitySetup.enabled` 布尔与 XML 片段（`robotmodelbuilder/RobotModelBuilderPlugin.cpp` 等），无规范化、无 content hash
- **修改后接口：**`EngineeringPolicySet`（SYM-POL-001）与 `CollisionPolicy`（SYM-POL-002）不可变值对象，字段集 = requirements §6.7.2（collisionExecutionMode/detectorBackend+version/collisionParticipationByObject/excludedPairs[]/allowedContactPairs[]/safetyDistance/pathValidationProfile/policySchemaVersion）；规范化输出稳定 content hash
- **实施步骤：**验证来源与字段 → 校验对象引用存在于同一快照 → 按 `(ownerScopeId, objectId)` 排序配对 → 去完全重复 → 检测语义冲突（excluded∩allowed、重复规则、负/NaN 距离）→ 计算 content hash → 确定性序列化
- **RED 测试：**`test/PolicyNormalizationTest.cpp`（注册于 `sdurws_ird_policy_test`）：同一对象对同时 excluded/allowed 报 `IRD-POLICY-PAIR-OVERLAP`、未知对象报 `IRD-POLICY-UNRESOLVED-OBJECT`、冲突/无来源/重复/负 NaN 距离报 `IRD-POLICY-CONFLICT`——先确认测试在无实现时失败
- **最小实现：**字段校验＋配对排序去重＋冲突检测＋JSON 往返；不实现评估器与投影（T02/T04 范围）
- **正常/边界/失败测试：**正常：数组顺序不同但语义相同的两份输入产生相同策略 JSON 与 hash；`DisabledForDraft/Quick/Verified` 仅允许契约规定的证据等级与后端组合（`DisabledForDraft` 不与全局 EvaluationMode=Quick/Verified 混用）。边界：safetyDistance=0 合法、空配对集、schemaVersion 旧版迁移。失败：上述三类错误码 → 稳定 Input 诊断，不产生正式策略
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_policy(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_policy_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_policy(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；未触碰 requirements/schema 与 WP-06/业务插件文件；策略源码无硬编码默认安全距离或默认配对
- **证据工件：**`policy/out/test-evidence/wp-07/<run-id>/`：规范 JSON、冲突矩阵、content hash、拒绝诊断、命令日志与评审签名
- **提交格式：**`WP-07-T01: 新增策略规范化与冲突检测`

  - 新增 EngineeringPolicySet/CollisionPolicy 规范化与 content hash 实现
  - 新增 冲突矩阵失败测试与目标登记
  - 新增 规范 JSON 与拒绝诊断证据记录
- **停止与升级条件：**策略字段含义、默认值或证据等级组合未在 requirements §6.7.2 冻结时暂停，不自行添加默认规则，升级至产品负责人
