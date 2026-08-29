# WP-07-T05 统一入口与静态扫描

- Task ID：WP-07-T05
- 需求/阶段：ARC-05、CON-06、NFR-MNT-07；阶段 A / R1
- 架构契约：`architecture/public-interfaces.md`、`architecture/testing-contract.md`；模块方案：`module-design/policy-collision.md`
- 前置：WP-07-T01～T04、WP-01 边界扫描入口。
- 允许：修改 `policy/include/.../IEngineeringPolicyProvider.hpp`、`src/PolicyProvider.cpp`、`test/PolicyEntryTest.cpp`、`scripts/industrial-robot/check-policy-ownership.ps1`（由 WP-01 调用）。
- 禁止：业务插件新增 collision enabled/safety distance/excluded 默认值、修改需求、手工 CSV 或直接写项目文件。
- 产出：按 project/branch/revision/snapshot 查询策略的统一 Provider、私有开关扫描和旧链路迁移证据。

## 数据流

`provider(ref/snapshot) -> load immutable policy -> expose const interface -> consumers call CollisionEvaluator`。扫描白名单仅允许 WP-07 policy 和 RobWork adapter 目录出现规则转换。

## Given/When/Then

- Given不存在策略或 revision 不匹配，When provider query，Then返回结构化诊断，不创建默认策略。
- Given业务插件包含本地 enabled、distance、excluded 或 `stripDeviceScope` 逻辑，When scan，Then非零退出并报告文件/行号。
- Given纯显示开关变化，When provider/evaluator query，Then revision、sliceHash、缓存和碰撞结论不变。

## 测试、证据与提交

命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_policy_entry_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-policy-ownership.ps1
```
证据：Provider 查询日志、扫描报告、旧链路迁移 verdict 和评审签名。提交：`WP-07-T05: enforce unified policy ownership`。

停止：发现多个公共策略所有者或扫描需要扩大业务插件白名单时暂停。
