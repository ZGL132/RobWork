# B29：B.1脚本编码与双Shell回归

B27独验暴露LF/no-BOM检出时PS5.1无法解析validate-b1-batch.ps1；基点和冻结对象相同，属于既存遗漏。沿用B23对B.1批次JSON/契约顺序的语义审计与原test-governance-lite，不搭建第二套批次执行器或测试平台；本批只修文件编码入口。

先在原套件增加LF/CRLF各合法与非法schema共4例，保留源BOM状态，只改隔离副本换行。PS5.1修前6passed1failed14notRun：LF合法正例真实ParserError，不能将解析失败计作非法输入拒绝。给原脚本仅加UTF-8 BOM，正文逐字节不变；不修改JSON模式/外部门禁/排队语义。

首轮修后PS7全21通过；PS5.1合法LF通过，但原生stderr按宽度在schemaVersion中间折行，错误输入已exit1而具名pattern未匹配（7passed1failed13notRun）。保留该失败和宽度探测；最终将特定schema标记逐字符允许空白以适配原生换行，Invoke-Probe明确排除ParserError/Unexpected token，防止放宽诊断匹配把解析失败洗成合法拒绝。

最终原套件PS7与PS5.1各21passed0failed0skipped0notRun；原17例保留，新增4例都真实调用脚本子进程。脚本只依赖原PowerShell JSON、.NET UTF8Encoding及既有批次/契约。B.1结构验证并非执行授权：外部门禁open仍由编排器约束；大小写/路径/字段语义继续按B23既存pending边界，不声称本次编码修正证明全流水线正确。无产品算法、上游或vcpkg变更，无远程同步，不授全T01/T02。
