# WP-01-T01 构建边界失败测试

- **Task ID / 需求 ID / ADR / 阶段：** WP-01-T01；ARC-02（主：业务插件仅经第 6.3 节稳定端口协作）、NFR-MNT-02（插件不含其他插件 Widget 头）、NFR-MNT-07（名称拼接/剥离静态扫描）；无直接关联 ADR；阶段 A 前提 / R1。契约：`architecture/public-interfaces.md`、`architecture/testing-contract.md`。
- **基线 commit：** 代码基线 94fb910e8d4b1e2bb84d569cbca4aa623cbd2844；文档基线：main 当前 HEAD
- **前置任务及必需工件：** WP-00-T04；工件：`evidence/wp-00/t03-gate-and-fixtures.md` 与 `evidence/wp-00/t04-independent-review.md`（门禁与独立评审通过）。本任务先于 CMake 骨架（WP-01-T02）执行，不依赖任何构建目标。
- **允许创建/修改/删除的文件：**
  - 创建：`RobWork/scripts/industrial-robot/check-boundaries.ps1`、`RobWork/scripts/industrial-robot/common.ps1`（仅路径解析与日志公共函数）
  - 创建夹具：`docs/industrial-robot-design/fixtures/wp-01/boundaries/{old-plugin-dependency,widget-header,unregistered-library,name-concatenation}/`（各含一个最小 CMakeLists.txt 或源/头文件，仅注入一种违规）
  - 写运行日志：`out/logs/industrial-robot/<timestamp>/`
- **禁止修改的文件和公共接口：** `RobWork/RobWorkStudio/src/rwslibs/CMakeLists.txt` 与旧插件源码（`sdurws_robotmodelbuilder`、`sdurws_engineeringrequirements`、`sdurws_kinematicanalysis`、`sdurws_structureoptimizer*` 相关目录）；`requirements.md`、CSV、文档门禁脚本；扫描器不得自动修复源码或依赖清单。
- **修改前接口：** 无（新增脚本与夹具）。
- **修改后接口：** `check-boundaries.ps1 [-ScanRoot <路径>]`（缺省扫描 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/` 与安装规则），违反任一规则即非零退出并列出文件与行：核心公共头包含 QWidget/QApplication/旧插件头；核心链接旧插件或未登记 target；业务插件自行拼接运行时名称；业务插件声明碰撞默认值/安全距离/排除规则；安装规则含测试数据/私有头/绝对构建路径；脚本出现 offscreen 或 GUI 并行。
- **实施步骤：**
  1. 建 4 个夹具，各仅注入一种违规。
  2. 写 `check-boundaries.ps1` 骨架（恒返回 0），对 4 夹具运行，确认"必须非零"断言全部失败（RED）。
  3. 实现逐规则扫描器与稳定诊断（文件＋行＋规则名＋修复动作）。
  4. 对正式树运行，确认当前合法样例通过（退出码 0）。
  5. 保存夹具与正式树两组日志到 `out/logs/industrial-robot/<timestamp>/`。
- **RED 测试：** 夹具断言（先于扫描器实现）：`old-plugin-dependency`、`widget-header`、`unregistered-library`、`name-concatenation` 四夹具退出码必须为非零——骨架返回 0 时断言失败；诊断关键词分别为"旧插件目标依赖""QWidget/QApplication 头包含""未登记 target""运行时名称拼接"。
- **最小实现：** 仅实现四类违规的扫描、诊断与 `-ScanRoot` 参数；依赖缺版本/许可证/哈希/审批的审计规则由 WP-01-T05 扩展，offscreen/GUI 并行等脚本规则在本任务一并落地，其余不实现。
- **正常/边界/失败测试：**
  - 正常：Given 干净正式树，When 运行扫描器（缺省 ScanRoot），Then 退出码 0。
  - 边界：Given 每个夹具仅一种违规，When 以 `-ScanRoot` 指向夹具运行，Then 非零且诊断只命中该违规（无误报扩散）。
  - 失败：Given 扫描目标文件缺失或不可读，When 运行扫描器，Then 非零并输出 IO 诊断，不静默通过。
- **精确验证命令：**（仓库根目录、VS x64 PowerShell；本任务早于统一测试入口脚本与 CMake 骨架，验证命令仅为扫描器自身——CMake/CTest 目标自 WP-01-T02 起存在）
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1`；预期退出码 0。
  - `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\RobWork\scripts\industrial-robot\check-boundaries.ps1 -ScanRoot .\docs\industrial-robot-design\fixtures\wp-01\boundaries\old-plugin-dependency`（其余三个夹具目录逐一替换）；预期每个夹具退出码非零且诊断命中关键词。
- **diff 和禁止项检查：** `git diff --name-only` 仅含 `RobWork/scripts/industrial-robot/check-boundaries.ps1`、`common.ps1`、`docs/industrial-robot-design/fixtures/wp-01/boundaries/`；`rwslibs/CMakeLists.txt` 与旧插件目录零变化；脚本自身不含 offscreen 平台设置或并行 GUI 启动逻辑。
- **证据工件：** `out/logs/industrial-robot/<timestamp>/boundary-fixtures.log`（4 夹具退出码与诊断原文）、`boundary-clean-tree.log`（正式树退出码 0）、规则-夹具映射表（含每条规则的文件/行定位示例）。
- **提交格式：** `WP-01-T01: 构建边界失败测试`
- **停止与升级条件：** 边界规则无法从 WP-01 计划 §2/§7 与 `architecture/public-interfaces.md` 推导，或消除违规必须改动旧插件或 rwslibs/CMakeLists.txt 时，停止并升级给工作包所有者；扫描器实现者不得同时担任 WP-01-T02 验证者。
