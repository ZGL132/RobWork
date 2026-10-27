# REUSE-T01 B14 独立验收

**FAIL：1项语义登记问题，门禁均通过。** 冻结提交 b83fa7a3fc8ff233e56736adabd53f11c914dbad，父提交6925608ccbc5c7764dc983f7e7e0276f1f12d55f。本地独立Temp clone，core.longpaths=true后detached checkout，起止clean。没有实施、改源工作树、提交、推送、合入或创建子代理。

## B14-P2-01：路径接受面误记为ASCII

reuse/functions/project.json:2388 的 project.Codec.anonymous.requirePathField 写“4096字节ASCII路径字段profile”。真实Codec.cpp:773–778明确允许空格/非ASCII中文路径；779–794只要求非空、长度≤4096 UTF8字节，并拒绝字节<0x20或==0x7F。parseString/parseUnicodeEscape先把JSON转义还原UTF8，parseDraftDocument:1418把解码值交给该函数。因此磁盘JSON纯ASCII不等于解码路径只能ASCII。

该登记将成为后续typed parser/schema迁移的语义依据，若按错误profile实施会拒绝现有合法中文路径。请只改该行描述，保留非空/长度/C0与DEL约束、空格和非ASCII接受面，以及io对路径解析/存在性/安全的权威边界；不改产品接受面。这是静态确证的登记错误，本轮未运行C++，不称产品缺陷已复现。源码/矩阵片段见semantic-evidence.log。

## 已通过的核验

- 新增132关系：project68、reporting64，全部pending；161个源码区段、132个definitions记录首尾/范围、958项摘要一致。真实完整定义、六个canonical重载、七个dump重载、Reader两个构造和29组成对read/write均核对，qualifiedSymbol作用域相符。
- 七条搜索真实重放，exitCode与排序后行多重集合一致；命中44/122/208/12/28/3/9。六项安装候选摘要或入口存在性检查一致。
- BINArchive宿主表示/string长度和固定大端报告字节不等价；Boost公开Endian与hex候选真实。PropertyTree既有B8-r8反例仅按原三项API证据引用，未重跑或扩大为所有JSON库无能力。未把外部安装头伪装上游RobWork源码。
- ReportCodec的parseFull确实重复Data读取，encodeFull复用encodeData；digestFull只对身份副本清currentness.computedAtUtc，编码仍保留原时间。单位、身份、摘要、当前性与诊断权威边界保持；向量reserve、u32超长、UTF8、readTime等待核事项未伪称实跑缺陷。
- 真实消费者、测试调用与原CMake注册均核对；静态绑定没有充当生产正向实验。73个变更均为治理设计文件，产品/框架/vcpkg无改。
- B12-r2/B13新增归档63文件保真：58原始字节一致，5仅CRLF/LF，内容差异0。归档scope未扩大。33条原始日志排除diff-check且未清洗，精确列表见diff-check-exclusions.log。

| 检查 | 结果 | exit |
| --- | --- | --- |
| validate-task | PASS | 0 |
| validate-docs，普通模式 | PASS | 0 |
| validate-reuse，冻结Head | PASS governance only | 0 |
| 范围git diff --check | PASS | 0 |

机器检查4 passed、0 failed、0 skipped；独立语义发现1项阻断。全矩阵548＝545 pending＋3 proposal，closedFiles=0；inventory=1401、units=20、DEBT=16、UNRESOLVED=1399。真实命令、日志与validator/helper/driver SHA-256见acceptance.json。

NOT RUN：C++/GUI/性能、极大内存/超长边界、API探测重跑、生产返回值扰动及独立oracle、DeepEvidence、远程推送/CI。GitHub443不可达是指派背景，本轮未验证远程交付。不得以本报告授全T01 PASS、T02准入或算法替换批准。
