# REUSE-T01 第八批返工独立复验

**PASS，仅关闭 B8-P2-01 的登记返工问题。** 冻结 `6d4c254548d2784aa25a2b87978e3d7fc0ab60af`，对照原FAIL冻结 `bb997f1cfb89c8d8eb24b457d2c82d4c0711af44`。不判REUSE-T01整体通过或生产算法合规。

- `reuse/functions/io.json` 的 io.Json.Parser.readHex4 已改为 Json.cpp:723–747；本次重新读取完整签名和函数体，包含4位循环、截断/非十六进制拒绝、结果写入和闭括号。它是 Parser 的成员定义，符合 qualifiedSymbol 的匿名命名空间/类作用域，不再是 parseString 内调用分支。
- 独立重算UTF8区段摘要（CRLF转LF，单末尾LF）得到 `0aafea170b8523491cc3c8a45584848bcea661af38bc7c4c9709aea0597c2100`，与登记一致。具体记录为 binding-check.json、source-section.txt、fixed-row.json。
- 与原FAIL冻结比较，此行仅变更startLine/endLine/sha256；decision/status均仍为pending，source路径、qualifiedSymbol、targets及审计边界保留。产品目录diff为空（git diff --quiet exit0），未以返工关闭库存或准许新核心。
- 原FAIL归档 `traceability/acceptance/REUSE-T01-batch8-r1-20261022/` 与原TEMP根42文件逐一比较：42均存在；37逐字节相同；acceptance.md、check.ps1、replay-search.ps1、search-3.stdout.txt、search-4.stdout.txt仅CRLF/LF不同，归一换行后42内容全部一致。原acceptance.json仍为bb997f1/result=FAIL/B8-P2-01，保留失败与日志，未递归归档repo。逐文件结果见archive-check.json。

真实门禁：task/docs/reuse全部exit0，passed=3、skipped=0、failed=0；完整输出为task.log/docs.log/reuse.log，独立exit.txt均0。机器输出仍为functions=220、closedFiles=0，治理PASS不代表整体语义合规。

独立shared clone的HEAD为上述冻结SHA，clone/checkout exit0，验收前后status干净。本代理只写TEMP根报告/日志，未改父实施树、产品、上游或vcpkg，未提交、推送、合入或派子代理。

NOT RUN：C++/API/GUI（此次只修登记区段，按分工无需重复）；原轮已核且本次未变的19项及8条检索不重复执行；未验其他批次、全库DeepEvidence或全T01退出。
