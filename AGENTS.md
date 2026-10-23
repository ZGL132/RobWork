# AGENTS.md — 最小协作规则

本仓库＝RobWork 上游框架＋工业机械臂产品。需求语义以 `RobWork/doc/industrial-robot-design/REQUIREMENTS.md` 为准，架构以 `ARCHITECTURE.md` 为准；执行流程以本文件为准。

2026-10-22 所有者要求精简治理并控制 Agent 上下文：本文取代旧版重复阅读、固定篇幅产物和所有改动统一重验的要求。旧协议与本文冲突的流程条款不再适用于普通任务；硬约束和已批准迁移阶段门继续有效。

## 0. 工作边界

- 产品源码：`RobWork/RobWorkStudio/src/rwslibs/industrialrobot/`；设计：`RobWork/doc/industrial-robot-design/`；治理脚本：`RobWork/scripts/industrialrobot/`。
- 框架及 vcpkg 只读；确需框架改动，先登记 `industrialrobot/patches/PATCHES.md` 并产出 patch。
- 一切工作基于 `redesign-main` 及其任务分支；`main` 冻结，禁止提交、合并或推送。

## 0.1 上游复用硬约束（H1—H6）

- **H1 复用优先**：先检索 RobWork/RobWorkSim，直接 API > 薄适配 > 最小上游扩展 > 新实现。后两项须缺口证据及所有者明确批准。
- **H2 禁止平行实现**：不得复制、重写或绕过上游运动学、动力学等核心。
- **H3 适配上游**：沿用上游类型、接口、构建和测试规范，保持产品中文契约及命名空间。
- **H4 全面适用**：文档、代码、测试、门禁、CI 与审查均受本节约束。
- **H5 阶段执行**：复用治理独立验收 pass 后，按已批准迁移计划推进；本轮精简不批准新核心或上游扩展。
- **H6 可追溯**：功能绑定具体上游文件、符号、目标、版本与检索证据；变动产品代码按 `reuse/policy.md` 登记逐文件凭证，逐功能补强格式见 `reuse/audit-contract.md`，不能用文件凭证冒充语义审计。

复用迁移以计算准确、架构合理、代码简洁为目标，按能力完成局部架构重构，不以调用上游或复用数量代替成果。每包实施前按 `reuse/policy.md` §1.1 明确保留/删除/适配/职责归属；允许重构旧内部结构，不扭曲业务契约、不扩大范围或权限。验收同时检查准确性、架构、简洁性及运行效果；不满足不得关闭债务或合入。

`reuse/` 位于设计区。只读当前任务涉及的 policy 章节、catalog/inventory 条目和 migration 阶段；不向上下文倾倒完整库存。`validate-reuse.ps1` 负责全量机器核查。存量债务不等于合规，历史 done 不构成豁免。

上游对象的使用/构造/写入权按 ARCHITECTURE §3.6 / SA-19；值类型、受控视图/任务私有会话、宿主精确例外分别审查，runtime 保持模型 owner。审计覆盖全部 20 单元的实际功能、工具/测试/构建，不限于 16 个已知重复文件。catalog、逐功能矩阵和 direct/adapter 正向证据按 audit-contract 演进；新增门禁独立验收前不得进入 REUSE-T02 或算法替换（migration §11），文档更新不代表机器检查已实现。无需为普通修正扩大全量必读集。

## 1. 最小上下文

1. 先看 `git status --short` 与当前分支，保留他人改动。
2. 有任务 ID：定位该契约，只读范围、需求/设计引用、前置和验证命令；普通修正与纯文档任务可直接使用用户请求，不额外编制契约。
3. 用 `rg -n` 定位相关条款，再读命中附近章节；仅在需求、接口或边界不明确时追到上层文档。
4. 补充入口：设计区 `RobWork/doc/industrial-robot-design/AGENTS.md`；源码区 `RobWork/RobWorkStudio/src/rwslibs/industrialrobot/AGENTS.md`。按实际修改区域取用，不把链接变成必读链。

默认不读整册 REQUIREMENTS/ARCHITECTURE/DTB、资产清单、依赖全图、完整 reuse 库存、历史 findings、证据索引、流水线协议或全部阶段 Skills。需要时定向检索；同一会话已读且未变的规则不重读。工具输出限于定位结果、失败摘要及必要片段，完整日志落文件。context pack 是可选缓存，用了就验证该任务包，无需刷新其他任务包。

## 2. 中文注释

产品代码必须有易懂的中文注释，解释业务意图与原因，保持需求/任务追溯，不复制设计文档长段落。

- 每个 `.hpp/.cpp` 文件头说明职责、设计依据和必要背景。
- 类/结构体说明业务概念、所有权、生命周期和线程约束；函数（含私有函数）用 Doxygen 说明职责、参数方向/范围/单位、返回及失败/异常语义。
- 复杂算法、事务、边界分支说明关键步骤和失败路径；常量说明来源。
- 物理量、坐标系、rad/deg、裸指针所有权、共享状态线程约束、错误归类、排序/种子/舍入等确定性来源必须明确。
- 测试说明需求/AT 与断言意图；CMake 说明目标注册、依赖及门禁关键步骤。禁止复述代码、过期注释及无任务编号的占位 TODO。

## 3. 代码规范

C++17；命名空间 `sdurws::ird::<unit>`；库 `sdurws_ird_<unit>`、别名 `RWS::ird::<unit>`；公共头在 `include/sdurws/ird/<unit>/`，使用 include guard。其余命名跟随所在单元。依赖只经 vcpkg；测试用 googletest（`find_package(GTest CONFIG REQUIRED)`）。调用方错误 fail-fast，环境错误用登记的单元前缀稳定诊断码，不吞错。

## 4. 按影响验证

| 改动 | 必要验证 |
| --- | --- |
| 文档/注释 | 文档门禁与链接/事实核对，无需 C++ 构建 |
| 治理脚本 | 变动脚本的正例、负例及相关文档门禁，无需无关产品构建 |
| 普通产品修正 | 集成模式受影响目标构建、相关测试、`ird_gates` 与复用检查 |
| CMake/依赖/公共接口/跨单元或核心算法 | 增加独立冒烟及对端契约回归，数值/性能验收不能省略 |
| GUI 交互 | 真实 Widgets 生产路径测试、入口/布局核查及必要人工点验，组件测试不能替代 |

任务 acceptance 与相关 verify 仍须满足；重复命令执行一次、引用同一证据即可。`gate-all.ps1` 用于整体验证，不要求每次小改全跑。证据索引深查显式运行 `validate-docs.ps1 -DeepEvidence`；普通提交不要求重建全库索引或全量刷新 context pack。

**增量范围是默认口径**：每轮实施、自查和独立验收只覆盖修改代码及实际受影响的依赖、消费者和规则；未变且不受影响的模块不编译、不重测、不重新验收。源码/依赖/工具链/配置/输入与范围仍适用的原始证据直接引用，不能只凭 HEAD 相同或不同决定重跑。公共接口、核心数值或依赖变化才扩大到相关消费者；扩大范围须说明具体影响，不默认全库构建。治理脚本只跑改动分支的正负例，不触发无关 C++/GUI 构建或全部历史夹具。

**能力批次一次收口**：同一依赖链、相同消费者及验证范围的功能合包；先稳定代码与相关测试，再一次性同步文档、矩阵和逐文件凭证，最后验证并提交。只刷新实际受影响的绑定，不重复生成历史报告/总台账。共享文件由唯一修改者批次整合，其他会话提交修改片段。实施待独验、独验通过可集成、存在明确阻断分别说明，不另造任务状态字段。

同一批次可用 `validate-docs.ps1 -TaskFile $taskFiles`（PowerShell 数组）一次完成文档、所选契约和全量复用检查，或仅用 `validate-task.ps1 -TaskFile $taskFiles` 检查契约与复用；不再另跑被包含的相同命令。单独调用默认检查不减少，最终收口门禁及独验条件不变。`verify-task.ps1` 仅复用开头严格相同、同一 PowerShell 运行时且带 `-NoProfile` 的当前任务复用前置，单列引用计数；跨运行时、更强参数、其他任务或执行过其他命令后仍实际检查。已知无关全局阻断只引用原始记录并明确责任，不反复执行相同失败命令、不冒称全局 PASS，也不自行放宽准入。

构建参数（只在需要配置时使用）：

```powershell
cmake -S RobWork -B build -G "Visual Studio 17 2022" -A x64 -DRWS_BUILD_INDUSTRIALROBOT=ON -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake"
cmake --build build --config Release --target <受影响目标>
cmake -S RobWork/RobWorkStudio/src/rwslibs/industrialrobot -B <smoke目录> -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" -DCMAKE_PREFIX_PATH=<Qt-msvc目录>
```

集成构建前确认 `build/CMakeCache.txt` 中 `RWS_BUILD_INDUSTRIALROBOT:BOOL=ON`。fresh 树所需 `RobWorkStudio.ini.template.static` 位于源码侧 `RobWork/RobWorkStudio/bin/`；DLL/PATH 故障按 DTB §5.1 定位，python313.dll 处置见 `RobWork/.agents/skills/ird-python313-dll/SKILL.md`。

Windows Qt GUI 测试：Visual Studio x64 开发环境；`$env:QT_QPA_PLATFORM='windows'`；绝对路径，每次仅一个可执行文件，禁止 offscreen。插件初始化失败先停进程、检查继承的 QT_*/QML_* 环境再重启。QCoreApplication 模型测试无需 GUI 插件。细节按需读 `ird-qt-gui-windows` Skill。

测试必须真实执行，保留构建日志、gtest XML 与 per-exe `ird-test-report.json`（沿用已有输出机制）；脚本测试使用其真实日志/报告。区分 PASS/FAIL/SKIP/NOT RUN，计数分列 passed/skipped/failed。失败先修复或报告，未执行不能标通过。

## 5. 架构红线

L2 计算库零 Qt；业务域之间不直接互链，只经六类端口；不跨单元私有头；名称解析/运动学真值归 runtime，修订写路径归 project，共享策略归 policy，单位/文案归 core/diagnostics，命令入口归 ui。不自行拼剥 RobWork 名称前缀；项目修订只增不改；不恢复 `old/` 历史实现。

## 6. 修改、审查与交付

### 6.1 工作流程

普通改动：明确范围和预期 → 修改 → 按 §4 验证 → 同步实际改变的契约/设计 → 提交推送。无需另写侦察画像、八节设计、计划、六段决策 Note、交接模板或重复状态台账；这些工具仅在复杂任务确有用途时使用，不逐阶段强制调用 Skill。

### 6.2 分支与合入

实现任务优先用契约分支 `wp<nn>-t<kk>`；非编号任务可用 `codex/<主题>`。纯文档可在 `redesign-main` 修订。实施者不得自行合入主线，合入由所有者决策。禁止改写已推送历史、强推或触碰 `main`。

### 6.3 提交

一个完整改动连同测试、必要文档同提交并立即推送 `origin`。中文标题用任务前缀或 `[docs]/[governance]/[DTB]/[units]/[ARCH]/[ACC]/[PIPE]/[CCP]`。正文简述原因、验证及影响，不要求固定四段标题；简单文档改动可仅标题。`commit-lint.ps1 -Range <base>..HEAD` 只检查新增提交。推送失败先查原因，非快进时在不改写已推送历史的前提下同步；不能安全解决则报告，不强推。

### 6.4 审查分级

普通局部修正与文档维护自查即可；公共接口、跨单元接线、数值核心、身份/持久化、安全边界及复用迁移必须独立验收。用户明确要求独立验收或既有迁移阶段门规定时同样执行。验收核对具体 SHA、相关 acceptance、真实验证与红线；只重跑受影响验证，不无条件全库重配、全量测试或注入两个失败断言。独立验收不能由实施者自称完成。

人工任务可在一个干净现场验收，记录随任务提交；不强制第二个记录 worktree 和 evidence 分支。自动流水线继续按 PIPE 的锁、状态机、冻结 SHA 与 `acc/<taskId>/<attempt>` 回传协议运行；人工任务不读写流水线状态。

## 7. 沟通

禁止使用子智能体。需要独立验收时，由人工或所有者另开的独立会话执行，不自动派发子代理。

交流与提交摘要用中文。结果说明改动、验证及未完成事项即可，一次报告作为交付摘要，不多处抄写。同一问题只在相关任务记录登记；跨任务缺陷进 findings，语义争议交所有者，不为格式和措辞单独建缺陷。
