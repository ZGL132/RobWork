# B26：findings 并集解算入口审计

完整审读 `merge-findings-union.ps1` 参数/根路径解析、Git stage 读取、条目扫描与规范化、排序、顶层 schema/note 守卫、删除/异改/新增处理、条目区重组、JSON/重复 ID/计数校验、写盘及 `git add`；回归入口的锁、返工失败证据和临时目录清理也逐段复核。新增逐功能行保持 owner 权限、`NONE` 上游能力、空 target、pending；矩阵不授予模型读取/构造/写入权。

复用来源是 Git 三方 index (`git show :1/:2/:3`、`update-index`/`add`)、PowerShell 原生 JSON/集合 API、.NET UTF-8 文件 API 和现有 pipeline 回归入口。RobWork 的 C++/RobWorkSim API、CTest/gtest 不提供 findings 三方文本并集语义；没有把它们包装成相似能力，也没有引入新的通用 merge 框架。产品运动学/动力学能力不在本批审计/授权范围。

发现两处真实契约漏检：PowerShell 默认 `-ne` 大小写不敏感，与脚本头和 PIPE §4.7③ 的“顶层字段不一致即拒绝”冲突；`note=RuleA/Rulea` 与 schema `ird-findings/1/IRD-FINDINGS/1` 都错误自动解算并消除 index 冲突。UTF-8 BOM 缺失另导致 Windows PowerShell 5.1 无法解析该脚本，属于既有跨 Shell 缺陷。最小修改恢复 UTF-8 BOM，并仅将两处顶层比较改为 `-cne`；条目块已使用大小写敏感比较，不动并集算法。

修前真实隔离 Git 三方 index：PS7 6/8 通过、2/8 错误接受；PS5.1 原文件解析失败。补 BOM 后红测两 Shell 各 6/8 通过、2/8 错误接受；绿测各 8/8 通过，16/16 总计。拒绝例验证工作文件原字节与三个 unmerged stage 均不变，成功例验证 union/同号同义/对方独改后的实际内容及 index 消解。原 `test-pipeline-pipeline.ps1` 在两 Shell 完整入口通过，加入 3 个真实 Git 合并边界并核对 stage 与拒绝原因；UTF-8 index-info 文件经 `cmd.exe` 重定向以避免 Windows PowerShell 原生命令管道编码差异，夹具显式清理本地 ignore 并关闭换行转换。

B24/B25 独立局部验收以冻结 SHA 的原件归档并保留原有失败/限制记录；不将这些批次 PASS 扩大为全 T01。全项目功能矩阵仍有大量 pending，GAP 提案仍未批准；本批不进入 runtime、适配层或算法替换，不推送远程。


B26第二轮冻结4b9977f09的raw修正及7项正式检查均通过，但独立对抗25 passed/1 failed：PS5.1缺stage时Git stderr受Stop策略提升为NativeCommandError，退出1而非既定调用错误2；基点仅补BOM亦可复现。原FAIL及327件清单原件保留于batch26/r3/rejected-4b9977f09。第三轮最小返工只在Get-StageLines局部恢复Continue并按Git退出码判断，finally恢复调用方策略；不改变并集算法。原pipeline加入缺stage第四个边界，核exit2、具名原因、原工作文件及两stage不变。红测PS7通过、PS5.1因上述异常失败；修后两Shell完整入口均exit0、四边界通过。新冻结独立验收尚未执行，不自称PASS，不进入T02，不同步远程。
