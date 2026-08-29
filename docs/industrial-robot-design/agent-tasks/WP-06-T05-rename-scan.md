# WP-06-T05 重命名回归与静态扫描

- **Task ID / 需求 ID / ADR / 阶段：**WP-06-T05；ARC-03、ARC-04、CON-06、NFR-MNT-07；ADR-001（objectId 与显示名称解耦）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（`stripDeviceScope` 存在于 `robotmodelbuilder/WorkCellConverter.cpp`）；契约 `architecture/public-interfaces.md` §2；方案 `module-design/runtime-model.md` v0.3 §5/§7
- **前置任务及必需工件：**WP-06-T02（RuntimeNameMap 与 `IRuntimeNameResolver`）、WP-06-T03（双编译工件）、WP-05-T01（EvaluatorInputSlice/sliceHash 契约）、WP-01-T01（check-boundaries.ps1 边界扫描入口）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/` 下 `test/RenameScanTest.cpp`、`testdata/runtime/names/`；`RobWork/scripts/industrial-robot/check-runtime-names.ps1`（由 check-boundaries.ps1 统一调用）；删除旧链路仅限迁移表批准项
- **禁止修改的文件和公共接口：**业务插件新增前缀拼接、历史 snapshot 格式、sliceHash 计算（WP-05）、`IRuntimeNameResolver` 签名、手工 CSV；不得删除旧链路而不留迁移证据
- **修改前接口：**`stripDeviceScope` 等前缀拼接/剥离逻辑散落在业务插件；localName/deviceName 变化会级联影响物理输入身份
- **修改后接口：**重命名只更新名称表与 `runtimeDeviceName/localName`；objectId、canonical 物理内容、sliceHash 与历史快照不变；`ResolverContractTest` 断言重命名后旧绑定消失（注册于 `sdurws_ird_runtime_contract_test`）
- **实施步骤：**构造旧 revision 名称表 → 执行重命名 → 新表编译 → 对照 objectId/canonical/sliceHash → 运行静态扫描 → 按 v0.3 §7 迁移表处置 `stripDeviceScope`（只读适配器 → 删除）
- **RED 测试：**`test/RenameScanTest.cpp`（注册于 `sdurws_ird_runtime_test`）：Given localName/deviceName 重命名，Then objectId、物理输入与 sliceHash 不变且旧历史仍可查询；Given `stripDeviceScope` 链路或新旧名并存，Then 扫描失败并指出文件与行号——先确认测试在无实现时失败
- **最小实现：**重命名回归夹具（Arm→ArmA、Joint1→JointRenamed、RobotB）＋check-runtime-names.ps1 白名单扫描（仅 resolver/adapter 所有目录允许名称转换）
- **正常/边界/失败测试：**正常：合法 resolver 实现扫描通过且其他目录无拼接。边界：历史快照保留旧 map、新执行仅用新 map、同一 objectId 禁止同时绑定新旧名、新模型不得残留旧/双前缀。失败：跨模块违规、双前缀、手工修正 sliceHash → 非零退出
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_runtime(_contract)?_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-runtime-names.ps1
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_runtime(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；`rg -n "stripDeviceScope|localName" RobWork/RobWorkStudio/src/rwslibs/industrialrobot/plugins` 仅白名单目录命中；无 sliceHash 手工修正 diff；删除旧链路的提交附迁移 verdict（Migratable/Rewrite/EvidenceOnly）
- **证据工件：**`runtime/out/test-evidence/wp-06/<run-id>/`：重命名前后 hash 对照、历史 map 样本、扫描报告、违规夹具、迁移 verdict 与评审签名
- **提交格式：**`WP-06-T05: 新增重命名稳定性保障与名称静态扫描`

  - 新增 重命名回归夹具与 check-runtime-names.ps1 白名单扫描
  - 新增 RenameScanTest 回归测试与目标登记
  - 新增 前后哈希对照与迁移 verdict 证据记录
- **停止与升级条件：**物理切片因显示名称变化失效、或扫描规则需扩大白名单时暂停并升级至 WP-06 所有者与独立测试负责人
