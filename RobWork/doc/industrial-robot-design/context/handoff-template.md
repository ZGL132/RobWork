# 会话交接记录模板（context/handoff-template.md）

| 字段 | 值 |
| --- | --- |
| 文档版本 | v1.0（2026-10-17，随 Agent-native 治理 Task 8 固化；内容源＝`RobWork/.agents/skills/ird-handoff/SKILL.md`——两处同步修订，本模板为规范载体） |
| 用途 | 任一会话结束（无论任务完成与否）时的统一交接格式——接手会话只拿仓库＋任务 ID＋本记录即可继续，不依赖原会话对话历史 |
| 纪律 | 失败如实（根 AGENTS §7 三态）；阻塞必已登记（findings/DTB §4/governance-log，记录只引编号）；流水线相位写"读 state.json"不转抄状态值（PIPE 权威红线）；SHA 口径＝冻结实施 head 用 40 位字面值，分支后续追加以 origin 引用＋ls-remote 冻结为准（避免自引用） |

---

```markdown
# 交接记录：<taskId 或批次名>（<日期>）

## 目标
<本会话要完成什么（一句）>

## 完成项
- <条目>（<分支>@<SHA>）

## 未完成项
- <条目>——卡在 <哪一步/哪个门>

## 提交清单
- <branch>@<SHA>：<一句话摘要>
（已推送的提交不得 rebase/amend；后续追加以 origin/<branch> 引用，
 验收/接手时以 ls-remote 冻结值核对）

## 改动文件
<路径清单（新增/修改分组）>

## 执行命令与结果
<ird-verify-change 四态格式摘要：PASS（附数字）/ FAIL（附原因）/ SKIP（附豁免依据）/ NOT RUN（附原因）>

## 证据
<路径清单：gtest XML / ird-test-report / 构建日志 / 截图——检索经 traceability/evidence-index.json>

## 阻塞
- <问题描述>——已登记：<findings 编号 / DTB §4 O-xx / governance-log P-xx>
（未登记的阻塞必须先登记再交接）

## 下一步
1. <接手会话的第一个动作（具体到命令或文件）>
2. …
```
