# 规则执法分级审计表（context/rule-enforcement.md）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.0（2026-10-17，随 Agent-native 治理 Task 3 建立——治理原则"无执法不立规"落地） |
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
| `validate-docs.ps1` | 文档锚点（PIPE/ACC 关键语义）、B.1、20 单元、任务 ID 唯一、追踪矩阵、phase-one 索引、资产清单（路径/分类/链接/根级全覆盖）、分层规则入口（分区文件存在＋权威链接＋根回链）、Agent Notes（四目录结构/编号/计划语气黑名单/archived 取代指针）、执行协议 Skills（六件存在性＋frontmatter）、证据索引一致性（计数＋路径集合双向＋字节数；`-DeepEvidence` 追加 SHA-256 复算——2026-10-17 审查 P1-3 对策） |
| `validate-state.ps1` | state.json schema、phase 枚举、SHA 冻结链⑩、run 租约、队列可领取性 |
| `validate-task.ps1` / `verify-task.ps1` | 任务契约字段完备性（ready 必备字段）/ 任务验证命令执行与证据核验 |
| `build-context-pack.ps1` / `validate-context-pack.ps1` | 上下文包生成与新鲜度校验（源文件 git blob SHA-1 重算，STALE=exit 2；表头/状态行/标题身份/契约存在性结构核验）；PIPE 权威红线——包内 state.json 状态字段副本（JSON 形态扫描）即 exit 1 |
| `index-evidence.ps1` / validate-docs 证据段 | 证据摘要索引生成与一致性（新证据未重跑索引→条目数不符即门禁失败）；索引只存摘要不嵌内容；删除/移动三前置登记于 traceability/README.md §3 |
| `gate-all.ps1` | 集成＋冒烟一键全量门禁＋第 0.7 步治理文档门禁（validate-docs -DeepEvidence＋validate-context-pack -All，2026-10-17 挂载——审查 P1-1 消账） |
| CI 双模板（`scripts/industrialrobot/ci/`） | 远端常驻：构建＋测试（`^sdurws_ird_.*_test$` 全目标）＋治理门禁两步（validate-docs -DeepEvidence＋validate-context-pack，2026-10-17 扩面）＋check-boundaries |
| CMake 目标注册面 | C++17（CXX_STANDARD 17 显式）、gtest 接入（find_package GTest CONFIG REQUIRED）、vcpkg toolchain 解析 |

## 3. 审计表（按规则来源分组）

### 3.1 根 AGENTS.md

| 规则（出处） | 层级 | 执法者/指针 |
| --- | --- | --- |
| 分支红线：main 冻结禁推（§0/§6.2） | behavioral | 升级候选：CI 检查 push 目标分支（未建，不虚标） |
| 框架零源码修改 SA-02（§1/§5.4） | gate＋review | ird_gates 命中集＋ACC 范围核查（allowedFiles 越界即 FAIL） |
| 详细中文注释规范（§2 全节） | review | ACC 验收 checklist（注释规范核查项） |
| 高危信息六表（§2.5） | review | 同上 |
| C++17（§3，D-01） | gate | 各目标 CXX_STANDARD 17 显式声明（CMake 配置面） |
| 命名空间/目标命名/头文件 guard（§3） | review | 验收核查＋ird_gates 公共头布局用例（部分面） |
| 第三方依赖经 vcpkg、禁 vendor（§3） | gate＋review | vcpkg toolchain 构建解析（非 vcpkg 依赖无法链接）＋ACC 核查 |
| gtest 测试框架（§3） | gate | find_package(GTest CONFIG REQUIRED) 构建面 |
| 双模式构建＋缓存确认 grep（§4.1） | review | ACC"重配重跑"环节（验收者 fresh 树亲证） |
| 测试真实执行留痕（§4.2） | gate＋review | verify-task.ps1（证据存在性）＋ACC 重跑对账 |
| L2 计算内核零 Qt（§5.1） | gate | BuildRedLineTest NoQtInclude_NFR_MNT_01_R3 |
| 业务域互链禁止 R-1（§5.2） | gate | BuildRedLineTest NoCrossUnitInclude＋ird_gates 依赖边 |
| 跨单元私有头禁止 R-2（§5.3） | gate | BuildRedLineTest NoCrossUnitInclude_R1_R2_T1 / PublicHeaderPathLayout_R2 |
| 名称前缀不拼接 R-4（§5.5） | review | 验收核查（runtime 归属语义） |
| 权威唯一 PA-1（§5.6） | review | 架构审查＋ACC |
| 不可变历史 PA-2（§5.7） | review | 单元测试（project 域修订语义用例）＋ACC |
| 不继承 old/（§5.8） | behavioral | old/ 磁盘不存在（事实性满足）；引入即违 SA-02 面 |
| 不重定义/收窄/扩大需求语义（§6.1） | review | ACC（需求溯源逐条核对） |
| 提交前缀体系（§6.2） | behavioral | 升级候选：commit-lint（未建） |
| 人机同规/署名人负责（§6.2） | behavioral | 升级候选：分支保护/签名策略（仓库设置层，非文档可执法） |
| 一次修改一次提交＋即时推送（§6.3） | behavioral | 事后审计（合入批次核查提交粒度） |
| commit 四段正文（§6.3） | behavioral | 升级候选：commit-lint（未建） |
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
- **降级/维持 behavioral**（明示不造摆设门禁）：提交粒度纪律、中文交流规范、阅读路径、人机同规署名责任。
- **删除项**：无——第一遍审计未发现"完全无执法载体且无保留价值"的条款（各 behavioral 条款均有事后审计或升级路径）。
- 本表自身为 load-bearing 登记资产：路径存在性由 validate-docs 资产清单门禁保证（自反执法）。
