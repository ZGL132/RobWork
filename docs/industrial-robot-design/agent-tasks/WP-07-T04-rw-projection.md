# WP-07-T04 RobWork 适配投影

- **Task ID / 需求 ID / ADR / 阶段：**WP-07-T04；ARC-05、CON-06、NFR-COR-05、NFR-MNT-07；ADR-004（单一权威：XML 不是第二策略源）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（基线 `robotmodelbuilder/RobotModelBuilderWidget.hpp` 等直接读写 `CollisionSetup`/`ProximitySetup` XML，XML 即权威）；契约 `architecture/public-interfaces.md` §6；方案 `module-design/policy-collision.md` v0.3 §5-5
- **前置任务及必需工件：**WP-07-T01（规范化策略）、WP-07-T02（配对语义）、WP-06-T02（RuntimeNameMap/`IRuntimeNameResolver`）、WP-01-T05（RobWork 依赖基线）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/` 下 `include/sdurws/ird/policy/CollisionPolicyAdapters.hpp`、`src/RobWorkCollisionAdapter.cpp`、`test/RobWorkProjectionTest.cpp`、`testdata/policy/xml/`
- **禁止修改的文件和公共接口：**把 XML 设置变成第二权威、静默选择冲突配置、`RuntimeNameMap` 与名称解析规则、GUI、RobWork 库本身、`schemas/engineering-policy.schema.json`
- **修改前接口：**`CollisionSetup`/`ProximitySetup` 由各插件手工构造与导出 XML，导入时后写覆盖先写，冲突无诊断
- **修改后接口：**`CollisionPolicyAdapters` 从同一策略＋RuntimeNameMap 确定性生成 `CollisionSetup`、`ProximitySetup` 与路径验证 profile；XML 导入仅合并为策略草稿，enabled/pair/距离冲突返回诊断并要求显式选择；投影后逐项反解与策略规范 JSON 对比
- **实施步骤：**规范化策略＋名称映射 → 反解对象 ID → 构造 RobWork setups → 导入 XML 为草稿 → 逐字段比较 → 报告冲突并产出草稿 → 往返反解校验与策略 hash 一致
- **RED 测试：**`test/RobWorkProjectionTest.cpp`（注册于 `sdurws_ird_policy_test`）：未知 runtime name 投影必须报 `IRD-NAME-UNRESOLVED` 且无正式 setup（沿用 public-interfaces §2 冻结码）；两份 XML 在 enabled/pair/distance 冲突时必须返回冲突诊断与草稿，不静默择一——先确认测试在无实现时失败
- **最小实现：**三 setup 生成器＋XML 草稿合并＋冲突检测＋往返反解；不实现 Provider 入口（T05）
- **正常/边界/失败测试：**正常：合法策略投影后 pair、enabled、distance 与名称可反解且与策略 content hash 一致。边界：空配对策略、XML 与策略完全一致（无冲突直通）、仅顺序差异。失败：名称无法反解报 `IRD-NAME-UNRESOLVED`、字段冲突报 `IRD-POLICY-CONFLICT`、RobWork 投影构造失败报 `IRD-POLICY-BACKEND-UNAVAILABLE`（System），均不产生正式 setup
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_policy(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_policy_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_policy(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；adapter 不写入项目 revision；无对 XML 值的无诊断覆盖；RobWork 头仅出现在 adapter 实现文件
- **证据工件：**`policy/evidence/WP-07/T04/`：策略/setup 往返 JSON、冲突清单、名称反解记录、RobWork 版本、命令日志与评审签名
- **提交格式：**`WP-07-T04: implement RobWork policy projection`
- **停止与升级条件：**RobWork 设置无法表达策略字段、或需要增加未批准默认值时暂停并升级至 WP-07 所有者与 RobWork 依赖基线评审
