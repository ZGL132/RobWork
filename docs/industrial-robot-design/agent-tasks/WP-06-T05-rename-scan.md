# WP-06-T05 重命名回归与静态扫描

- Task ID：WP-06-T05
- 需求/阶段：ARC-03、ARC-04、CON-06、NFR-MNT-07；阶段 A / R1
- 架构契约：`architecture/public-interfaces.md`、`architecture/execution-model.md`、`architecture/testing-contract.md`；模块方案：`module-design/runtime-model.md`
- 前置：WP-06-T02、WP-06-T03、WP-05 输入切片契约。
- 允许：修改 `runtime/test/RenameScanTest.cpp`、`testdata/runtime/names/`、`scripts/industrial-robot/check-runtime-names.ps1`（由 WP-01 统一入口调用）。
- 禁止：在业务插件新增前缀拼接、修改历史 snapshot、手工修正 sliceHash 或删除旧链路而不留迁移证据。
- 产出：重命名稳定性测试、历史名称兼容策略和静态扫描报告。

## 数据流

`old revision/name map -> rename command -> new map -> compile -> compare objectId/canonical/sliceHash`。历史快照保留旧 map；新执行只使用新 map。扫描只允许 resolver/adapter 目录出现拼接/剥离逻辑。

## Given/When/Then

- Given localName/deviceName 重命名，When compile，Then objectId、物理输入和 sliceHash 不变，旧历史仍可查询。
- Given新旧名称同时存在、双前缀或 `stripDeviceScope` 链路，When scan/resolve，Then失败并指出文件与行号。
- Given合法 resolver 实现，When scan，Then仅允许白名单目录中的名称转换，其他目录退出码非零。

## 测试、证据与提交

覆盖 Arm→ArmA、Joint1→JointRenamed、历史快照、旧前缀和跨模块违规。命令：
```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_rename_scan_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-runtime-names.ps1
```
证据：重命名前后 hash、历史 map、扫描报告、违规样例和评审签名。提交：`WP-06-T05: enforce rename stability and name scan`。

停止：发现物理切片因显示名称变化而失效，或扫描规则需要扩大白名单时暂停并报告。
