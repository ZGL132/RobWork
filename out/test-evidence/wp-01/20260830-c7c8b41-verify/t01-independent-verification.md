# WP-01-T01 独立验证复跑记录

- Task ID：WP-01-T01（构建边界失败测试）；执行日期：2026-08-30
- 实现提交：`c7c8b4126a0bb76b08edcf96fe2ff4e1142d8762`（基线 `ac49d911e8aede1d5ab63688927c55a258a31573`）
- 验证上下文：独立验证者/治理协调（ZCode 治理会话，与实施会话分离）
- 执行环境：Windows 11 专业版（NT 10.0.26200），Windows PowerShell 5.1.26100.9168；隔离 detached worktree 于实现 SHA 上复跑
- 本记录位置说明：实现证据按卡内指定存放于 `out/logs/industrial-robot/20260830-193442/`；本文件为独立验证者按 AGENTS §5.3 与账本 Done 规则的规范证据根 `out/test-evidence/wp-01/<run-id>/` 补立的复跑记录，两类路径口径差异已作为非阻断观察移交卡片所有者

## 1. 实现提交范围与格式检查

- 提交仅含 9 个新增文件：`RobWork/scripts/industrial-robot/{check-boundaries.ps1,common.ps1}`、`docs/industrial-robot-design/fixtures/wp-01/boundaries/{old-plugin-dependency,widget-header,unregistered-library,name-concatenation}/` 各一个单违规文件、`out/logs/industrial-robot/20260830-193442/` 三份证据工件——全部在卡内允许清单内
- 禁改文件零变化：`rwslibs/CMakeLists.txt`、旧插件源码、`requirements.md`、`requirement-traceability.csv`、`validate-development-docs.ps1`、`fixtures/wp-00/`（`git diff c7c8b41^ c7c8b41 -- <路径>` 为空）
- 提交格式符合卡内要求（`WP-01-T01: 构建边界失败测试`＋中文分条正文）；`git diff --check` 干净
- CSV SHA-256 复核：`0725D6EE05C5D4A5B9A9AD73E5D2499D2A0FB98FD41E4F957D18B1255F848C88`，与 T02 入库哈希一致

## 2. 精确验证命令复跑（实现 SHA 隔离 worktree，仓库根）

命令 1（正式树，缺省 ScanRoot；目标骨架由 WP-01-T02 交付前为空集＋R7 自扫描）：

```text
$ powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1
扫描根尚未创建（目标骨架由 WP-01-T02 交付）: <worktree>\RobWork\RobWorkStudio\src\rwslibs\industrialrobot
Boundary scan passed: 0 files scanned, root=<default-root-missing>
exit code: 0
```

命令 2（四夹具逐一 `-ScanRoot`，预期非零＋关键词）：

```text
old-plugin-dependency → exit 1｜[R2 旧插件目标依赖] CMakeLists.txt:6 核心链接旧插件目标：sdurws_robotmodelbuilder
widget-header         → exit 1｜[R1 QWidget/QApplication 头包含] include\sdurws\ird\core\Sample.hpp:4 #include <QWidget>
unregistered-library  → exit 1｜[R3 未登记 target] CMakeLists.txt:6 链接未登记 target：some_unregistered_lib
name-concatenation    → exit 1｜[R4 运行时名称拼接] plugins\sample\NameConcat.cpp:8 return robotName + "." + localName;
```

四夹具与实现证据 `boundary-fixtures.log` 逐字一致（规则、文件、行号、修复动作均同）；每夹具恰一条违规，无错报扩散。

失败场景（卡内失败测试，非零＋IO 诊断）：

```text
$ ... check-boundaries.ps1 -ScanRoot .\nonexistent-dir-xyz
[R0 IO] 扫描根不存在或不可读: .\nonexistent-dir-xyz；请检查 -ScanRoot 参数。
exit code: 1
```

## 3. 禁项与规则集检查

- 扫描器/公共脚本无 `offscreen`、`Start-Job`、`Start-ThreadJob`、`-Parallel` 字面量（R7 匹配串按拼接构造防自命中；`boundary-clean-tree.log` 令牌等价断言＋自扫描零命中与本验证 grep 一致）
- 扫描器无自动修复行为；输出仅"规则＋文件＋行＋修复动作"诊断
- 头注释符合规范：职责边界、允许/禁止依赖、所有者、契约引用（public-interfaces §6.3、testing-contract、WP-01 §2/§7）、Task ID、失败行为
- 规则集 R1～R7 与卡内"修改后接口"六类诊断一致；R5/R6/R7 无夹具属预期（正式树暂无 CMake 内容，随 WP-01-T02 起生效，映射表已注明）

## 4. RED→GREEN 因果与文档门禁

- 每夹具仅注入一种违规且独立成目录：扫描器若缺失对应规则，该夹具将退出 0 而使断言失败——本验证四夹具全部非零命中，GREEN 成立；RED 骨架断言（骨架返回 0 时四断言全败）由实现证据与夹具构造共同佐证
- 文档门禁 `validate-development-docs.ps1` 复跑：退出码 0，成功行 `128 requirements, 19 acceptance tests, 25 contracts, 76 symbols, 5 ADRs, 0 trace gaps`

## 5. 结论与签署

**独立验证通过。** 实现范围、接口、证据与提交格式符合卡内要求；全部精确命令在实现 SHA 上复跑通过。

签署（独立执行上下文）：独立验证者/治理协调（ZCode 治理会话，2026-08-30）。本签署支持账本 `Done` 登记，不等于最终评审。
