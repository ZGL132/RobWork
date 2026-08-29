# WP-09-T05 静态一致性扫描

- **Task ID / 需求 ID / ADR / 阶段：**WP-09-T05；ERR-01、NFR-MNT-03、NFR-SEC-07；ADR-004（诊断唯一所有权）；阶段 A / R1
- **基线 commit：**代码 `94fb910e8d4b1e2bb84d569cbca4aa623cbd2844`（基线业务插件硬编码状态词与中文文案并存）；契约 `architecture/public-interfaces.md` §8、`architecture/testing-contract.md` §1；方案 `module-design/diagnostics.md` v0.3 §7
- **前置任务及必需工件：**WP-09-T01（13 字段 Schema）、WP-09-T02（目录与术语表）、WP-09-T03（登记表与 mapper）、WP-01-T01（check-boundaries.ps1 统一扫描入口）
- **允许创建/修改/删除的文件：**根 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/diagnostics/` 下 `test/StaticConsistencyTest.cpp`、`testdata/diagnostics/scan/`；`RobWork/scripts/industrial-robot/check-diagnostics.ps1`（由 check-boundaries.ps1 调用）；删除旧资产仅限 v0.3 §7 迁移表批准项
- **禁止修改的文件和公共接口：**业务插件硬编码状态词/诊断码/单位文案（禁止新增而非本卡修改）、扩大测试字符串例外白名单、登记表既有 code、其他 WP 私有头
- **修改前接口：**无一致性扫描；状态词、诊断码与单位文案可随意硬编码进任何插件
- **修改后接口：**`check-diagnostics.ps1` 输出四类扫描结果（重复 code、硬编码中文状态词/单位文案、未登记 action、mapper 旁路）并接入 check-boundaries.ps1；违规即非零退出
- **实施步骤：**定义白名单（测试期望字符串＋诊断目录资源）→ 实现重复 code 检测 → 硬编码状态词与本地单位转换文案检测 → 未登记 action 与 mapper 旁路调用位置检测 → 接入统一入口 → 按 v0.3 §7 迁移表处置旧字符串错误资产
- **RED 测试：**`test/StaticConsistencyTest.cpp`（注册于 `sdurws_ird_diagnostics_test`）：植入含硬编码状态词、重复 code 与 mapper 旁路构造 Diagnostic 的违规夹具时扫描必须非零退出并报告文件/行号——先确认测试在无实现时失败
- **最小实现：**四类检测＋白名单机制＋脚本接线；不修改业务插件（违规仅报告）
- **正常/边界/失败测试：**正常：白名单内的测试期望字符串与目录资源扫描通过。边界：夹具含中文注释（不误报）、同文件多处违规逐一报告、白名单条目可定位到测试。失败：业务插件硬编码状态词、重复 code、本地单位转换文案、mapper 旁路 → 非零退出并指明文件与行号
- **精确验证命令：**

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\run-tests.ps1 -Configuration Debug -Regex '^sdurws_ird_diagnostics(_contract)?_test$'
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-diagnostics.ps1
ctest --test-dir out\build\industrial-robot -C Debug -R "^sdurws_ird_diagnostics(_contract)?_test$"
```

- **diff 和禁止项检查：**`git diff --name-only` 仅命中允许清单；白名单 diff 逐条可解释（无整目录放行）；check-diagnostics.ps1 已列入 check-boundaries.ps1 调用链；无登记表改动
- **证据工件：**`diagnostics/out/test-evidence/wp-09/<run-id>/`：扫描报告、白名单清单、违规夹具与输出、迁移 verdict、命令日志与评审签名
- **提交格式：**`WP-09-T05: 新增诊断一致性静态扫描`

  - 新增 check-diagnostics.ps1 四类检测与白名单机制
  - 新增 违规夹具扫描测试与目标登记
  - 新增 扫描报告与迁移 verdict 证据记录
- **停止与升级条件：**扫描误报无法解释、或需要允许业务插件自定义同义 code 时暂停并升级至 WP-09 所有者与独立测试负责人
