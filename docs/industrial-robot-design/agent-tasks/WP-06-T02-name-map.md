# WP-06-T02 RuntimeNameMap 双向映射

- **Task ID / 需求 ID / ADR / 阶段：**WP-06-T02；ARC-03、ARC-04、CON-06、MDL-06、MDL-14、NFR-MNT-07；ADR-001（ownerScopeId 命名作用域）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`；契约 `architecture/public-interfaces.md` §2（CTR-API-002/CTR-NAM-001）；方案 `module-design/runtime-model.md` v0.3 §3
- **前置任务及必需工件：**WP-06-T01（CanonicalKinematicModel 编译产物与 canonical identities）、WP-03-T02（ObjectId 与 ownerScopeId 规则）、WP-01-T03（测试入口）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/runtime/` 下 `include/sdurws/ird/runtime/RuntimeNameMap.hpp`、`include/sdurws/ird/runtime/IRuntimeNameResolver.hpp`、`src/RuntimeNameMap.cpp`、`src/RuntimeJson.cpp`（名称表序列化部分）、`test/NameMapTest.cpp`、`testdata/runtime/names/`
- **禁止修改的文件和公共接口：**WP-03 objectId 规则、`IRuntimeNameResolver` 冻结签名（scopedName/resolve/nameMap，`expected` 返回）、`objectKind` 值域（Device/Joint/Link/Frame/FixedFrame/CompensationFrame/Tool/EnvironmentObject）、`schemas/runtime-name-map.schema.json`、各业务插件名称拼接、GUI 文本、历史快照格式、手工 CSV
- **修改前接口：**基线 `robotmodelbuilder/WorkCellConverter.cpp` 的 `stripDeviceScope` 仅单向剥离前缀；无双向映射、无 ownerScopeId、歧义名取首个匹配
- **修改后接口：**`RuntimeNameMap` 绑定主键 `(ownerScopeId, objectId)`，值为 `runtimeDeviceName/localName/runtimeScopedName/objectKind`；`<runtimeDeviceName>.<localName>` 拼接只允许出现在 resolver 实现内部；WORLD 与外部环境对象用全局名；持久化对齐 `schemas/runtime-name-map.schema.json` 与示例
- **实施步骤：**遍历 canonical identities → 校验 ownerScopeId/localName → 分配 runtimeDeviceName → 生成排序绑定表 → 构造双向索引 → 确定性序列化 → 输出 `IRuntimeNameResolver` 只读并发安全实现
- **RED 测试：**`test/NameMapTest.cpp`（注册于 `sdurws_ird_runtime_test`）：双前缀、旧前缀、去前缀重名构造必须报 `IRD-RUNTIME-NAME-COLLISION`；解析期未知/歧义名称返回 `IRD-NAME-UNRESOLVED`/`IRD-NAME-AMBIGUOUS`/`IRD-NAME-DUPLICATE-PREFIX`，绝不取第一个匹配——先确认测试在无实现时失败
- **最小实现：**绑定构建＋双向查找＋诊断；作用域隔离判定（ownerScopeId 不同 localName 相同允许，同作用域重复拒绝）；不含重命名与静态扫描（T05 范围）
- **正常/边界/失败测试：**正常：Arm、ArmA、RobotB 多作用域绑定唯一且 `scopedName/resolve` 互逆。边界：大小写差异、非法字符、绑定排序稳定、名称表 JSON 序列化往返逐字节一致、WORLD/环境对象无前缀。失败：同作用域重复 localName、双前缀、旧前缀、去前缀重名、未知 objectId/scopedName → 稳定 Input 诊断
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_runtime(_contract)?_test$'
cmake --build out\build\industrial-robot --config Debug --target sdurws_ird_runtime_test
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_runtime(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；`grep -rn "\.\|localName" business 插件目录` 确认前缀拼接仅存在于 `src/RuntimeNameMap.cpp`；schema 文件未改动
- **证据工件：**`runtime/evidence/WP-06/T02/`：binding 表、双向逆映射检查记录、拒绝诊断样例、黄金夹具哈希、命令日志与评审签名
- **提交格式：**`WP-06-T02: implement deterministic runtime name map`
- **停止与升级条件：**名称格式或多机械臂作用域语义无法由 public-interfaces §2 与 ADR-001 推导时暂停，升级至架构负责人补 ADR
