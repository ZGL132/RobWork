# REUSE-T01 B27 独立局部验收

结论：**PASS_LOCAL_B27_ONLY**。冻结 `b428aa566144bdad777dbc14b985a6d90f435d65`，基点 `7c263e78683a74b5532e6f99e7647a02b6f39ea7`。仅独立临时克隆验收；未修改共享树、实施修复、远程、合入。

- PS7原完整suite：17 passed / 0 failed / 0 skipped / 0 notRun。
- PS5.1在系统默认CRLF独立检出：17 passed / 0 failed / 0 skipped / 0 notRun。
- 独立context v2：38 passed / 0 failed；旧误绿复现4/4；索引原探针重放16/16；上游检索/候选摘要8/8。每条负例同时核对退出码及具名诊断，不接受ParserError冒充拒绝。
- task（内调reuse）、docs、唯一一回冻结Head reuse、显式scope、commit-lint均exit0；165路径均在契约允许面，产品/框架/vcpkg变更0。
- 矩阵新增24、刷新3、删除0；全库869。27段源码、156输入、153证据摘要独立核对通过。本批27均pending/owner/NONE/空targets；全库仍866pending+3proposal。

17行helper与原内联清单语义一致；双Shell独立对比锚点、重复designRefs、反斜杠、固定输入去重后完整指纹行相同。删除任一9项必需输入指纹均exit1；源实际缺失/变化仍exit2。导航与根按需规则一致，索引仅改注释/提示，三入口与测试逐段来源、需求、权限无越权结论。原8用例保留，新增9例真实执行；已核对trace/reuse稀疏面、clone局部长路径及父进程测试自身不覆盖。

**保留限制：**首次LF检出PS5.1为6p/1f/10未执行；统一autocrlf=false仍失败。基点同样失败，直接基点/冻结B.1脚本均ParserError，Git对象相同，属于既存LF/PS5.1兼容限制。系统默认CRLF检出原suite17/17通过，不能用后者抹去前两轮。全差异diff-check exit2来自CRLF及原始证据空白；功能面cr-at-eol检查exit0，未清洗原始日志。探针v1留空行导致的未命中探索已保留，v2整行删除后真实复现旧误绿。详情见acceptance.json。

已知重复taskId/坏JSON跳过/索引启发式/findings保护风险保持pending。此结论不授全T01/T02、核心或上游扩展、生产正向证据、远程/CI或合入。

原命令和退出码见commands.json、eol-comparison.json及各探针report；日志按名保存。manifest.json仅枚举可交付验收文件，排除全部克隆树、fixture和.git。
