# REUSE-T13：基础平台首包——S6 编译与关节偏置

基线 `a045120547704d4a40752728632334f56e0b9f5c`；分支 `codex/reuse-foundation`；现场 `D:/10_Source_Repos/21_robot/reuse-foundation`。本记录只覆盖当前能力，不另建全项目台账。

## 定向交接索引（适用上述基线）

- 主实施链/覆盖清查：集成 `82cd82ff7`；查 `../REUSE-T01/README.md`、`../../../reuse/functions/`、`../REUSE-T11/README.md` 及 `../../../reuse/extensions/required-proposals.md`。T01入口为历史快照；RPY两项关闭以REUSE-T11独验为准，其余清查/缺口保持未决，由协调会话分配；不阻断本次局部正确性修复。
- drivetrain REUSE-T12：集成 `bc212f245`；查 `../../../tasks/REUSE-T12.json`、`../../../reuse/drivetrain-linalg-migration.md`、`../drivetrain-linalg/README.md`。已有42项业务/15项契约和断开原件；性能NOT RUN、独验待完成。后续集成曾报告drivetrain/CMakeLists.txt覆盖缺口；当前基线门禁核查结果另列，不能因合入消除语义债务。责任归协调会话安排的drivetrain实施/独验者，不由本包接管。
- trajectory REUSE-T09：集成 `8db7a7e78`、`aee6ac5bf`、`b0997f8d8`；查 `../../../tasks/REUSE-T09.json`、`../../../reuse/trajectory-polynomial-migration.md`、`../REUSE-T09/README.md`。已有189项业务/69项契约、断开及性能原件；catalog verified已整合，不重复翻转。未查到独立验收记录，留协调会话安排；不自签、不把ready当done。
- EAA最新决议：`../REUSE-T10/withdrawal-acceptance.json` 绑定撤销提交54f0beda6。上游保持原状；局部表示转换1e-6不适用于本包FK/编译链、合法性或错误语义。

## 实施与适配边界

所有者已批准将 `WorkCellCompiler.cpp` 必要实审与迁移纳入同一个有界能力包。保留 CanonicalModel→WorkCell 唯一构造链、名称、限位 `[qmin-offset,qmax-offset]`、速度/加速度、默认 q_rw=0、原受控视图与图元规则。删除旧 `jointStaticTransform` 的无条件旋转偏置、轴对齐的自持轴角/近极 snap、OBJ 逐三角复制和失去用途的 `meshDigestTag`；关节运动、坐标对齐和几何转换分别直接消费真实上游 Joint、Transform3D、Model3D 结果。临时文件及 Windows locale 仅作局部环境适配，不新增模型/缓存/DTO/公共门面。

具体文件/符号/目标见 `../../../reuse/catalog.json` 的 RUNTIME-WC-MODEL、RUNTIME-AXIS-ALIGN、RUNTIME-MESH-LOAD、RUNTIME-GEOMETRY-CARRIER 及 `../../../reuse/functions/runtime.json`；逐文件凭证 `../../../reuse/changes/REUSE-T13.json`。保留项也已实审登记，不为了行覆盖重写。所有权授权仅落实原 SA-19 runtime-owner 的精确文件/目标，不改变原架构权限。无公共接口或产品 CMake 依赖变化；实现版本按既有键机制升 /1.1。下游 kinematics/trajectory/dynamics 使用原端口，不自行补偿。

发现分类、旧路径/源码证据、候选适用性、缓存键、非重叠保留理由及平台限制只记在 [suitability.md](suitability.md)，不另建总台账。轴/模型/FK验收仍为1e-9；EAA的1e-6不外扩。一般轴内部关节帧滚转变化、OBJ改Indexed、非Windowsmesh并发和上游mask测试待归因均须供独验核对，不隐藏差异。

## 原始证据与当前结论

本包使用隔离 `.cache/integrated` 集成树及 `.cache/smoke` 冒烟树；没有写主树构建。原工具链、依赖路径、命令见 `configure.log`、`smoke-configure*.log`、`verify-production.py`、`final-runs.json`、`runs.json`，构建日志保留。

| 检查 | passed / skipped / failed | 原件与适用范围 |
| --- | --- | --- |
| 原实现反例（红测） | 1 / 0 / 1 | `offset-red.xml`、`offset-red.log`：移动offset失败、旋转回归通过；不与最终用例累计 |
| 最终集成runtime | 305 / 0 / 0 | `runtime.xml`、`runtime.log`、`runtime-report.json`、`restore-build.log`；排除 worker 占位单独直接运行，父用例仍真实派生子进程并通过 |
| 真实生产聚合断开（预期失败） | 19 / 0 / 13 | `disconnected.xml`、`disconnected.log`、`disconnected-source.txt`、`disconnected-build.log`；32项，13条真实断言失败，随后原源码恢复构建并运行上述正常结果 |
| 对端modeling | 474 / 0 / 0 | `modeling.xml`、`modeling.log`、`modeling-report.json`；适用相同最终产品实现路径，后补runtime测试断言未改对端路径，不重复跑 |
| 独立冒烟runtime | 195 / 0 / 0 | `smoke.xml`、`smoke.log`、`smoke-report.json`、`smoke-build.log`；原冒烟条件不编译S6，后补集成测试不进入冒烟目标 |
| 直接复用上游原测试 | 8 / 0 / 1 | `upstream.xml`、`upstream.log`、`upstream-report.json`；Model3D.constructor的mask期望/默认值差异待归因，不能称上游全绿 |
| 场景性能对照 | 执行成功，不设通过计数 | `runs.json`、`perf-*`；区间重叠，见适用性说明，不据小样本宣称稳定改善/退化 |
| 独立验收 | NOT RUN | 由所有者另开会话/人工；本会话不自签、不closed |

旧记录若写“304项均通过”，应读作当时报告的303 passed + 1 worker占位skip；不能抹掉skip。本表是新增用例及明确过滤占位后的305 passed，不是将历史skip改成pass。早期 `semantic*.xml` 等失败原件保留，包括测试首次未限定visual而计入两个引用的断言错误，修正后最终报告如上；未伪称这些中间失败为产品故障。

`package-diagnostic.ps1` 只从原门禁 AST 载入原函数并检查本包7文件，不改校验规则，不是CI入口，不替代全量门禁。它检查全部矩阵的结构/绑定并对本包做完整行覆盖；全量继承的 drivetrain 覆盖FAIL另记。正式准入仍要求全量门禁和独验。

门禁自查：`package-diagnostic-final.log` 为本包原函数范围诊断 PASS（权限仅登记4个实际使用受限上游的精确文件，其余3文件不授冗余权限）；`ird-gates.log` 为架构引擎 R1/R2/R3/R4/R5/T1/T2/SUB/GRAPH/LIB/SA02 零命中。`final-reuse.log`、`validate-docs.log` 与 `validate-task-r3.log` 均 FAIL，原因是同一继承的 drivetrain/CMakeLists.txt REUSE-COVERAGE，由协调会话安排负责人，本包不代改、不关闭。故不宣称整体门禁/正式准入 PASS。

`validate-task.ps1` 的旧分支正则拒绝根 AGENTS 已允许、所有者明确指定的 codex 分支；本包只对齐命名句法，主线写入/所有权/算法迁移门禁不变。`branch-contract-test.ps1` 对真实校验器的隔离结构夹具执行9项正/负例，全通过；main、redesign-main、路径穿越、空主题/空格及.lock均拒绝，原 wp/doc 分支仍接受。真实 T13 契约继续触发完整 H1-H6，仍如实继承上述 FAIL。foundation-governance矩阵只刷新受影响区段/输入摘要，保留原 pending 状态，不借此宣称治理脚本已迁移关闭。

## 后续基础平台边界

本包不是全部基础平台完成。定向承接的下一组事项：core 的现有单位/比较/身份/事件审计，diagnostics 的日志边界审计，evidence 的结果/provenance 权威及 runtime/policy 既有端口。正确的既有复用直接保留；不把业务算法搬入平台。已知 F-966（极值比较）、F-969（日志保留总数）、FNV128 身份问题及事件生命周期差异应按原审计提出有界处理；不在本包夹带新核心，不把这些旧债标为已关闭。跨单元drivetrain/T09遗留仍归协调会话安排。

回滚：只回退本包产品/矩阵/凭证，不恢复EAA补丁、不回退其他三包。独验由所有者另开会话或人工完成，本记录不自签PASS或closure。
