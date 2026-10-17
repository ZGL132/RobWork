# 新会话接手演练记录（context/handoff-drill.md）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.0（2026-10-17，Agent-native 治理 Task 8 交付物） |
| 演练任务 | CORE-T11（单元卡与实现一致性核查——治理试点） |
| 最终结论 | **attempt 2 verdict＝PASS**（独立对抗式验收，2026-10-17）；合入决策归所有者 |
| 本文件口径 | 本文件是演练的 **canonical 总记录**（编排者视角全链）；实施者/验收者一手记录在 `wp03-t11` 分支同名文件（实施批 `b53477be`、返工批 `397a5465`）——**合入该分支时同名文件冲突取 redesign-main 侧（本版为准），分支版本作为附录留档不合并** |

---

## 0. 演练设计（验收什么）

检验治理系统 Task 1–7 建成物的真实可用性：**新会话只凭"仓库＋TaskId＋分支＋上下文包"四项输入，不携带任何对话历史，能否按分层规则与执行协议 Skills 独立完成任务并交付可验收的提交**。

隔离措施：实施者与验收者各为独立全新上下文子会话（ACC §2.2 独立子会话模式）；验收者在 detached worktree 干净复现（ACC §3），不采信实施者口述（复现优先于采信）。

## 1. 时间线与 SHA 链

| 阶段 | 提交 | 内容 |
| --- | --- | --- |
| 准备批（编排者） | `458250f0`（redesign-main） | 登记 CORE-T11：DTB v0.82 §2.4 增行＋core.md v0.13 §9 增行＋契约（ready）＋上下文包 |
| 实施批（实施者子会话） | `b53477be`（wp03-t11） | 五段执行＋两处真实文档失同步修正（§3.1 模块计数 10→9、DiagData 摘要列补 DiagnosticLevel）＋handoff-drill.md 初版；验证四态（构建面 SKIP＝纯文档豁免） |
| 验收 attempt 1（验收者子会话） | 无提交（只读核查） | **verdict=FAIL**：阻断①系统缺陷 F-908/F-909（干净树门禁必挂）；阻断②head SHA 未回填。六维度中定位/最小性/禁止路径/证据真实性/可续作全 PASS |
| 系统修复批（编排者） | `9d297aab`（redesign-main） | F-908（证据索引改 git 跟踪面＋quotepath=off）、F-909（包指纹改 git blob SHA）；F-908/F-909 入 findings（blocking→fixed）；索引重生成 6590 |
| 返工轮（实施者子会话） | `0e5714fc`（merge 主线修复）＋`397a5465`（返工提交，origin/wp03-t11 head） | SHA 回填三处＋§9 返工轮小节＋包按 blob 口径重建；推送后复验全 PASS |
| 验收 attempt 2（验收者子会话） | 无提交（只读核查） | **verdict=PASS**：两阻断在干净树独立复现消除；含"验证验证者"黑盒检查（篡改一字节→正确 STALE） |

## 2. 演练发现与处置对照（摩擦→修法，"不只追加提示词"）

| # | 发现（来源） | 性质 | 处置 |
| --- | --- | --- | --- |
| 1 | 证据索引计入 36 个 gitignored 文件（6626 vs 干净检出 6590），fresh 树/CI 门禁必挂（验收 attempt 1） | 系统 blocking | **F-908 修复**（`9d297aab`）：index-evidence/validate-docs 计数基准改 git ls-files 跟踪面 |
| 2 | 包指纹字节级 SHA-256 行尾敏感，autocrlf 下干净树 9 处全量 STALE 误报（验收 attempt 1） | 系统 blocking | **F-909 修复**（`9d297aab`）：指纹改 git hash-object blob SHA；STALE 报错附重跑命令 |
| 3 | 中文文件名被 git 八进制转义致 4 个证据文件解析失败（修复批开发中实测） | 系统 bug | 同批修复：`-c core.quotepath=off`＋UTF-8 控制台编码 |
| 4 | 实施者 head SHA 未回填（验收 attempt 1 阻断②） | 实施瑕疵 | 返工轮补齐（`397a5465`）；固化口径入 handoff-template（ls-remote 冻结值，避免自引用） |
| 5 | 共享源多包连带过期（改 core.md → CORE-T01 包也 STALE），接手者不知重跑纪律会卡住（实施者摩擦③） | 可用性 | STALE 报错已附 `-TaskId <包名>` 重跑提示；attempt 2 suggestion（提示曾指向首个关联包）→ 本批修 `$tid` 循环作用域 |
| 6 | 包路径嵌套直觉坑：仓库根含同名 RobWork/ 子目录（实施者摩擦①） | 可用性 | ird-discover-change Skill 增路径提示行（本批） |
| 7 | 契约 acceptance"§10 测试目标登记"与卡面结构错位（实际 §3.3/§8）（实施者摩擦④） | 契约措辞 | **移交合入批**：修 CORE-T11.json designRefs/acceptance 措辞或 DTB §4 登记 |
| 8 | 立卡批版本行与 §12.2 变更记录表断档（实施者摩擦⑤，根因＝编排者准备批疏漏） | 流程教训 | 实施者已补（`b53477be`）；教训留档：立卡批必须同步变更记录表 |
| 9 | 编排者与子会话共享工作树，子会话切分支后编排者提交误落 wp03-t11（`b1003d32`，未推送即回收） | 操作事故 | 按 SHA 文件检出回收至 `9d297aab`，任务分支指针复位；ird-implement-task Skill 自查清单增"提交前 `git branch --show-current` 核查"（本批）；编排者本批提交前二次核查分支，现场未重演 |
| 10 | 本地 Skills 不经 Skill 工具自动调用，需读文照执行（实施者摩擦②） | 预期内 | 非缺陷——Task 5 既定验收口径（"按 AGENTS.md 入口能找到并照执行"）；留档 |
| 11 | 环境小坑：Git Bash 无 python、管道后 `$?` 取 tail 退出码（实施者摩擦⑥） | 环境口径 | ird-verify-change 已有四态口径覆盖（退出码重定向取值）；留档 |

## 3. 验证命令实录（Task 8 清单要求的四条，最终态）

- `pwsh -File RobWork/scripts/industrialrobot/validate-docs.ps1` → PASS（20 units, 12 trace entries, 357 task files, 26 registered assets, 5 notes, 6 skills, 2 packs, 6590 evidence）exit 0
- `pwsh -File RobWork/scripts/industrialrobot/validate-context-pack.ps1 -All` → 2 packs fresh=2 exit 0
- `pwsh -File RobWork/scripts/industrialrobot/index-evidence.ps1` → 6590 files（git 跟踪面，与干净检出一致）
- `git diff --check` → clean

（干净树复现由验收者独立完成：detached worktree @ `397a5465` 两条门禁裸跑 PASS＋黑盒篡改检查——详见 §1 attempt 2 行。）

## 4. 计划完成定义对照

| 完成定义（计划原文） | 结果 |
| --- | --- |
| 新 Agent 能从分层规则进入正确局部规则 | ✅ 实施者六维度"定位"PASS（规则链/章节锚点/DTB 行级全对） |
| 每类任务有最小阅读路径 | ✅ 按路径一取用，无整册通读（验收"上下文最小性"PASS） |
| 长期决策有独立 Note | ✅ Task 4（本演练未触及，门禁持续校验 5 notes） |
| 上下文包可生成并检查过期 | ✅ 且经双向实测：过期检测真实触发（含共享源连带过期）、行尾缺陷被发现并修复 |
| 证据可检索且不污染默认上下文 | ✅ 索引 6590 条经 F-908 修复后跨环境一致 |
| 至少一个真实任务已由新会话独立接手并通过对应验证 | ✅ CORE-T11：attempt 2 PASS（两独立子会话，干净树复现） |

## 5. 移交与遗留

- **CORE-T11 合入决策归所有者**：`origin/wp03-t11`＝`397a5465`（attempt 2 PASS；合入时同名 handoff-drill.md 冲突取 redesign-main 侧，契约 status 翻 done 随合入收尾批）。
- 遗留（均已登记或移交）：表中 #7（契约措辞，合入批处置）；F-907（open，归治理批——AGENTS §4.1/DTB §5.1 环境口径澄清）。
- 演练产出的系统改进全部落地（#1/#2/#3/#5/#6/#9）；本记录＋`handoff-template.md` 为 Task 8 交付物。
