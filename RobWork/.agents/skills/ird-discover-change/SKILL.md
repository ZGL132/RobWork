---
name: ird-discover-change
description: 任务侦察；接手 industrialrobot 变更、范围或依赖不明确时使用。
---

# ird-discover-change

1. 看当前分支及 git status，保留其他改动，禁止 main。
2. 有任务 ID 就只读该契约的范围、前置、引用和验证；无 ID 的普通修正直接以用户请求为范围，不额外编卡。
3. rg 定位相关需求/单元章节和源码；跨单元时才查依赖边，复用时才查相关 catalog/inventory 条目。
4. 按根 AGENTS §4/§6 判断验证与独立验收范围。

用几句话明确范围、预期与风险即可，不另产侦察报告。默认不读完整资产清单、库存、findings、PIPE 或所有阶段 Skill；已读未变规则不重读。

执行根 AGENTS.md，按需取用本技能，不构成强制阶段链。
