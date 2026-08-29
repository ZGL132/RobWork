# WP-07-T05 统一入口与静态扫描

- **Task ID / 需求 ID / ADR / 阶段：**WP-07-T05；ARC-05、CON-06、NFR-MNT-07；ADR-004（策略唯一所有权）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（基线各插件自带碰撞开关与默认排除规则）；契约 `architecture/public-interfaces.md` §6；方案 `module-design/policy-collision.md` v0.3 §3/§7
- **前置任务及必需工件：**WP-07-T01～T04（规范化/评估器/路径协议/投影）、WP-01-T01（check-boundaries.ps1 统一扫描入口）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/policy/` 下 `include/sdurws/ird/policy/IEngineeringPolicyProvider.hpp`、`src/PolicyProvider.cpp`、`test/PolicyEntryTest.cpp`；`RobWork/scripts/industrial-robot/check-policy-ownership.ps1`（由 check-boundaries.ps1 调用）
- **禁止修改的文件和公共接口：**`IEngineeringPolicyProvider` 冻结签名（public-interfaces §6 两个 `resolvedPolicy` 重载）、业务插件新增 collision enabled/safety distance/excluded 默认值、requirements、手工 CSV、直接写项目文件
- **修改前接口：**无统一 Provider；策略以插件局部变量与项目 XML 字段形式存在，查询路径不可审计
- **修改后接口：**`resolvedPolicy(const ProjectRevisionRef&)` 与 `resolvedPolicy(const AnalysisSnapshot&)` 只读并发安全，均不叠加私有默认值或覆盖策略字段；`PolicyProviderContractTest` 注册于 `sdurws_ird_policy_contract_test` 目标
- **实施步骤：**实现 Provider 装载不可变策略 → 暴露 const 接口 → 消费者统一调用 `CollisionEvaluator` → 编写 check-policy-ownership.ps1 白名单扫描（仅 WP-07 policy 与 RobWork adapter 目录允许规则转换）→ 按 v0.3 §7 迁移表处置旧 `collisionSetup.enabled`/`proximitySetup.enabled` 链路
- **RED 测试：**`test/PolicyEntryTest.cpp`（注册于 `sdurws_ird_policy_test`）：业务插件含本地 enabled、distance、excluded 或 `stripDeviceScope` 逻辑时扫描非零退出并报告文件/行号——先确认测试在无实现时失败
- **最小实现：**两个重载查询＋结构化错误返回＋扫描脚本；不做策略编辑 GUI（WP-10 消费）
- **正常/边界/失败测试：**正常：纯显示开关变化时 revision、sliceHash、缓存与碰撞结论均不变。边界：策略缺失 vs revision 不匹配两种结构化诊断可区分、空策略集。失败：不存在策略或 revision 不匹配返回结构化 Input 诊断（如 `IRD-PROJ-STALE-REVISION`），不创建默认策略；扫描命中违规 → 非零退出
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_policy(_contract)?_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-policy-ownership.ps1
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_policy(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；`grep -rn "setEnabled\|safetyDistance\|excluded" 业务插件目录` 零命中；Provider 无写路径；旧链路删除提交附 Migratable/Rewrite/EvidenceOnly verdict
- **证据工件：**`policy/evidence/WP-07/T05/`：Provider 查询日志、扫描报告、旧链路迁移 verdict、命令日志与评审签名
- **提交格式：**`WP-07-T05: enforce unified policy ownership`
- **停止与升级条件：**发现多个公共策略所有者、或扫描需扩大业务插件白名单时暂停并升级至 ADR-004 所有者与独立测试负责人
