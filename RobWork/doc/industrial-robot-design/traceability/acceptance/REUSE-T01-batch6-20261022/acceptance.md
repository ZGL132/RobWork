第六批独立验收：PASS（仅治理登记）

冻结 SHA：`ee9cc79bd68ec27b8b2f95901dddd78d6c57d5d2`。

本批28条新增关系（core 1、dynamics 1、runtime 18、selection 8）未发现需返工项。逐条核对了源码区段、全限定作用域、需求/设计契约、真实构建目标及上游候选差异。28条源码摘要、28条CMake输入摘要、12条catalog摘要、2条catalog测试摘要、2条PTP附注证据摘要均一致。batch6-search五条命令已真实重放，退出码与命中集合5/5一致。task/docs/reuse门禁及范围diff检查退出码均为0；完整日志保存在本目录。

关键语义：core仍为单位换算唯一owner，但不豁免复用；Statistics样本mean不能替代时间加权RMS；DOM getUnit只提供比例且有loader重依赖，不能替代强类型量纲契约；runtime大端、selection小端及两处u32At的memcpy主机序差异均如实登记。BINArchive协议不等同于产品固定字节序协议，runtime owner不授予模型构造/写入权限。PTP附注仅对应3个标量/工具用例，没有Q生产实测。

当前174条登记中171 pending、3 proposal，closedFiles=0；本批28条均pending，相关能力仍candidate。这是治理登记通过，不是全部实际功能审计完成或算法合规声明，不批准REUSE-T01整体退出、REUSE-T02、算法替换、新核心或上游扩展。

本次未运行C++/GUI、上游完整测试、API重探测、跨端执行、生产断开/扰动或全库DeepEvidence。未修改父工作树，未提交、推送或合入；独立checkout最终为冻结SHA且干净。同提交前批API归档不在本次重新背书范围，范围diff结论不扩大到整提交的全部历史归档日志。

结构化详情及证据清单见 `acceptance.json`。
