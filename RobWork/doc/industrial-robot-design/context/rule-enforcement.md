# 规则执法分级审计表（context/rule-enforcement.md）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.4（2026-10-18：新增偶发失败观测工具 run-flake-probe（F-925）——重复运行＋后台负载＋失败用例点名/原始输出落盘；观测层，不改变测试门禁判定；前一版 v1.3＝commit-lint 挂 CI） |
| 用途 | 根/分区 AGENTS.md 逐条规则的执法分级：每条规则声明由谁执法（gate / review / behavioral），杜绝"不可执法却假装可执法"的摆设条款 |
| 维护纪律 | 新规则落文时**必须**同时在本表登记行；规则变更/执法者升级（behavioral→gate）随批同步。第一遍审计只加标注，不批量删规则 |

## 1. 执法层级定义

| 层级 | 含义 | 执法时机 |
| --- | --- | --- |
| gate | 机器门禁自动判定，违者构建/CI/脚本直接失败 | 构建、gate-all、CI、validate-* 脚本运行时 |
| review | 对抗式验收人工/会话核查，违者验收 FAIL | 验收段（ACC 协议） |
| behavioral | 行为约定，靠阅读阶梯与提交纪律传导，无自动判定 | 全程（违反靠事后审计发现） |

## 2. 执法者清单（gate 层现有设施）

| 执法者 | 检查面 |
| --- | --- |
| `BuildRedLineTest`（core/test） | 零 Qt include（R-3）、零跨单元 include（R-1/R-2）、公共头路径布局（R-2）——四用例：SourceTreeReachable / NoQtInclude_NFR_MNT_01_R3 / NoCrossUnitInclude_R1_R2_T1 / PublicHeaderPathLayout_R2 |
| `ird_gates.cmake` ＋ `ird_gates_whitelist.cmake` | 单元依赖边/命中集检查（dependency-graph.json 允许边；R-3 等例外经白名单登记） |
| `validate-docs.ps1` | 文档锚点（PIPE/ACC 关键语义）、B.1、20 单元、任务 ID 唯一、追踪矩阵、phase-one 索引、资产清单（路径/分类/链接/根级全覆盖）、分层规则入口（分区文件存在＋权威链接＋根回链）、Agent Notes（四目录结构/编号/计划语气黑名单/archived 取代指针）、执行协议 Skills（六件存在性＋frontmatter）、证据索引一致性（计数＋路径集合双向＋字节数；`-DeepEvidence` 追加 SHA-256 复算——2026-10-17 审查 P1-3 对策）、**未跟踪证据检测**（traceability/ 下 `??` 文件即失败——强制 git add→索引→校验顺序，F-929）、**findings 台账不变量**（ID 唯一/status·severity 枚举/日期格式/fixedAt↔status 一致，F-930）、生产装配清单 Tier A 结构面（schema/唯一性/wired 锚点/waiver-挂-open-finding 且关闭即失效——T1） |
| `validate-state.ps1` | state.json schema、phase 枚举、SHA 冻结链⑩、run 租约、队列可领取性 |
| `validate-task.ps1` / `verify-task.ps1` | 任务契约字段完备性（ready 必备字段）/ 任务验证命令执行与证据核验 |
| `verify-task-scope.ps1` | 任务改动范围机器预检：base..head 逐文件 vs 契约 `allowedFiles`/`forbiddenFiles`（ACC §4.1 机器执行面；2026-10-18 落位，F-928） |
| `commit-lint.ps1` | 提交信息规范：前缀白名单（[WP-nn-Tkk]/[DTB]/[docs]/[units]/[ARCH]/[governance]/[ACC]/[PIPE]/[CCP]）＋标题非空话＋正文四段（改动内容/改动原因/验证/影响面）；`-Range` 只查新增提交，历史存量按 F-012 豁免（2026-10-18 落位，F-934） |
| `run-flake-probe.ps1` | 偶发失败观测（非门禁）：测试 exe 重复运行＋可选后台 CPU 负载，逐次落盘 gtest XML/stdout/stderr，失败用例点名并汇总（F-925；`-SelfTest` 校验解析器） |
| `build-context-pack.ps1` / `validate-context-pack.ps1` | 上下文包生成与新鲜度校验（源文件 git blob SHA-1 重算，STALE=exit 2；表头/状态行/标题身份/契约存在性结构核验）；PIPE 权威红线——包内 state.json 状态字段副本（JSON 形态扫描）即 exit 1 |
| `index-evidence.ps1` / validate-docs 证据段 | 证据摘要索引生成与一致性（新证据未重跑索引→条目数不符即门禁失败）；未跟踪证据提示（F-929）；索引只存摘要不嵌内容；删除/移动三前置登记于 traceability/README.md §3 |
| `gate-all.ps1` | 集成＋冒烟一键全量门禁＋第 0.7 步治理文档门禁（validate-docs -DeepEvidence＋validate-context-pack -All，2026-10-17 挂载——审查 P1-1 消账） |
| CI 双模板（`scripts/industrialrobot/ci/`） | 远端常驻：构建＋测试（`^sdurws_ird_.*_test$` 全目标）＋治理门禁两步（validate-docs -DeepEvidence＋validate-context-pack，2026-10-17 扩面）＋check-boundaries；2026-10-18 增 **commit-lint**（GitHub fetch-depth 0／GitLab GIT_DEPTH 0，仅查本次 push/MR 新增提交，F-934） |
| CMake 目标注册面 | C++17（CXX_STANDARD 17 显式）、gtest 接入（find_package GTest CONFIG REQUIRED）、vcpkg toolchain 解析 |

## 3. 审计表（按规则来源分组）

### 3.1 根 AGENTS.md

| 规则（出处） | 层级 | 执法者/指针 |
| --- | --- | --- |
| 分支红线：main 冻结禁推（§0/§6.2） | behavioral | 升级候选：CI 检查 push 目标分支（未建，不虚标） |
| 框架零源码修改 SA-02（§1/§5.4） | gate＋review | ird_gates 命中集＋ACC 范围核查（allowedFiles 越界即 FAIL） |
| 契约范围 `allowedFiles`/`forbiddenFiles`（ACC §4.1） | gate＋review | `verify-task-scope.ps1` 机器预检（2026-10-18 起；逐文件命中判定）＋验收者 diff 语义复核（机器只证"路径在允许模式内"） |
| 证据索引与提交时序（traceability/README §2） | gate | validate-docs 未跟踪证据检测（`??` 即失败）＋index-evidence 未跟踪提示——强制"git add→索引→校验"顺序（F-929） |
| findings 台账状态一致性（ACC §5 强制转登） | gate | validate-docs findings 段：`fixedAt` 存在则 `status` 必须 fixed；ID 唯一/枚举/日期格式（F-930） |
| 详细中文注释规范（§2 全节） | review | ACC 验收 checklist（注释规范核查项） |
| 高危信息六表（§2.5） | review | 同上 |
| C++17（§3，D-01） | gate | 各目标 CXX_STANDARD 17 显式声明（CMake 配置面） |
| 命名空间/目标命名/头文件 guard（§3） | review | 验收核查＋ird_gates 公共头布局用例（部分面） |
| 第三方依赖经 vcpkg、禁 vendor（§3） | gate＋review | vcpkg toolchain 构建解析（非 vcpkg 依赖无法链接）＋ACC 核查 |
| gtest 测试框架（§3） | gate | find_package(GTest CONFIG REQUIRED) 构建面 |
| 双模式构建＋缓存确认 grep（§4.1） | review | ACC"重配重跑"环节（验收者 fresh 树亲证） |
| 测试真实执行留痕（§4.2） | gate＋review | verify-task.ps1（证据存在性）＋ACC 重跑对账 |
| 留痕证据文件化（F-915 口径——测试运行证据以 gtest XML（`<target>_report` 目标产物）等文件入库 traceability/，**提交信息文字不构成留痕载体**） | review | ACC 验收对账（比对分支声明的运行结果与 traceability/ 内 XML 实件；UI-T84 起随批兑现——负例/演示类运行同样落文件） |
| L2 计算内核零 Qt（§5.1） | gate | BuildRedLineTest NoQtInclude_NFR_MNT_01_R3 |
| 业务域互链禁止 R-1（§5.2） | gate | BuildRedLineTest NoCrossUnitInclude＋ird_gates 依赖边 |
| 跨单元私有头禁止 R-2（§5.3） | gate | BuildRedLineTest NoCrossUnitInclude_R1_R2_T1 / PublicHeaderPathLayout_R2 |
| 名称前缀不拼接 R-4（§5.5） | review | 验收核查（runtime 归属语义） |
| 权威唯一 PA-1（§5.6） | review | 架构审查＋ACC |
| 不可变历史 PA-2（§5.7） | review | 单元测试（project 域修订语义用例）＋ACC |
| 不继承 old/（§5.8） | behavioral | old/ 磁盘不存在（事实性满足）；引入即违 SA-02 面 |
| 不重定义/收窄/扩大需求语义（§6.1） | review | ACC（需求溯源逐条核对） |
| 提交前缀体系（§6.2） | gate＋review | `commit-lint.ps1 -Range`（新提交；历史按 F-012 豁免不回写）＋合入批抽查 |
| 人机同规/署名人负责（§6.2） | behavioral | 升级候选：分支保护/签名策略（仓库设置层，非文档可执法） |
| 一次修改一次提交＋即时推送（§6.3） | behavioral | 事后审计（合入批次核查提交粒度） |
| commit 四段正文（§6.3） | gate＋review | `commit-lint.ps1`（新提交四段标记缺失即 FAIL；验证段 passed/skipped 口径由 AGENTS §6.3 文本＋review 抽查，F-926） |
| 实施者不得自行合入（§6.4） | gate | validate-state ⑩ SHA 冻结链：awaiting_merge 必须有结构化 acceptanceRecord，未验收提交无法混入合入 |
| 中文交流/三态如实汇报（§7） | behavioral＋review | 汇报审计＋ACC 记录核查 |

### 3.2 设计区 AGENTS.md（本树）

| 规则 | 层级 | 执法者/指针 |
| --- | --- | --- |
| 权威关系表（§2，冲突时权威序判定） | review | 冲突登记 findings 处置 |
| 最小阅读路径（§3） | behavioral | 阅读阶梯传导 |
| 提交前缀（§4） | behavioral | 同根 §6.2 |
| 根级资产必须登记（§4） | gate | validate-docs 资产清单门禁（unregistered root-level asset 即失败） |
| PIPE 权威红线：不复制 state.json 状态字段（§4） | gate＋review | validate-context-pack.ps1（Task 6 起：包内状态字段副本即 exit 1，实测验证）；evidence-index 侧随 Task 7 |
| 不批量移动/删除旧文档与证据（§4） | behavioral | 治理计划停止条件（Task 1–3 期） |

### 3.3 源码区 AGENTS.md（industrialrobot/）

| 规则 | 层级 | 执法者/指针 |
| --- | --- | --- |
| 公共头路径/跨单元公共头（§1） | gate | BuildRedLineTest PublicHeaderPathLayout_R2 |
| 架构红线速查（§2，同根 §5） | gate | 见 3.1 对应行 |
| 注释契约（§3） | review | ACC |
| 测试目标命名/四类测试目录（§4） | gate＋review | CMake 目标注册面（`_test/_contract_test/_gui_test` 后缀）＋ACC |
| 测试替身政策：替身只打端口边界、不引 gmock（§4） | review | ACC checklist（2026-10-17 新落） |
| 证据输出三件套（§4） | gate＋review | verify-task.ps1＋ACC 重跑 |
| Windows GUI 测试规则（§5） | review | gui_test 执行核查（禁 offscreen 等） |
| 构建自查双模式＋grep 确认（§6） | behavioral＋review | 实施者自查＋ACC 重配重跑 |

## 4. 审计处置汇总（2026-10-17 第一遍）

- **升级候选**（behavioral→gate，待后续批次）：push 目标分支检查、commit-lint（前缀＋四段）、context pack 状态字段检查（Task 6）。
- **2026-10-18 升级落地（本表 v1.1）**：①改动范围（ACC §4.1）→ `verify-task-scope.ps1` 机器预检（F-928）；②证据索引时序 → validate-docs 未跟踪证据硬失败（F-929，UI-T85 attempt 1 B-1 根因面）；③findings 状态一致性 → validate-docs 台账不变量（F-930，F-921/F-922 状态漂移实例）。三项均在 gate 层，不需要行为自觉。
- **2026-10-18 升级落地（本表 v1.2）**：④提交信息（前缀＋四段）→ `commit-lint.ps1 -Range`（F-934；止增不追改——历史 99 条标题风格债按 F-012 整族销案，已推送历史不回写）；⑤验证段 passed/skipped 计数口径 → AGENTS §6.3 文本（F-926），机器无法稳定判语义、由 review 抽查。
- **降级/维持 behavioral**（明示不造摆设门禁）：提交粒度纪律、中文交流规范、阅读路径、人机同规署名责任。
- **删除项**：无——第一遍审计未发现"完全无执法载体且无保留价值"的条款（各 behavioral 条款均有事后审计或升级路径）。
- 本表自身为 load-bearing 登记资产：路径存在性由 validate-docs 资产清单门禁保证（自反执法）。
