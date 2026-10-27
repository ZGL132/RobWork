# B14-P2-01 返工独立复验

结论：**PASS，仅 B14-P2-01 返工复验，该项关闭**。本结论不重授整批 B14 / 全 T01 PASS，不授 T02 准入或生产迁移批准。

冻结 SHA：a78b41ff29261b17fc8379ee27e4afb1933ab827；直接父 SHA：85e561539a4038e37231dcfcb506109a5a188151。原 FAIL 比较基准：b83fa7a3fc8ff233e56736adabd53f11c914dbad。独立现场：C:\Users\zgl18\AppData\Local\Temp\ird-accept-b14-r2-844c099516cf4504831b94667d217371\repo。

修正文案与 Codec.cpp 的真实行为一致：解码后 UTF-8 路径允许中文和空格；非空、最多 4096 字节，拒绝 C0 字节(<0x20)与 DEL(0x7F)。已核 parseString/parseUnicodeEscape、requirePathField(779–794) 及调用处；磁盘 JSON 的 ASCII 载体限制与解码后路径接受面已分清，io 权威边界保留。

与原 B14 SHA 比较，project.json 仅目标关系的 differences 改变，其余矩阵数据一致，decision/status 仍为 pending。返工记录 before/after、source 与矩阵相符。7 项摘要全部匹配（源码区段 1、inputs 4、auditEvidence 2）。本次未重做原 B14 的全部关系审计，未实跑复现产品缺陷。

原 FAIL 归档 44 文件：40 个原始字节摘要相同，4 个仅 CRLF/LF 差异，无内容偏差；原 acceptance 仍为 FAIL、原 SHA 与 OPEN finding 保持原样。原始日志未清洗。

| 检查 | 结果 |
| --- | --- |
| validate-task | PASS，exit 0 |
| validate-docs | PASS，exit 0；NOT AUDITED evidence |
| validate-reuse，冻结 SHA | PASS，exit 0；仅治理检查 |
| scoped git diff --check | PASS，exit 0；排除归档原日志，见精确排除表 |

四项门禁 passed=4、skipped=0、failed=0。validate-reuse 的 closedFiles=0 不代表算法合规。源码/上游/vcpkg 自原 B14 SHA 起无变化，独立 checkout 起止干净。

NOT RUN：C++ 构建、GUI、数值/性能、DeepEvidence、B14 全量关系重审、其他批次、远程交付核验。未修改父现场、提交、推送或合入。

详细证据：acceptance.json、facts.json、hash-checks.json、archive-checks.json、task/docs/reuse.log 与 exit 文件、scoped-diff-check.log、diff-check-exclusions.log、product-upstream-diff.log、final-status.log。验证脚本和报告生成脚本 SHA256 记录于 acceptance.json。
